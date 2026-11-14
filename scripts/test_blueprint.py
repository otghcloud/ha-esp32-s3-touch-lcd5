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
        return result

    def snapshot(self):
        source = self.blueprint["actions"][0]["default"][0]["data"]["payload"]
        return json.loads(self.env.from_string(source).render(**self.variables()))

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
        self.assertEqual(len(snapshot["pages"]), 4)
        self.assertEqual(snapshot["pages"][0]["buttons"][0]["entity"], "")
        page = snapshot["pages"][1]
        self.assertEqual(page["title"], 'Landing "night"')
        self.assertEqual(page["temperature"], "Temperature: 21.2 C")
        self.assertEqual(page["buttons"][2], {
            "entity": "automation.landing", "name": 'Lighting "auto" %', "state": "off",
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
