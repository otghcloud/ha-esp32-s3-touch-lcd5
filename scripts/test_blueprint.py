import ast
import copy
import json
import math
import re
import unittest
from pathlib import Path
from types import SimpleNamespace

from jinja2 import Environment, StrictUndefined
import yaml


ROOT = Path(__file__).resolve().parents[1]


class InputKey(str):
    pass


class BlueprintLoader(yaml.SafeLoader):
    pass


BlueprintLoader.add_constructor(
    "!input", lambda loader, node: InputKey(loader.construct_scalar(node))
)


def resolve(value, inputs):
    if isinstance(value, InputKey):
        return inputs[value]
    if isinstance(value, list):
        return [resolve(item, inputs) for item in value]
    if isinstance(value, dict):
        return {key: resolve(item, inputs) for key, item in value.items()}
    return value


def is_number(value):
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


class BlueprintTests(unittest.TestCase):
    def setUp(self):
        with (ROOT / "wallpanel_blueprint.yaml").open(encoding="utf-8") as source:
            self.blueprint = yaml.load(source, Loader=BlueprintLoader)
        self.inputs = {"panel_device": "test-panel"}
        for section in self.blueprint["blueprint"]["input"].values():
            for name, definition in section.get("input", {}).items():
                self.inputs[name] = copy.deepcopy(definition["default"])
        self.states = {}
        self.attributes = {}
        self.device_entities = ["sensor.renamed_dashboard_endpoint"]
        self.env = Environment(undefined=StrictUndefined)
        # HA preserves Unicode unless ensure_ascii is explicitly requested.
        self.env.filters["to_json"] = lambda value, ensure_ascii=False: json.dumps(value, ensure_ascii=ensure_ascii)
        # HA rejects nonnumeric input when no int-filter default is supplied.
        self.env.filters["int"] = int
        self.env.filters["regex_match"] = lambda value, pattern: re.match(pattern, value) is not None
        self.env.globals.update(
            states=lambda entity: self.states.get(entity, "unknown"),
            state_attr=lambda entity, attribute: self.attributes.get(entity, {}).get(attribute),
            device_entities=lambda device: self.device_entities if device == "test-panel" else [],
            is_number=is_number,
            version=lambda value: tuple(map(int, value.split("."))),
        )

    def variables(self):
        result = resolve(self.blueprint["variables"], self.inputs)
        result["endpoint"] = self.env.from_string(result["endpoint"]).render(**result).strip()
        for key in ("firmware_release", "latest_release", "release_notice"):
            result[key] = self.env.from_string(result[key]).render(**result).strip()
        for key in ("effective_pages", "watched_entities", "ignored_slots"):
            result[key] = ast.literal_eval(self.env.from_string(result[key]).render(**result).strip())
        result["configuration_error"] = self.env.from_string(result["configuration_error"]).render(**result).strip()
        return result

    def snapshot(self):
        source = self.blueprint["actions"][-1]["default"][1]["variables"]["dashboard_snapshot"]
        return ast.literal_eval(self.env.from_string(source).render(**self.variables()).strip())

    def chunks(self, snapshot):
        repeat = self.blueprint["actions"][-1]["default"][2]["repeat"]
        variables = {"dashboard_snapshot": snapshot, "transfer": "test-transfer"}
        count = int(self.env.from_string(repeat["count"]).render(**variables))
        data = repeat["sequence"][0]["data"]
        return [
            {
                key: self.env.from_string(value).render(
                    repeat=SimpleNamespace(index=index + 1), **variables
                )
                for key, value in data.items()
            }
            for index in range(count)
        ]

    def test_light_capabilities_related_watching_and_deduplication(self):
        self.inputs.update(page_1_button_1="light.colour", page_2_button_1="light.colour",
                           page_1_button_2="light.simple", reverse_active=["light.colour"],
                           light_details=[dict(light="light.colour", related_1="automation.auto",
                                               related_1_title=" Auto ", related_2="switch.outlet")])
        self.attributes["light.colour"] = {
            "supported_color_modes": ["rgb", "color_temp"], "brightness": 128, "hs_color": [33, 75],
            "color_temp_kelvin": 3000, "min_color_temp_kelvin": 2200, "max_color_temp_kelvin": 6500,
        }
        self.attributes["light.simple"] = {"supported_color_modes": ["onoff"]}
        self.attributes["automation.auto"] = {"friendly_name": "Automatic lighting"}
        self.states["automation.auto"] = "off"
        self.assertEqual(self.variables()["configuration_error"], "")
        self.assertIn("automation.auto", self.variables()["watched_entities"])
        self.assertIn("switch.outlet", self.variables()["watched_entities"])
        result = self.snapshot()
        self.assertEqual(result["reverse_active"], ["light.colour"])
        self.assertEqual(len(result["light_details"]), 2)
        light, simple = result["light_details"]
        self.assertEqual(light, dict(
            entity="light.colour", colour_mode="", known=True, dimmable=True, colour=True, temperature=True, temperature_issue=False,
            brightness=128, kelvin=3000, min_kelvin=2200, max_kelvin=6500, hue=33, saturation=75,
            related=["automation.auto", "switch.outlet"], related_titles=["Auto", ""],
        ))
        self.assertTrue(simple["known"])
        self.assertFalse(simple["dimmable"] or simple["colour"] or simple["temperature"])
        self.assertEqual(simple["brightness"], -1)
        self.assertEqual(result["detail_entities"][0]["name"], "Automatic lighting")
        self.assertEqual(result["detail_entities"][0]["state"], "off")
        self.inputs["light_details"].append(dict(light="light.simple", related_1="automation.auto",
                                                related_1_title="Other Auto"))
        result = self.snapshot()
        self.assertEqual(len(result["detail_entities"]), 2, "shared related entities are transmitted once")
        self.assertEqual(result["light_details"][1]["related_titles"], ["Other Auto"])
        self.inputs["page_1_button_18"] = "light.hidden"
        self.inputs["page_1_layout"] = "2x2"
        self.inputs["light_details"].append(dict(light="light.hidden", related_1="switch.hidden"))
        self.assertNotIn("switch.hidden", self.variables()["watched_entities"])
        self.assertNotIn("light.hidden", [light["entity"] for light in self.snapshot()["light_details"]])

    def test_release_checks_warn_without_blocking_the_dashboard(self):
        release = self.inputs["release_sensor"] = "sensor.latest_wallpanel"
        firmware = "sensor.renamed_firmware"
        self.device_entities.append(firmware)
        current = self.variables()["blueprint_release"]
        self.states[firmware] = f"wallpanel/{current}"
        self.states[release] = current
        self.assertEqual(self.variables()["release_notice"], "")
        self.assertIn(release, self.variables()["watched_entities"])
        self.assertEqual(self.snapshot()["latest_release"], current)
        latest = "4294967295.4294967295.4294967295"
        self.states[release] = latest
        if current == latest:
            self.assertEqual(self.variables()["release_notice"], "")
        else:
            self.assertIn(f"{latest} is available", self.variables()["release_notice"])
        self.states[firmware] = "wallpanel/0.0.0"
        self.assertIn("does not match blueprint", self.variables()["release_notice"])
        self.assertEqual(self.variables()["configuration_error"], "")
        self.assertEqual(self.snapshot()["blueprint_release"], current)
        for invalid in ("unknown", "unavailable", "1.2", "01.2.3", "1.0.0-beta.1", "1.2.4294967296"):
            with self.subTest(invalid=invalid):
                self.states[release] = invalid
                self.assertEqual(self.snapshot()["latest_release"], "")
        self.states[release] = latest
        self.states[firmware] = f"wallpanel/{current}"
        notice = self.variables()["release_notice"]
        self.assertNotIn("does not match", notice)
        self.assertEqual("is available" in notice, current != latest)
        guard = self.env.from_string(self.blueprint["actions"][1]["if"])
        variables = self.variables()
        self.assertEqual(guard.render(**variables).strip(), "True")
        for trigger_id, entity, expected in (
                ("refresh", release, "False"), ("state", "light.other", "False"),
                ("state", release, "True"), ("state", firmware, "True")):
            trigger = SimpleNamespace(
                id=trigger_id, event=SimpleNamespace(data=SimpleNamespace(entity_id=entity))
            )
            self.assertEqual(guard.render(trigger=trigger, **variables).strip(), expected)

    def test_missing_and_partial_light_attributes_remain_unknown(self):
        self.inputs["page_1_button_1"] = "light.test"
        self.assertFalse(self.snapshot()["light_details"][0]["known"])
        for modes, dimmable, colour in [
                (["brightness"], True, False), (["xy"], True, True), (["white"], True, False),
                (["rgbww"], True, True), (["color_temp"], True, False)]:
            with self.subTest(modes=modes):
                self.attributes["light.test"] = {"supported_color_modes": modes, "hs_color": [22]}
                light = self.snapshot()["light_details"][0]
                self.assertEqual((light["dimmable"], light["colour"]), (dimmable, colour))
                self.assertFalse(light["temperature"], "missing Kelvin bounds cannot invent a range")
                self.assertEqual((light["hue"], light["saturation"], light["brightness"]), (-1, -1, -1))

    def test_invalid_related_configuration_is_rejected(self):
        for value in [None, "light.test", {}, [None], [{}], [dict(light="switch.bad")],
                      [dict(light="light.test", related_1="lock.bad")],
                      [dict(light="light.test", related_1="light.test")],
                      [dict(light="light.test", related_1="switch.a", related_2="switch.a")],
                      [dict(light="light.test", related_1_title=123)],
                      [dict(light="light.test"), dict(light="light.test")]]:
            with self.subTest(value=value):
                self.inputs["light_details"] = value
                self.assertTrue(self.variables()["configuration_error"])

    def test_oversized_rich_configuration_sends_no_partial_model(self):
        snapshot = self.full_snapshot("\U0001f600" * 80)
        snapshot["padding"] = "X" * 262144
        self.assertEqual(self.chunks(snapshot), [])
        error = self.blueprint["actions"][-1]["default"][3]
        self.assertEqual(self.env.from_string(error["if"]).render(dashboard_snapshot=snapshot), "True")
        self.assertEqual(error["then"][0]["data"]["level"], "error")

    def test_sparse_pages_and_buttons_keep_slots_and_escape_names(self):
        self.inputs.update(
            page_order=[{"page": "2", "title": 'Landing "night"'}],
            page_2_button_3="automation.landing",
            page_2_temperature="sensor.landing",
        )
        self.states.update({"automation.landing": "off", "sensor.landing": "21.25"})
        self.attributes.update({
            "automation.landing": {"friendly_name": 'Lighting "auto" %'},
            "sensor.landing": {"unit_of_measurement": "C"},
        })
        snapshot = self.snapshot()
        self.assertEqual(len(snapshot["pages"]), 8)
        self.assertEqual(snapshot["pages"][0]["buttons"][0]["entity"], "")
        page = snapshot["pages"][1]
        self.assertEqual(page["title"], 'Landing "night"')
        self.assertEqual(page["temperature"], "21.2 C")
        self.assertEqual(page["buttons"][2], {
            "entity": "automation.landing", "name": 'Lighting "auto" %', "state": "off", "icon": "",
            "tap_action": "default",
        })

    def test_changes_and_removals_replace_old_entities(self):
        self.inputs["page_1_button_1"] = "light.office"
        self.states["light.office"] = "on"
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["state"], "on")
        self.inputs["page_1_button_1"] = "switch.other"
        self.states["switch.other"] = "off"
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["entity"], "switch.other")
        self.inputs["page_1_button_1"] = []
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["entity"], "")

    def test_screensaver_flag_defaults_off_and_follows_blueprint_toggle(self):
        self.assertIs(self.snapshot()["screensaver_enabled"], False)
        self.inputs["screensaver_enabled"] = True
        self.assertIs(self.snapshot()["screensaver_enabled"], True)
        self.inputs["screensaver_enabled"] = False
        self.assertIs(self.snapshot()["screensaver_enabled"], False)

    def test_named_page_order_preserves_slots_and_defaults_unnamed_titles(self):
        self.inputs.update(page_1_button_1="light.office", page_3_button_1="switch.landing")
        self.assertEqual(self.snapshot()["page_order"], list(range(8)))
        self.inputs["page_order"] = [{"page": "3", "title": " Landing "}, {"page": "1"}]
        snapshot = self.snapshot()
        self.assertEqual(snapshot["page_order"], [2, 0, 1, 3, 4, 5, 6, 7])
        self.assertEqual(snapshot["pages"][2]["title"], "Landing")
        self.assertEqual(snapshot["pages"][0]["title"], "Page 1")
        self.assertEqual(snapshot["pages"][0]["buttons"][0]["entity"], "light.office")
        self.assertEqual(snapshot["pages"][2]["buttons"][0]["entity"], "switch.landing")
        self.inputs["page_order"][1]["title"] = "  "
        self.assertEqual(self.snapshot()["pages"][0]["title"], "Page 1")
        self.inputs["page_order"][0]["title"] = "\U0001f600" * 100
        self.assertEqual(self.snapshot()["pages"][2]["title"], "\U0001f600" * 80)
        self.inputs["page_order"] = []
        self.assertEqual(self.snapshot()["page_order"], list(range(8)))
        self.assertEqual(self.snapshot()["pages"][2]["title"], "Page 3")

    def test_invalid_page_order_logs_configuration_error(self):
        for value in (None, "3,1", {"page": "1"}, [None], [{}], [{"page": "9"}],
                      [{"page": 1}], [{"page": "1"}, {"page": "1"}],
                      [{"page": "2", "title": None}], [{"page": "2", "title": 123}]):
            with self.subTest(value=value):
                self.inputs["page_order"] = value
                self.assertTrue(self.variables()["configuration_error"])

    def test_custom_titles_apply_to_every_button_slot(self):
        for page in range(1, 9):
            self.inputs[f"page_{page}_layout"] = "6x3"
            for button in range(1, 19):
                key = f"page_{page}_button_{button}"
                self.inputs[key] = f"light.test_{page}_{button}"
                self.inputs[f"{key}_title"] = f'  Custom "{page}/{button}"  '
        snapshot = self.snapshot()
        for page, row in enumerate(snapshot["pages"], start=1):
            for button, item in enumerate(row["buttons"], start=1):
                self.assertEqual(item["name"], f'Custom "{page}/{button}"')
                self.assertEqual(item["entity"], f"light.test_{page}_{button}")

    def test_blank_titles_restore_default_names_and_ignore_empty_slots(self):
        entity = "light.office"
        self.inputs["page_1_button_1"] = entity
        self.attributes[entity] = {"friendly_name": "Office Lamp"}
        for title, expected in (("Custom Lamp", "Custom Lamp"), ("", "Office Lamp"), ("  ", "Office Lamp")):
            with self.subTest(title=title):
                self.inputs["page_1_button_1_title"] = title
                self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["name"], expected)
        self.attributes[entity] = {}
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["name"], entity)
        self.inputs["page_1_button_1"] = []
        self.inputs["page_1_button_1_title"] = "No entity"
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0],
                         {"entity": "", "name": "", "state": "", "icon": "", "tap_action": "default"})

    def test_custom_title_keeps_existing_label_limit(self):
        self.inputs["page_1_button_1"] = "light.office"
        self.inputs["page_1_button_1_title"] = "x" * 81
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["name"], "x" * 80)

    def test_entity_icons_apply_to_every_slot_and_follow_attribute_changes(self):
        for page in range(1, 9):
            self.inputs[f"page_{page}_layout"] = "6x3"
            for button in range(1, 19):
                entity = f"light.test_{page}_{button}"
                self.inputs[f"page_{page}_button_{button}"] = entity
                self.attributes[entity] = {"icon": "mdi:lightbulb-auto"}
        for page in self.snapshot()["pages"]:
            for button in page["buttons"]:
                self.assertEqual(button["icon"], "mdi:lightbulb-auto")
        self.attributes["light.test_1_1"]["icon"] = "mdi:ceiling-light"
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["icon"], "mdi:ceiling-light")
        del self.attributes["light.test_1_1"]["icon"]
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["icon"], "")

    def test_independent_layouts_icons_and_retained_extra_slots(self):
        self.inputs.update(
            page_1_layout="2x2", page_2_layout="4x2", page_3_layout="6x3",
            page_1_button_5="light.saved", page_2_button_8="switch.eight",
            page_3_button_18="lock.eighteen", page_3_icon="mdi:door-open",
            navigation_layout="6x3",
        )
        snapshot = self.snapshot()
        self.assertEqual(snapshot["version"], 3)
        self.assertEqual(snapshot["navigation_layout"], "6x3")
        self.assertEqual([len(page["buttons"]) for page in snapshot["pages"][:3]], [18, 18, 18])
        self.assertEqual(snapshot["pages"][2]["icon"], "mdi:door-open")
        self.assertEqual(snapshot["pages"][2]["buttons"][17]["entity"], "lock.eighteen")
        variables = self.variables()
        self.assertEqual(variables["ignored_slots"], ["Page 1 button 5"])
        self.assertNotIn("light.saved", variables["watched_entities"])
        self.assertIn("switch.eight", variables["watched_entities"])
        self.assertIn("lock.eighteen", variables["watched_entities"])
        self.inputs["page_1_layout"] = "4x2"
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][4]["entity"], "light.saved")
        self.assertEqual(self.variables()["ignored_slots"], [])

    def test_menu_capacity_guard_prevents_dispatch_but_ignored_slots_do_not_enable_pages(self):
        self.states["sensor.renamed_dashboard_endpoint"] = "esphome.test_set_dashboard_chunk"
        for page in range(1, 6):
            self.inputs[f"page_{page}_button_1"] = f"light.page_{page}"
        variables = self.variables()
        guard = self.blueprint["actions"][-1]["choose"][1]
        self.assertEqual(self.env.from_string(guard["conditions"]).render(**variables), "True")
        self.assertEqual(guard["sequence"][0]["data"]["level"], "error")
        for layout in ("4x2", "6x3"):
            self.inputs["navigation_layout"] = layout
            self.assertEqual(self.variables()["configuration_error"], "")
        self.inputs.update(navigation_layout="2x2", page_5_layout="2x2",
                           page_5_button_1=[], page_5_button_18="light.ignored")
        self.assertEqual(self.variables()["configuration_error"], "")
        warning = self.blueprint["actions"][-1]["default"][0]
        self.assertEqual(self.env.from_string(warning["if"]).render(**self.variables()), "True")
        message = self.env.from_string(warning["then"][0]["data"]["message"]).render(**self.variables())
        self.assertIn("Page 5 button 18", message)

    def test_auto_layout_thresholds_and_numbered_slot_identity(self):
        for count, layout in ((0, "2x2"), (1, "2x2"), (4, "2x2"), (5, "4x2"),
                              (8, "4x2"), (9, "6x3"), (18, "6x3")):
            with self.subTest(count=count):
                for slot in range(1, 19):
                    self.inputs[f"page_1_button_{slot}"] = "light.test" if slot > 18 - count else []
                self.inputs["page_1_button_18_title"] = "Last slot"
                self.inputs["page_1_button_18_tap_action"] = "details"
                page = self.snapshot()["pages"][0]
                self.assertEqual(page["layout"], layout)
                self.assertEqual(sum(bool(button["entity"]) for button in page["buttons"]), count)
                if count:
                    self.assertEqual(page["buttons"][17]["name"], "Last slot")
                    self.assertEqual(page["buttons"][17]["tap_action"], "details")
                    self.assertIn("light.test", self.variables()["watched_entities"])
                self.assertEqual(self.variables()["ignored_slots"], [])
        self.inputs["page_1_compact"] = False
        for slot, layout in ((1, "2x2"), (4, "2x2"), (5, "4x2"), (8, "4x2"),
                             (9, "6x3"), (18, "6x3")):
            for b in range(1, 19):
                self.inputs[f"page_1_button_{b}"] = "light.test" if b == slot else []
            page = self.snapshot()["pages"][0]
            self.assertEqual(page["layout"], layout)
            self.assertIs(page["compact"], False)
            self.assertEqual(page["buttons"][slot - 1]["entity"], "light.test")

    def test_disabled_page_preserves_inputs_and_removes_only_inactive_subscriptions(self):
        self.inputs.update(
            page_1_button_18="light.disabled", page_1_button_18_title="Saved",
            page_1_button_18_tap_action="details", page_1_temperature="sensor.disabled",
            page_1_icon="mdi:home", page_1_enabled=False,
            page_2_button_1="light.shared", wake_sensors=["binary_sensor.wake"],
            light_details=[dict(light="light.disabled", related_1="switch.disabled"),
                           dict(light="light.shared", related_1="switch.shared")],
        )
        original = copy.deepcopy(self.inputs)
        variables = self.variables()
        for entity in ("light.disabled", "sensor.disabled", "switch.disabled"):
            self.assertNotIn(entity, variables["watched_entities"])
        for entity in ("light.shared", "switch.shared", "binary_sensor.wake"):
            self.assertIn(entity, variables["watched_entities"])
        self.assertFalse(any(b["entity"] for b in self.snapshot()["pages"][0]["buttons"]))
        self.assertEqual(self.snapshot()["pages"][0]["temperature"], "")
        self.assertEqual(self.inputs, original)
        self.inputs["page_1_enabled"] = True
        page = self.snapshot()["pages"][0]
        self.assertEqual(page["buttons"][17]["name"], "Saved")
        self.assertEqual(page["buttons"][17]["tap_action"], "details")
        self.assertIn("switch.disabled", self.variables()["watched_entities"])
        self.inputs.update(page_1_enabled=False, page_2_button_2="light.disabled")
        self.assertIn("switch.disabled", self.variables()["watched_entities"])

    def test_tap_actions_are_per_slot_and_invalid_active_choices_fail(self):
        self.inputs.update(page_1_button_1="light.same", page_1_button_2="light.same",
                           page_1_button_2_tap_action="details", page_2_button_1="light.same")
        pages = self.snapshot()["pages"]
        self.assertEqual([b["tap_action"] for b in pages[0]["buttons"][:2]], ["default", "details"])
        self.assertEqual(pages[1]["buttons"][0]["tap_action"], "default")
        for entity, action in (("switch.bad", "details"), ("lock.bad", "details"), ("light.same", "bad")):
            self.inputs.update(page_1_button_2=entity, page_1_button_2_tap_action=action)
            self.assertTrue(self.variables()["configuration_error"])
        self.inputs["page_1_enabled"] = False
        self.assertEqual(self.variables()["configuration_error"], "")
        for key, value in (("page_1_enabled", "false"), ("page_1_compact", 1), ("page_1_layout", "bad")):
            self.inputs.update(page_1_enabled=True, page_1_compact=True, page_1_layout="auto",
                               page_1_button_2=[])
            self.inputs[key] = value
            self.assertTrue(self.variables()["configuration_error"])

    def full_snapshot(self, title="Full control"):
        self.inputs["navigation_layout"] = "4x2"
        self.inputs["page_order"] = [
            {"page": str(page), "title": f"API page {page}"} for page in range(1, 9)
        ]
        for page in range(1, 9):
            self.inputs[f"page_{page}_layout"] = "6x3"
            for button in range(1, 19):
                key = f"page_{page}_button_{button}"
                self.inputs[key] = "light." + f"p{page}_b{button}_".ljust(122, "x")
                self.inputs[f"{key}_title"] = title
                self.states[self.inputs[key]] = "on"
                self.attributes[self.inputs[key]] = {"icon": "mdi:ceiling-light"}
        return self.snapshot()

    def test_homepage_order_titles_and_menu_capacity(self):
        self.assertEqual(self.snapshot()["homepage"],
                         {"enabled": False, "position": 0, "title": "Homepage"})
        self.inputs.update(homepage_enabled=True, page_order=[
            {"page": "3", "title": "Landing"}, {"page": "1", "title": "Office"},
            {"page": "home", "title": " Welcome "},
        ])
        snapshot = self.snapshot()
        self.assertEqual(snapshot["page_order"], [2, 0, 1, 3, 4, 5, 6, 7])
        self.assertEqual(snapshot["homepage"], {"enabled": True, "position": 2, "title": "Welcome"})
        self.assertEqual(snapshot["pages"][0]["title"], "Office")
        self.assertEqual(snapshot["pages"][2]["title"], "Landing")
        self.inputs["page_order"] = [{"page": "2"}]
        self.assertEqual(self.snapshot()["homepage"]["position"], 0)
        self.inputs["page_order"] = [{"page": "home"}, {"page": "home"}]
        self.assertTrue(self.variables()["configuration_error"])
        self.inputs["page_order"] = []
        for page in range(1, 9):
            self.inputs[f"page_{page}_button_1"] = f"light.page_{page}"
        self.inputs["navigation_layout"] = "4x2"
        self.assertIn("6x3", self.variables()["configuration_error"])
        self.inputs["navigation_layout"] = "6x3"
        self.assertEqual(self.variables()["configuration_error"], "")
        self.inputs["homepage_enabled"] = False
        self.inputs["navigation_layout"] = "4x2"
        self.assertEqual(self.variables()["configuration_error"], "")

    def test_localization_presets_and_wake_hold(self):
        self.assertEqual(self.snapshot()["localization"], {"time": "24h", "date": "long"})
        self.inputs.update(time_format="12h", date_format="iso",
                           wake_sensors=["binary_sensor.motion", "binary_sensor.presence"])
        self.assertEqual(self.snapshot()["localization"], {"time": "12h", "date": "iso"})
        self.assertIn("binary_sensor.motion", self.variables()["watched_entities"])
        for motion, presence, expected in (
                ("off", "off", False), ("on", "off", True), ("off", "on", True),
                ("unknown", "unavailable", False)):
            self.states.update({"binary_sensor.motion": motion, "binary_sensor.presence": presence})
            self.assertIs(self.snapshot()["wake_hold"], expected)
        self.inputs["date_format"] = "%x"
        self.assertTrue(self.variables()["configuration_error"])

    def test_wake_requires_real_off_to_on_event_not_attributes_or_refresh(self):
        self.inputs["wake_sensors"] = ["binary_sensor.motion"]
        template = self.env.from_string(self.variables()["fresh_wake"])
        self.assertEqual(template.render(), "False")
        for before, after, entity, trigger_id, expected in (
                ("off", "on", "binary_sensor.motion", "state", True),
                ("on", "on", "binary_sensor.motion", "state", False),
                ("unknown", "on", "binary_sensor.motion", "state", False),
                ("unavailable", "on", "binary_sensor.motion", "state", False),
                (None, "on", "binary_sensor.motion", "state", False),
                ("on", None, "binary_sensor.motion", "state", False),
                ("off", "on", "binary_sensor.other", "state", False),
                ("off", "on", "binary_sensor.motion", "refresh", False)):
            data = SimpleNamespace(entity_id=entity,
                                   old_state=None if before is None else SimpleNamespace(state=before),
                                   new_state=None if after is None else SimpleNamespace(state=after))
            trigger = SimpleNamespace(id=trigger_id, event=SimpleNamespace(data=data))
            self.assertEqual(template.render(trigger=trigger, **self.variables()).strip(), str(expected))
        action = self.blueprint["actions"][0]
        for fresh, endpoint, error, expected in (
                (True, "esphome.office_set_dashboard_chunk", "", "True"),
                (False, "esphome.office_set_dashboard_chunk", "", "False"),
                (True, "", "", "False"),
                (True, "esphome.office_set_dashboard_chunk", "Invalid configuration", "False")):
            self.assertEqual(self.env.from_string(action["if"]).render(
                fresh_wake=fresh, endpoint=endpoint, configuration_error=error), expected)
        self.assertEqual(self.env.from_string(action["then"][0]["action"]).render(
            endpoint="esphome.office_set_dashboard_chunk"), "esphome.office_wake_display")

    def test_full_unicode_snapshot_round_trips_through_bounded_blueprint_chunks(self):
        snapshot = self.full_snapshot("\U0001f600" * 80)
        chunks = self.chunks(snapshot)
        body = "".join(chunk["payload"][1:] for chunk in chunks)
        self.assertGreater(len(body.encode()), 32768)
        self.assertLessEqual(len(body.encode()), 262144)
        self.assertEqual(json.loads(body), snapshot)
        for index, chunk in enumerate(chunks):
            self.assertEqual(int(chunk["index"]), index)
            self.assertEqual(int(chunk["total"]), len(chunks))
            self.assertTrue(chunk["payload"].startswith("~"))
            self.assertTrue(chunk["payload"].isascii())
            self.assertLessEqual(len(chunk["payload"].encode()), 4097)
        # A small snapshot is also framed as text, not a native HA dictionary.
        self.setUp()
        small = self.chunks(self.snapshot())
        self.assertEqual(json.loads("".join(chunk["payload"][1:] for chunk in small)), self.snapshot())
        self.assertTrue(small[0]["payload"].startswith("~{"))

    def test_outlet_icons_follow_state_and_respect_custom_icon(self):
        self.inputs["page_1_button_1"] = "switch.outlet"
        self.attributes["switch.outlet"] = {"device_class": "outlet"}
        for state, icon in (("on", "mdi:power-plug"), ("off", "mdi:power-plug-off")):
            self.states["switch.outlet"] = state
            self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["icon"], icon)
        self.states["switch.outlet"] = "unavailable"
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["icon"], "")
        self.attributes["switch.outlet"]["icon"] = "mdi:desk-lamp"
        self.assertEqual(self.snapshot()["pages"][0]["buttons"][0]["icon"], "mdi:desk-lamp")

    def test_temperature_unknown_and_optional_sensor(self):
        self.inputs["page_1_button_1"] = "light.test"
        self.inputs["page_1_temperature"] = "sensor.temperature"
        self.states["sensor.temperature"] = "unavailable"
        self.assertEqual(self.snapshot()["pages"][0]["temperature"], "UNAVAILABLE")
        self.inputs["page_1_temperature"] = []
        self.assertEqual(self.snapshot()["pages"][0]["temperature"], "")

    def test_endpoint_discovery_and_trigger_filtering(self):
        self.states["sensor.renamed_dashboard_endpoint"] = "esphome.office_panel_set_dashboard_chunk"
        variables = self.variables()
        self.assertEqual(variables["endpoint"], "esphome.office_panel_set_dashboard_chunk")
        action = self.blueprint["actions"][-1]["default"][2]["repeat"]["sequence"][0]["action"]
        self.assertEqual(self.env.from_string(action).render(**variables), variables["endpoint"])
        condition = self.env.from_string(self.blueprint["conditions"][0]["value_template"])
        for endpoint, expected in (
            ("esphome.office_panel_set_dashboard_chunk", "True"),
            ("esphome.other_panel_set_dashboard_chunk", "False"),
        ):
            trigger = SimpleNamespace(id="ready", event=SimpleNamespace(data={"endpoint": endpoint}))
            self.assertEqual(condition.render(trigger=trigger, **variables).strip(), expected)
        for entity, expected in (
            ("sensor.renamed_dashboard_endpoint", "True"),
            ("light.unrelated", "False"),
        ):
            trigger = SimpleNamespace(
                id="state", event=SimpleNamespace(data=SimpleNamespace(entity_id=entity))
            )
            self.assertEqual(condition.render(trigger=trigger, **variables).strip(), expected)

        self.states["sensor.renamed_dashboard_endpoint"] = "esphome.office_panel_set_dashboard"
        self.assertEqual(self.variables()["endpoint"], "")


if __name__ == "__main__":
    unittest.main()
