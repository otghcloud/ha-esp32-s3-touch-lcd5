import ast
import copy
import json
import math
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
        self.env.filters["to_json"] = json.dumps
        self.env.globals.update(
            states=lambda entity: self.states.get(entity, "unknown"),
            state_attr=lambda entity, attribute: self.attributes.get(entity, {}).get(attribute),
            device_entities=lambda device: self.device_entities if device == "test-panel" else [],
            is_number=is_number,
        )

    def variables(self):
        result = resolve(self.blueprint["variables"], self.inputs)
        result["endpoint"] = self.env.from_string(result["endpoint"]).render(**result).strip()
        result["watched_entities"] = ast.literal_eval(
            self.env.from_string(result["watched_entities"]).render(**result).strip()
        )
        result["ignored_slots"] = ast.literal_eval(
            self.env.from_string(result["ignored_slots"]).render(**result).strip()
        )
        result["configuration_error"] = self.env.from_string(result["configuration_error"]).render(**result).strip()
        return result

    def snapshot(self):
        source = self.blueprint["actions"][0]["default"][1]["variables"]["dashboard_snapshot"]
        return ast.literal_eval(self.env.from_string(source).render(**self.variables()).strip())

    def chunks(self, snapshot):
        repeat = self.blueprint["actions"][0]["default"][2]["repeat"]
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

    def test_sparse_pages_and_buttons_keep_slots_and_escape_names(self):
        self.inputs.update(
            page_2_title='Landing "night"',
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
        self.assertEqual(page["temperature"], "Temperature: 21.2 C")
        self.assertEqual(page["buttons"][2], {
            "entity": "automation.landing", "name": 'Lighting "auto" %', "state": "off", "icon": "",
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

    def test_named_page_order_preserves_slots_and_legacy_titles(self):
        self.inputs.update(page_1_title="Old office", page_3_title="Old landing",
                           page_1_button_1="light.office", page_3_button_1="switch.landing")
        self.assertEqual(self.snapshot()["page_order"], list(range(8)))
        self.inputs["page_order"] = [{"page": "3", "title": " Landing "}, {"page": "1"}]
        snapshot = self.snapshot()
        self.assertEqual(snapshot["page_order"], [2, 0, 1, 3, 4, 5, 6, 7])
        self.assertEqual(snapshot["pages"][2]["title"], "Landing")
        self.assertEqual(snapshot["pages"][0]["title"], "Old office")
        self.assertEqual(snapshot["pages"][0]["buttons"][0]["entity"], "light.office")
        self.assertEqual(snapshot["pages"][2]["buttons"][0]["entity"], "switch.landing")
        self.inputs["page_order"][1]["title"] = "  "
        self.assertEqual(self.snapshot()["pages"][0]["title"], "Page 1")
        self.inputs["page_order"][0]["title"] = "\U0001f600" * 100
        self.assertEqual(self.snapshot()["pages"][2]["title"], "\U0001f600" * 80)
        self.inputs["page_order"] = []
        self.assertEqual(self.snapshot()["page_order"], list(range(8)))
        self.assertEqual(self.snapshot()["pages"][2]["title"], "Old landing")

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
                         {"entity": "", "name": "", "state": ""})

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
        self.assertEqual(snapshot["version"], 2)
        self.assertEqual(snapshot["navigation_layout"], "6x3")
        self.assertEqual([len(page["buttons"]) for page in snapshot["pages"][:3]], [4, 8, 18])
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
        self.states["sensor.renamed_dashboard_endpoint"] = "esphome.test_set_dashboard"
        for page in range(1, 6):
            self.inputs[f"page_{page}_button_1"] = f"light.page_{page}"
        variables = self.variables()
        guard = self.blueprint["actions"][0]["choose"][1]
        self.assertEqual(self.env.from_string(guard["conditions"]).render(**variables), "True")
        self.assertEqual(guard["sequence"][0]["data"]["level"], "error")
        for layout in ("4x2", "6x3"):
            self.inputs["navigation_layout"] = layout
            self.assertEqual(self.variables()["configuration_error"], "")
        self.inputs.update(navigation_layout="2x2", page_5_button_1=[], page_5_button_18="light.ignored")
        self.assertEqual(self.variables()["configuration_error"], "")
        warning = self.blueprint["actions"][0]["default"][0]
        self.assertEqual(self.env.from_string(warning["if"]).render(**self.variables()), "True")
        message = self.env.from_string(warning["then"][0]["data"]["message"]).render(**self.variables())
        self.assertIn("Page 5 button 18", message)

    def full_snapshot(self, title="Full control"):
        self.inputs["navigation_layout"] = "4x2"
        for page in range(1, 9):
            self.inputs[f"page_{page}_layout"] = "6x3"
            self.inputs[f"page_{page}_title"] = f"API page {page}"
            for button in range(1, 19):
                key = f"page_{page}_button_{button}"
                self.inputs[key] = "light." + f"p{page}_b{button}_".ljust(122, "x")
                self.inputs[f"{key}_title"] = title
                self.states[self.inputs[key]] = "on"
                self.attributes[self.inputs[key]] = {"icon": "mdi:ceiling-light"}
        return self.snapshot()

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
            self.assertLessEqual(len(chunk["payload"].encode()), 4097)
        # A small snapshot is also framed as text, not a native HA dictionary.
        self.setUp()
        small = self.chunks(self.snapshot())
        self.assertEqual(len(small), 1)
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
        self.inputs["page_1_temperature"] = "sensor.temperature"
        self.states["sensor.temperature"] = "unavailable"
        self.assertEqual(self.snapshot()["pages"][0]["temperature"], "Temperature: UNAVAILABLE")
        self.inputs["page_1_temperature"] = []
        self.assertEqual(self.snapshot()["pages"][0]["temperature"], "")

    def test_endpoint_discovery_and_trigger_filtering(self):
        self.states["sensor.renamed_dashboard_endpoint"] = "esphome.office_panel_set_dashboard"
        variables = self.variables()
        self.assertEqual(variables["endpoint"], "esphome.office_panel_set_dashboard")
        condition = self.env.from_string(self.blueprint["conditions"][0]["value_template"])
        for endpoint, expected in (
            ("esphome.office_panel_set_dashboard", "True"),
            ("esphome.other_panel_set_dashboard", "False"),
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


if __name__ == "__main__":
    unittest.main()
