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
    client = APIClient("127.0.0.1", 16053, expected_name="dynamic-host", client_info="Wallpanel tests")
    probes = asyncio.Queue()
    actions = asyncio.Queue()
    latest = {}
    with socket.socket() as port_check:
        port_check.bind(("127.0.0.1", 16053))
    with tempfile.TemporaryDirectory(prefix="wallpanel-api-") as directory:
        process = await asyncio.create_subprocess_exec(
            str(binary), cwd=directory, env={**os.environ, "WALLPANEL_API_TEST": "1"},
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
            probe = next(item for item in entities if isinstance(item, TextSensorInfo) and item.name == "Transport probe")
            wake = next(item for item in entities if isinstance(item, SelectInfo) and "Page 8" in item.options)

            def on_state(state):
                if isinstance(state, TextSensorState) and state.key == probe.key and not state.missing_state:
                    value = json.loads(state.state)
                    latest.update(value)
                    probes.put_nowait(value)

            def on_action(action):
                if not action.is_event:
                    actions.put_nowait(action)

            client.subscribe_home_assistant_states_and_services(on_state, on_action, lambda entity, attr: None)

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

            mixed = json.loads(json.dumps(snapshot))
            mixed["pages"][0].update(layout="2x2", buttons=mixed["pages"][0]["buttons"][:4])
            mixed["pages"][1].update(layout="4x2", buttons=mixed["pages"][1]["buttons"][:8])
            previous = await wait_probe()
            await send(mixed, "mixed")
            await wait_probe(lambda value: value["tick"] > previous["tick"] + 250)
            await tap(600, 307)
            await expect_entity(mixed["pages"][0]["buttons"][3]["entity"])
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
            await wait_probe(lambda value: value["sensor_hold"] and value["compact_clock"])
            client.select_command(wake.key, "Homepage")
            await client.execute_service(service["test_wake"], {})
            await wait_probe(lambda value: value["homepage_visible"])
            await tap(716, 456)
            settings = await wait_probe(lambda value: value["settings_visible"])
            await tap(200, 142)
            await wait_probe(lambda value: value["restarts"] == settings["restarts"] + 1)
            await tap(600, 142)
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
            await send(snapshot, "restore")
            warm = await wait_probe(lambda value: value["last_name"] == "X" * 80 and not value["compact_clock"])
            warm_heap = warm["heap_bytes"]
            for index in range(10):
                await send(snapshot, f"repeat-{index}")
                warm_tick = warm["tick"]
                warm = await wait_probe(lambda value: value["tick"] > warm_tick + 100)
            assert warm["heap_bytes"] - warm_heap < 16384, "Repeated transfers grow retained heap"
            size = len(json.dumps(unicode_snapshot).encode())
            print(f"Native API passed: 144 controls, Homepage, Settings, sensor wake/hold, "
                  f"{size} bytes, {len(unicode_chunks)} chunks.")
            print(f"128 KiB host stack; retained host heap {warm['heap_bytes']} bytes "
                  f"(change after repeated transfers: {warm['heap_bytes'] - warm_heap:+d}).")
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
