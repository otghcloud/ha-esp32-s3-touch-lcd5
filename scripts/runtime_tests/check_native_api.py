"""Verify large blueprint snapshots and touch routing through the local native API."""

import asyncio
from collections import deque
import json
import os
from pathlib import Path
import resource
import socket
import sys
import tempfile

from aioesphomeapi import APIClient, SelectInfo, TextSensorInfo, TextSensorState
from jinja2.nativetypes import NativeEnvironment

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from test_blueprint import BlueprintTests


def limit_stack():
    _, hard = resource.getrlimit(resource.RLIMIT_STACK)
    resource.setrlimit(resource.RLIMIT_STACK, (128 * 1024, hard))


async def check():
    binary = ROOT / "scripts/runtime_tests/.esphome/build/dynamic-host/.pioenvs/dynamic-host/program"
    fixture = BlueprintTests()
    fixture.setUp()
    snapshot = fixture.full_snapshot("X" * 80)
    log = deque(maxlen=200)
    ready = asyncio.get_running_loop().create_future()
    endpoint_seen = asyncio.get_running_loop().create_future()
    firmware_seen = asyncio.get_running_loop().create_future()
    release_states = asyncio.Queue()
    client = APIClient("127.0.0.1", 16053, expected_name="dynamic-host", client_info="Wallpanel tests")
    probes = asyncio.Queue()
    actions = asyncio.Queue()
    latest = {}
    with socket.socket() as port_check:
        port_check.bind(("127.0.0.1", 16053))
    with tempfile.TemporaryDirectory(prefix="wallpanel-api-") as directory:
        process = await asyncio.create_subprocess_exec(
            str(binary), cwd=directory, env={
                **os.environ, "WALLPANEL_API_TEST": "1",
                # mallinfo2 counts freed tcache blocks as allocated; measure live allocations instead.
                "GLIBC_TUNABLES": "glibc.malloc.tcache_count=0",
            },
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT,
            preexec_fn=limit_stack,
        )

        async def read_logs():
            async for line in process.stdout:
                text = line.decode(errors="replace").rstrip()
                log.append(text)
                if "All runtime dashboard assertions passed" in text and not ready.done():
                    ready.set_result(None)
            if not ready.done():
                ready.set_exception(RuntimeError("Host exited before runtime assertions passed"))

        reader = asyncio.create_task(read_logs())
        succeeded = False
        try:
            await asyncio.wait_for(ready, 30)
            await client.connect(login=True)
            entities, services = await client.list_entities_services()
            service = {item.name: item for item in services}
            assert "set_dashboard" not in service, "Only chunked dashboard transport is exposed"
            probe = next(item for item in entities if isinstance(item, TextSensorInfo) and item.name == "Transport probe")
            endpoint = next(item for item in entities if isinstance(item, TextSensorInfo) and item.name == "Dashboard endpoint")
            firmware = next(item for item in entities if isinstance(item, TextSensorInfo) and item.name == "Firmware release")
            release_status = next(item for item in entities if isinstance(item, TextSensorInfo) and item.name == "Release status")
            wake = next(item for item in entities if isinstance(item, SelectInfo) and "Page 8" in item.options)

            def on_state(state):
                if isinstance(state, TextSensorState) and not state.missing_state:
                    if state.key == firmware.key and not firmware_seen.done():
                        firmware_seen.set_result(state.state)
                    if state.key == release_status.key:
                        release_states.put_nowait(state.state)
                if isinstance(state, TextSensorState) and state.key == endpoint.key and not state.missing_state:
                    if not endpoint_seen.done():
                        endpoint_seen.set_result(state.state)
                if isinstance(state, TextSensorState) and state.key == probe.key and not state.missing_state:
                    value = json.loads(state.state)
                    latest.update(value)
                    probes.put_nowait(value)

            def on_action(action):
                if not action.is_event:
                    actions.put_nowait(action)

            client.subscribe_home_assistant_states_and_services(on_state, on_action, lambda entity, attr: None)
            assert await asyncio.wait_for(endpoint_seen, 5) == "esphome.dynamic_host_set_dashboard_chunk"
            expected_release = (ROOT / "VERSION").read_text().strip()
            assert await asyncio.wait_for(firmware_seen, 5) == f"wallpanel/{expected_release}"
            endpoint_started = asyncio.get_running_loop().time()

            async def wait_probe(predicate=lambda value: True):
                async with asyncio.timeout(10):
                    while True:
                        value = await probes.get()
                        if predicate(value):
                            return value

            async def send(body, transfer):
                chunks = fixture.chunks(body)
                for chunk in chunks:
                    await client.execute_service(service["set_dashboard_chunk"], {
                        **chunk, "transfer": transfer, "index": int(chunk["index"]), "total": int(chunk["total"]),
                    })
                return chunks

            async def expect_release_status(expected):
                async with asyncio.timeout(10):
                    while await release_states.get() != expected:
                        pass

            async def tap(x, y):
                await client.execute_service(service["test_tap"], {"x": x, "y": y})

            async def expect_entity(entity):
                action = await asyncio.wait_for(actions.get(), 5)
                assert action.service == "light.toggle", action
                assert action.data["entity_id"] == entity, action

            baseline = await wait_probe()
            chunks = fixture.chunks(snapshot)
            for chunk in chunks[:-1]:
                await client.execute_service(service["set_dashboard_chunk"], {
                    **chunk, "index": int(chunk["index"]), "total": int(chunk["total"]),
                })
            partial = await wait_probe(lambda value: value["tick"] > baseline["tick"] + 250)
            assert partial["last_name"] == baseline["last_name"], "Partial native transfer changed the UI"
            last = chunks[-1]
            await client.execute_service(service["set_dashboard_chunk"], {
                **last, "index": int(last["index"]), "total": int(last["total"]),
            })
            await wait_probe(lambda value: value["last_name"] == "X" * 80)
            client.select_command(wake.key, "Page 8")
            await client.execute_service(service["test_wake"], {})
            await wait_probe(lambda value: value["page8_visible"])
            await tap(720, 335)
            await expect_entity(snapshot["pages"][7]["buttons"][17]["entity"])
            await tap(768, 456)
            await wait_probe(lambda value: value["menu_visible"])
            await tap(107, 142)
            await wait_probe(lambda value: not value["menu_visible"] and not value["page8_visible"])
            assert actions.empty(), "Menu navigation invoked an entity action"
            await tap(720, 335)
            await expect_entity(snapshot["pages"][0]["buttons"][17]["entity"])

            latest_release = "4294967295.4294967295.4294967295"
            await send({**snapshot, "latest_release": latest_release}, "new-stable-release")
            await expect_release_status(
                "Up to date" if snapshot["blueprint_release"] == latest_release else f"Update available: {latest_release}"
            )
            await send({**snapshot, "blueprint_release": "0.0.0"}, "blueprint-release-mismatch")
            await expect_release_status("Firmware/blueprint mismatch: 0.0.0")

            mixed = json.loads(json.dumps(snapshot))
            for page, layout, capacity in ((0, "2x2", 4), (1, "4x2", 8)):
                mixed["pages"][page]["layout"] = layout
                for button in mixed["pages"][page]["buttons"][capacity:]:
                    button.update(entity="", name="", state="", icon="", tap_action="default")
            active_entities = {button["entity"] for page in mixed["pages"] for button in page["buttons"]}
            mixed["light_details"] = [light for light in mixed["light_details"] if light["entity"] in active_entities]
            previous = await wait_probe()
            await send(mixed, "mixed")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 250)
            await tap(600, 307)
            await expect_entity(mixed["pages"][0]["buttons"][3]["entity"])
            reserved = json.loads(json.dumps(mixed))
            reserved["pages"][0].update(layout="4x2", compact=False)
            reserved["pages"][0]["buttons"][2]["entity"] = ""
            active_entities = {button["entity"] for page in reserved["pages"] for button in page["buttons"]}
            reserved["light_details"] = [
                light for light in reserved["light_details"] if light["entity"] in active_entities
            ]
            previous = await wait_probe()
            await send(reserved, "reserved-gaps")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 150)
            await tap(497, 142)
            await asyncio.sleep(.15)
            assert actions.empty(), "Reserved empty slot dispatched an entity action"
            await tap(692, 142)
            await expect_entity(reserved["pages"][0]["buttons"][3]["entity"])
            previous = await wait_probe()
            await send(mixed, "restore-mixed")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 150)
            await tap(768, 456)
            await wait_probe(lambda value: value["menu_visible"])
            await tap(302, 142)
            await wait_probe(lambda value: not value["menu_visible"])
            await tap(689, 307)
            await expect_entity(mixed["pages"][1]["buttons"][7]["entity"])

            fixture.inputs["page_order"] = [{"page": "8", "title": "Upstairs"}, {"page": "1", "title": "Office"}]
            reordered = fixture.snapshot()
            previous = await wait_probe()
            await send(reordered, "reordered")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 250)
            await tap(768, 456)
            await wait_probe(lambda value: value["menu_visible"])
            await tap(107, 142)
            await wait_probe(lambda value: value["page8_visible"] and not value["menu_visible"])
            await tap(720, 335)
            await expect_entity(reordered["pages"][7]["buttons"][17]["entity"])
            client.select_command(wake.key, "Page 1")
            await client.execute_service(service["test_wake"], {})
            await wait_probe(lambda value: not value["page8_visible"])
            await tap(720, 335)
            await expect_entity(reordered["pages"][0]["buttons"][17]["entity"])
            fixture.inputs["page_order"] = []

            await tap(768, 456)
            await wait_probe(lambda value: value["menu_visible"])
            unicode_snapshot = fixture.full_snapshot("\U0001f600" * 80)
            for page in unicode_snapshot["pages"]:
                for button in page["buttons"]:
                    button["indicator"] = dict(state="\U0001f7e2" * 8, active=True, available=True)
            unicode_chunks = await send(unicode_snapshot, "unicode")
            await wait_probe(lambda value: value["menu_visible"])
            await tap(107, 142)
            await wait_probe(lambda value: value["last_name"] == "\U0001f600" * 80 and not value["menu_visible"])
            home_snapshot = json.loads(json.dumps(snapshot))
            home_snapshot.update(
                navigation_layout="6x3", homepage={"enabled": True, "position": 0, "title": "Welcome"},
                localization={"time": "12h", "date": "iso"}, wake_hold=True,
            )
            await send(home_snapshot, "home-hold")
            await wait_probe(lambda value: value["sensor_hold"] and value["twelve_hour"])
            client.select_command(wake.key, "Homepage")
            await client.execute_service(service["test_wake"], {})
            await wait_probe(lambda value: value["homepage_visible"])
            await tap(716, 456)
            settings = await wait_probe(lambda value: value["settings_visible"])
            await tap(200, 142)
            await wait_probe(lambda value: value["restarts"] == settings["restarts"] + 1)
            await tap(140, 307)
            asleep = await wait_probe(lambda value: not value["backlight"] and not value["sensor_hold"])
            await send(home_snapshot, "manual-sleep-refresh")
            still_asleep = await wait_probe(lambda value: value["tick"] > asleep["tick"] + 250)
            assert not still_asleep["backlight"], "Periodic sensor refresh defeated manual Sleep"
            await client.execute_service(service["wake_display"], {})
            await wait_probe(lambda value: value["backlight"] and value["sensor_hold"] and value["homepage_visible"])
            client.select_command(wake.key, "Page 1")
            await client.execute_service(service["test_wake"], {})
            await wait_probe(lambda value: not value["homepage_visible"] and not value["settings_visible"])
            client.select_command(wake.key, "Homepage")
            await client.execute_service(service["wake_display"], {})
            previous = await wait_probe()
            still_awake = await wait_probe(lambda value: value["tick"] > previous["tick"] + 250)
            assert not still_awake["homepage_visible"], "Already-awake activation jumped to the saved wake page"
            assert actions.empty(), "Local Settings/wake actions invoked a Home Assistant entity service"
            await client.execute_service(service["test_expire"], {})
            await wait_probe(lambda value: not value["sensor_hold"])

            details = BlueprintTests()
            details.setUp()
            details.inputs.update(
                page_1_button_1="light.detail", page_1_button_2="light.simple",
                page_1_button_3="light.dim", page_1_button_4="light.temp",
                light_details=[dict(light="light.detail", related_1="switch.outlet",
                                    related_2="automation.auto")],
            )
            details.states.update({entity: "on" for entity in
                                   ("light.detail", "light.simple", "light.dim", "light.temp", "automation.auto")})
            details.states["switch.outlet"] = "off"
            details.attributes.update({
                "light.detail": dict(supported_color_modes=["rgb", "color_temp"], brightness=128,
                                     hs_color=[90, 50], color_temp_kelvin=3000,
                                     min_color_temp_kelvin=2200, max_color_temp_kelvin=6500),
                "light.simple": dict(supported_color_modes=["onoff"]),
                "light.dim": dict(supported_color_modes=["brightness"], brightness=100),
                "light.temp": dict(supported_color_modes=["color_temp"], brightness=100,
                                   min_color_temp_kelvin=2500, max_color_temp_kelvin=6000),
            })

            async def touch(phase, x=0, y=0):
                await client.execute_service(service["test_touch"], {"phase": phase, "x": x, "y": y})

            async def hold(x, y, drift=(0, 0), extra_contacts=False):
                await touch(0, x, y)
                if extra_contacts:
                    await touch(4, x + drift[0], y + drift[1])
                elif drift != (0, 0):
                    await touch(1, x + drift[0], y + drift[1])
                await wait_probe(lambda value: value["detail_visible"])
                await touch(2)
                assert actions.empty(), "Opening details leaked a toggle or edit"

            async def expect_action(name, entity, parameter=None):
                action = await asyncio.wait_for(actions.get(), 5)
                assert action.service == name and action.data == {"entity_id": entity}, action
                rendered = {key: NativeEnvironment().from_string(value).render()
                            for key, value in action.data_template.items()}
                if parameter is not None:
                    assert rendered == parameter, (action, rendered, parameter)
                return rendered

            previous = await wait_probe()
            await send(details.snapshot(), "detail-model")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 150)
            details.inputs["page_1_button_1_tap_action"] = "details"
            previous = await wait_probe()
            await send(details.snapshot(), "detail-short-tap")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 150)
            await tap(200, 100)
            await wait_probe(lambda value: value["detail_visible"])
            await touch(2)
            assert actions.empty(), "Details tap leaked a toggle or duplicate release"
            await tap(69, 107)
            await expect_action("light.toggle", "light.detail")
            await tap(664, 456)
            await wait_probe(lambda value: not value["detail_visible"])
            details.inputs["page_1_button_1_tap_action"] = "default"
            previous = await wait_probe()
            await send(details.snapshot(), "detail-default-tap")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 150)
            await tap(200, 100)
            await expect_action("light.toggle", "light.detail")
            for x, y in ((200, 100), (200, 180), (375, 210)):
                await hold(x, y)
                await tap(664, 456)
                await wait_probe(lambda value: not value["detail_visible"])
            before_owner_loss = await wait_probe()
            await touch(0, 78, 142)
            await touch(5, 78, 142)
            cancelled = await wait_probe(lambda value: value["tick"] > before_owner_loss["tick"] + 700)
            assert not cancelled["detail_visible"], "Another nearby contact took over the hold"
            await touch(2)
            assert actions.empty(), "Losing the original contact leaked a toggle"
            await hold(78, 142, extra_contacts=True)
            await wait_probe(lambda value: value["slider_visible"] and value["detail_value"] == "50%")
            await tap(695, 168)
            await expect_action("switch.toggle", "switch.outlet")
            await tap(695, 338)
            await expect_action("automation.toggle", "automation.auto")
            details.inputs["reverse_active"] = ["switch.outlet"]
            await send(details.snapshot(), "detail-reversed-related")
            await wait_probe(lambda value: value["related_white"])
            await tap(695, 168)
            await expect_action("switch.toggle", "switch.outlet")
            await touch(0, 69, 107)
            await touch(4, 69, 107)
            await touch(2)
            await expect_action("light.toggle", "light.detail")
            await touch(0, 266, 250)
            await touch(5, 266, 158)
            await touch(2)
            await asyncio.sleep(.15)
            assert actions.empty(), "Losing the original contact committed a slider edit"
            await touch(0, 240, 250)
            await touch(4, 266, 158)
            await wait_probe(lambda value: value["detail_value"] == "100%")
            assert actions.empty(), "Brightness drag sent commands before release"
            details.attributes["light.detail"]["brightness"] = 20
            previous = await wait_probe()
            await send(details.snapshot(), "detail-during-drag")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 150)
            assert latest["detail_value"] == "100%", "Authoritative update reset the active preview"
            await touch(2)
            await expect_action("light.turn_on", "light.detail", {"brightness_pct": 100})
            await touch(2)
            await asyncio.sleep(.15)
            assert actions.empty(), "Duplicate release repeated the light command"
            details.attributes["light.detail"]["brightness"] = 255
            pending = await wait_probe(lambda value: value["detail_status"] == "Waiting for light...")
            await send(details.snapshot(), "detail-confirmed")
            await wait_probe(lambda value: value["tick"] > pending["tick"] and value["detail_status"] == "")
            await touch(0, 266, 158)
            await touch(2)
            await expect_action("light.turn_on", "light.detail", {"brightness_pct": 100})
            await wait_probe(lambda value: value["detail_status"] == "Waiting for light...")
            await send(details.snapshot(), "detail-identical-confirmation")
            await wait_probe(lambda value: value["detail_status"] == "")
            await tap(505, 247)
            await wait_probe(lambda value: value["detail_value"] == "3000 K")
            await touch(0, 266, 250)
            await touch(1, 266, 158)
            assert actions.empty(), "Temperature drag sent an intermediate command"
            await touch(2)
            await expect_action("light.turn_on", "light.detail", {"color_temp_kelvin": 6500})
            await tap(505, 343)
            await wait_probe(lambda value: value["wheel_visible"] and not value["slider_visible"])
            await touch(0, 240, 252)
            await touch(1, 359, 252)
            await wait_probe(lambda value: value["detail_value"] == "#FF0001")
            await touch(2)
            colour = await expect_action("light.turn_on", "light.detail")
            assert isinstance(colour["hs_color"], list) and len(colour["hs_color"]) == 2, colour
            assert min(colour["hs_color"][0], 360 - colour["hs_color"][0]) < 1, colour
            assert 99 < colour["hs_color"][1] <= 100, colour
            await tap(505, 151)
            await touch(0, 266, 371)
            await touch(2)
            await expect_action("light.turn_off", "light.detail")
            await wait_probe(lambda value: "not confirmed" in value["detail_status"])
            await tap(664, 456)
            await wait_probe(lambda value: not value["detail_visible"])
            await hold(478, 142)
            await wait_probe(lambda value: not value["slider_visible"] and not value["wheel_visible"])
            await tap(69, 107)
            await expect_action("light.toggle", "light.simple")
            await tap(664, 456)
            await hold(78, 307)
            await wait_probe(lambda value: value["slider_visible"] and not value["wheel_visible"])
            await tap(664, 456)
            await hold(478, 307)
            await wait_probe(lambda value: value["slider_visible"] and not value["wheel_visible"])
            await tap(664, 456)
            details.inputs["page_1_layout"] = "4x2"
            previous = await wait_probe()
            await send(details.snapshot(), "detail-dense-holds")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 150)
            for x in (107, 497, 692):
                # Finger centres can sit just below the small icon, above its title.
                await hold(x, 127, drift=(35, 25), extra_contacts=True)
                await wait_probe(lambda value: value["slider_visible"])
                if x != 692:
                    await tap(664, 456)
            details.states["light.temp"] = "unavailable"
            await send(details.snapshot(), "detail-unavailable")
            await wait_probe(lambda value: value["detail_status"] == "Light unavailable")
            await touch(0, 266, 250)
            await touch(2)
            await tap(69, 107)
            await asyncio.sleep(.15)
            assert actions.empty(), "Unavailable details emitted a service command"
            details.inputs["page_1_button_4"] = []
            await send(details.snapshot(), "remove-detail-primary")
            await wait_probe(lambda value: not value["detail_visible"])
            details.inputs.update(page_1_button_1="lock.test", page_1_layout="2x2",
                                  reverse_active=["lock.test"])
            for state, command in (("locked", "lock.unlock"), ("unlocked", "lock.lock")):
                details.states["lock.test"] = state
                previous = await wait_probe()
                await send(details.snapshot(), f"reversed-lock-{state}")
                await wait_probe(lambda value: value["tick"] > previous["tick"] + 150)
                await tap(200, 142)
                await expect_action(command, "lock.test")

            async def replace_model(body, transfer):
                previous = await wait_probe()
                await send(body, transfer)
                await wait_probe(lambda value: value["tick"] > previous["tick"] + 150)

            details.inputs["page_1_button_1"] = "script.start_tv"
            for state in ("off", "on"):
                details.states["script.start_tv"] = state
                body = details.snapshot()
                body["pages"][0]["buttons"][0]["indicator"] = dict(
                    state="playing", available=True, active=True,
                )
                await replace_model(body, f"script-{state}")
                for _ in range(2):
                    await tap(200, 142)
                    await expect_action("script.turn_on", "script.start_tv")
            body["pages"][0]["buttons"][0]["indicator"] = dict(
                state="unavailable", available=False, active=False,
            )
            await replace_model(body, "indicator-unavailable")
            await tap(200, 142)
            await expect_action("script.turn_on", "script.start_tv")
            body["pages"][0]["buttons"][0].update(
                state="unavailable", indicator=dict(state="on", available=True, active=True),
            )
            await replace_model(body, "actor-unavailable-indicator-active")
            await tap(200, 142)
            await asyncio.sleep(.15)
            assert actions.empty(), "An independent indicator enabled an unavailable actor"

            details.inputs.update(
                page_1_button_1="lock.test", page_1_button_1_tap_action="details",
                reverse_active=[],
                lock_details=[dict(lock="lock.test", related_1="switch.outlet",
                                   related_2="script.arrive", related_1_title="Auto-lock")],
            )
            details.states.update({"lock.test": "locked", "script.arrive": "on"})

            def lock_model():
                body = details.snapshot()
                body["pages"][0]["buttons"][0]["indicator"] = dict(
                    state="unlocked", available=True, active=True,
                )
                return body

            await replace_model(lock_model(), "lock-detail-model")
            await tap(200, 142)
            await wait_probe(lambda value: value["detail_visible"] and value["detail_status"] == "LOCKED")
            await touch(2)
            assert actions.empty(), "Lock details tap also issued a command"
            await tap(600, 169)
            await expect_action("switch.toggle", "switch.outlet")
            await tap(600, 339)
            await expect_action("script.turn_on", "script.arrive")
            await tap(200, 254)
            await expect_action("lock.unlock", "lock.test")
            # A command does not optimistically change actual lock state.
            await tap(200, 254)
            await expect_action("lock.unlock", "lock.test")
            details.states["lock.test"] = "unlocked"
            await replace_model(lock_model(), "lock-detail-unlocked")
            await wait_probe(lambda value: value["detail_status"] == "UNLOCKED")
            await tap(200, 254)
            await expect_action("lock.lock", "lock.test")
            await touch(0, 200, 254)
            details.states["lock.test"] = "locked"
            await replace_model(lock_model(), "lock-change-during-touch")
            await touch(2)
            await asyncio.sleep(.15)
            assert actions.empty(), "Actual lock-state change completed a stale, different operation"
            for state in ("locking", "unlocking", "opening", "jammed", "open", "unknown", "unavailable"):
                details.states["lock.test"] = state
                await replace_model(lock_model(), f"lock-{state}")
                await wait_probe(lambda value: value["detail_status"] == state.upper())
                await tap(200, 254)
                await asyncio.sleep(.1)
                assert actions.empty(), f"Non-actionable lock state {state} issued a guessed command"
            details.states["lock.test"] = "locked"
            await replace_model(lock_model(), "lock-restore")
            await tap(664, 456)
            await wait_probe(lambda value: not value["detail_visible"])
            details.inputs["page_1_button_1_tap_action"] = "default"
            await replace_model(lock_model(), "lock-default-tap")
            await tap(200, 142)
            await expect_action("lock.unlock", "lock.test")
            await hold(200, 180)
            await wait_probe(lambda value: value["detail_visible"] and not value["slider_visible"]
                             and not value["wheel_visible"])
            details.inputs["page_1_button_4"] = "lock.test"
            await replace_model(lock_model(), "duplicate-lock")
            details.inputs["page_1_button_1"] = []
            await replace_model(details.snapshot(), "remove-exact-lock-source")
            await wait_probe(lambda value: not value["detail_visible"])
            assert actions.empty(), "Closing lock details emitted an action"

            # A unique visible label acknowledges atomic application, not stale pooled text or elapsed time.
            heap_snapshot = json.loads(json.dumps(unicode_snapshot))
            heap_marker = "\U0001f600" * 79 + "\U0001f680"
            heap_snapshot["pages"][0]["buttons"][17]["name"] = heap_marker
            await send(heap_snapshot, "restore-worst-case")
            await expect_release_status("Latest release unavailable")
            warm = await wait_probe(lambda value: value["last_name"] == heap_marker and not value["twelve_hour"])
            await asyncio.sleep(5.2)
            quiet = await wait_probe(lambda value: value["tick"] > warm["tick"] + 5100)
            assert quiet["release_updates"] == warm["release_updates"], "Unchanged release status was republished"
            warm_heap = quiet["heap_bytes"]
            for index in range(10):
                heap_marker = "\U0001f600" * 79 + chr(0x1F601 + index)
                heap_snapshot["pages"][0]["buttons"][17]["name"] = heap_marker
                await send(heap_snapshot, f"repeat-{index}")
                warm = await wait_probe(lambda value: value["last_name"] == heap_marker)
            assert warm["heap_bytes"] - warm_heap < 16384, (
                f"Repeated transfers grow retained heap: {warm_heap} -> {warm['heap_bytes']}"
            )
            await asyncio.sleep(max(0, 31 - (asyncio.get_running_loop().time() - endpoint_started)))
            endpoint_probe = await wait_probe(lambda value: value["tick"] >= baseline["tick"] + 31000)
            assert endpoint_probe["endpoint_updates"] == 1, "Constant endpoint was republished after boot"
            size = len(json.dumps(unicode_snapshot).encode())
            print(f"Native API passed: 144 controls, reserved gaps, configurable taps, whole-tile holds, "
                  f"light/lock details, scripts, independent indicators and typed release-only commands, "
                  f"Homepage, Settings, sensor wake/hold, "
                  f"{size} bytes, {len(unicode_chunks)} chunks.")
            print(f"128 KiB host stack; retained host heap {warm['heap_bytes']} bytes "
                  f"(change after repeated transfers: {warm['heap_bytes'] - warm_heap:+d}).")
            print("Dashboard endpoint published once at boot and remained cached beyond 30 seconds.")
            succeeded = True
        finally:
            if not succeeded:
                print("\n".join(log), file=sys.stderr)
                print(f"Last transport probe: {latest}", file=sys.stderr)
            await client.disconnect()
            if process.returncode is None:
                process.terminate()
                try:
                    await asyncio.wait_for(process.wait(), 5)
                except TimeoutError:
                    process.kill()
                    await process.wait()
            await reader


if __name__ == "__main__":
    asyncio.run(check())
