#!/usr/bin/env python3
"""Generate ESPHome dashboard and Home Assistant entity YAML from config.yaml."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

import yaml


ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = ROOT / "config.yaml"
DASHBOARD_PATH = ROOT / "esphome" / "dashboard.yaml"
ENTITIES_PATH = ROOT / "esphome" / "entities.yaml"

ID_PATTERN = re.compile(r"^[a-z][a-z0-9_]*$")
ENTITY_PATTERN = re.compile(r"^[a-z_]+\.[a-z0-9_]+$")
ENTITY_DOMAINS = {
    "light": "light",
    "automation": "automation",
    "lock": "lock",
}
TYPE_LABELS = {
    "light": ("ON", "OFF"),
    "automation": ("ENABLED", "DISABLED"),
    "lock": ("LOCKED", "UNLOCKED"),
}
GENERATED_HEADER = (
    "# Generated from ../config.yaml by scripts/generate_dashboard.py. "
    "Do not edit directly.\n"
)


class HexColor(int):
    pass


class ESPHomeDumper(yaml.SafeDumper):
    pass


def represent_hex_color(dumper: yaml.Dumper, value: HexColor) -> yaml.Node:
    return dumper.represent_scalar("tag:yaml.org,2002:int", f"0x{value:06X}")


ESPHomeDumper.add_representer(HexColor, represent_hex_color)


def validate_id(value: Any, context: str) -> str:
    if not isinstance(value, str) or not ID_PATTERN.fullmatch(value):
        raise ValueError(f"{context} must be a lowercase snake_case ID")
    return value


def validate_entity(value: Any, context: str, expected_domain: str | None = None) -> str:
    if not isinstance(value, str) or not ENTITY_PATTERN.fullmatch(value):
        raise ValueError(f"{context} must be a Home Assistant entity ID")
    if expected_domain and value.split(".", 1)[0] != expected_domain:
        raise ValueError(f"{context} must use the {expected_domain}. domain")
    return value


def validate_config(config: Any) -> list[dict[str, Any]]:
    if not isinstance(config, dict) or not isinstance(config.get("pages"), list):
        raise ValueError("config.yaml must contain a pages list")
    if not config["pages"]:
        raise ValueError("config.yaml must define at least one page")

    page_ids: set[str] = set()
    control_ids: set[str] = set()
    for page_index, page in enumerate(config["pages"]):
        context = f"pages[{page_index}]"
        if not isinstance(page, dict):
            raise ValueError(f"{context} must be a mapping")
        page_id = validate_id(page.get("id"), f"{context}.id")
        if page_id in page_ids:
            raise ValueError(f"duplicate page ID: {page_id}")
        page_ids.add(page_id)
        if not isinstance(page.get("title"), str) or not page["title"].strip():
            raise ValueError(f"{context}.title must be a non-empty string")

        controls = page.get("controls")
        if not isinstance(controls, list) or not 1 <= len(controls) <= 4:
            raise ValueError(f"{context}.controls must contain between 1 and 4 controls")
        for control_index, control in enumerate(controls):
            control_context = f"{context}.controls[{control_index}]"
            if not isinstance(control, dict):
                raise ValueError(f"{control_context} must be a mapping")
            control_id = validate_id(control.get("id"), f"{control_context}.id")
            if control_id in control_ids:
                raise ValueError(f"duplicate control ID: {control_id}")
            control_ids.add(control_id)

            control_type = control.get("type")
            if not isinstance(control_type, str) or control_type not in ENTITY_DOMAINS:
                raise ValueError(
                    f"{control_context}.type must be one of "
                    f"{', '.join(ENTITY_DOMAINS)}"
                )
            validate_entity(
                control.get("entity_id"),
                f"{control_context}.entity_id",
                ENTITY_DOMAINS[control_type],
            )
            if not isinstance(control.get("label"), str) or not control["label"].strip():
                raise ValueError(f"{control_context}.label must be a non-empty string")
            icon = control.get("icon")
            if not isinstance(icon, str) or not re.fullmatch(r"mdi:[a-z0-9-]+", icon):
                raise ValueError(f"{control_context}.icon must use the mdi:<icon-name> form")

        temperature = page.get("temperature")
        if temperature is not None:
            if not isinstance(temperature, dict):
                raise ValueError(f"{context}.temperature must be a mapping")
            validate_entity(
                temperature.get("entity_id"),
                f"{context}.temperature.entity_id",
                "sensor",
            )
            temperature_label = temperature.get("label")
            if not isinstance(temperature_label, str) or not re.search(
                r"%[-+0-9.]*f", temperature_label
            ):
                raise ValueError(f"{context}.temperature.label must contain a float format")
    return config["pages"]


def icon_id(icon: str) -> str:
    return "icon_" + re.sub(r"[^a-z0-9]+", "_", icon.removeprefix("mdi:")).strip("_")


def color(value: int) -> HexColor:
    return HexColor(value)


def make_button(control: dict[str, Any], index: int, control_count: int) -> dict[str, Any]:
    column = index % 2
    row = index // 2
    if control_count == 1:
        x = 220
    elif control_count == 3 and row == 1:
        x = 220
    else:
        x = 20 if column == 0 else 420
    y = 65 if row == 0 else 230
    control_id = control["id"]
    control_type = control["type"]
    state_id = f"ha_{control_id}_state"
    on_label, off_label = TYPE_LABELS[control_type]

    if control_type == "lock":
        on_click = [
            {
                "if": {
                    "condition": {"lambda": f'return id({state_id}).state == "locked";'},
                    "then": [
                        {
                            "homeassistant.action": {
                                "action": "lock.unlock",
                                "data": {"entity_id": control["entity_id"]},
                            }
                        }
                    ],
                    "else": [
                        {
                            "homeassistant.action": {
                                "action": "lock.lock",
                                "data": {"entity_id": control["entity_id"]},
                            }
                        }
                    ],
                }
            }
        ]
    else:
        on_click = [
            {
                "homeassistant.action": {
                    "action": f"{control_type}.toggle",
                    "data": {"entity_id": control["entity_id"]},
                }
            }
        ]

    return {
        "button": {
            "x": x,
            "y": y,
            "width": 360,
            "height": 155,
            "bg_color": color(0x171F2C),
            "bg_opa": "90%",
            "border_color": color(0x344154),
            "border_width": 1,
            "radius": 18,
            "on_click": on_click,
            "widgets": [
                {
                    "image": {
                        "src": icon_id(control["icon"]),
                        "x": 22,
                        "y": 42,
                        "image_recolor": color(0xF2C66D),
                        "image_recolor_opa": "COVER",
                    }
                },
                {
                    "label": {
                        "text": control["label"],
                        "text_font": "font_medium",
                        "text_color": color(0xF2F4F8),
                        "x": 112,
                        "y": 30,
                    }
                },
                {
                    "label": {
                        "id": f"lbl_{control_id}_state",
                        "text": "UNKNOWN",
                        "text_font": "font_medium",
                        "text_color": color(0xAAB4C5),
                        "x": 112,
                        "y": 78,
                    }
                },
                {
                    "label": {
                        "id": f"lbl_{control_id}_unavailable",
                        "text": "UNAVAILABLE",
                        "text_font": "font_small",
                        "text_color": color(0xF2A65A),
                        "align": "TOP_RIGHT",
                        "x": -14,
                        "y": 10,
                        "hidden": True,
                    }
                },
            ],
        }
    }


def make_dashboard(pages: list[dict[str, Any]]) -> dict[str, Any]:
    icons = dict.fromkeys(
        control["icon"] for page in pages for control in page["controls"]
    )
    image_configs = [
        {
            "platform": "file",
            "file": icon,
            "id": icon_id(icon),
            "type": "GRAYSCALE",
            "transparency": "alpha_channel",
            "resize": "72x72",
        }
        for icon in icons
    ]

    page_configs = []
    for page_index, page in enumerate(pages):
        widgets: list[dict[str, Any]] = [
            {
                "label": {
                    "id": f"page_title_{page['id']}",
                    "text": page["title"],
                    "text_font": "font_medium",
                    "text_color": color(0xF2F4F8),
                    "align": "TOP_LEFT",
                    "x": 24,
                    "y": 10,
                }
            }
        ]
        widgets.extend(
            make_button(control, index, len(page["controls"]))
            for index, control in enumerate(page["controls"])
        )
        if page.get("temperature"):
            widgets.append(
                {
                    "label": {
                        "id": f"lbl_temperature_{page['id']}",
                        "text": "Temperature: --.- C",
                        "text_font": "font_small",
                        "text_color": color(0xB9C2D0),
                        "align": "BOTTOM_LEFT",
                        "x": 24,
                        "y": -14,
                    }
                }
            )
        if len(pages) > 1:
            widgets.append(
                {
                    "obj": {
                        "id": f"page_dots_{page['id']}",
                        "align": "BOTTOM_MID",
                        "y": -14,
                        "width": (len(pages) - 1) * 20 + 12,
                        "height": 12,
                        "pad_all": 0,
                        "border_width": 0,
                        "bg_opa": "TRANSP",
                        "clickable": False,
                        "scrollable": False,
                        "widgets": [
                            {
                                "obj": {
                                    "x": dot_index * 20,
                                    "y": 0,
                                    "width": 12,
                                    "height": 12,
                                    "radius": 6,
                                    "pad_all": 0,
                                    "border_width": 2,
                                    "border_color": color(0xB9C2D0),
                                    "bg_color": color(0xB9C2D0),
                                    "bg_opa": (
                                        "COVER" if dot_index == page_index else "TRANSP"
                                    ),
                                    "clickable": False,
                                    "scrollable": False,
                                }
                            }
                            for dot_index in range(len(pages))
                        ],
                    }
                }
            )
        page_configs.append(
            {
                "id": page["id"],
                "bg_color": color(0x111827),
                "bg_opa": "COVER",
                "widgets": widgets,
            }
        )

    return {
        "font": [
            {"file": "gfonts://Roboto", "id": "font_large", "size": 38},
            {"file": "gfonts://Roboto", "id": "font_medium", "size": 24},
            {"file": "gfonts://Roboto", "id": "font_small", "size": 16},
        ],
        "image": image_configs,
        "lvgl": {
            "page_wrap": False,
            "top_layer": {
                "widgets": [
                    {
                        "label": {
                            "id": "lbl_time",
                            "text": "--:--",
                            "text_font": "font_large",
                            "text_color": color(0xF2F4F8),
                            "align": "TOP_RIGHT",
                            "x": -24,
                            "y": 4,
                        }
                    }
                ]
            },
            "pages": page_configs,
        },
    }


def state_update(control: dict[str, Any]) -> dict[str, Any]:
    control_id = control["id"]
    control_type = control["type"]
    state_id = f"ha_{control_id}_state"
    unavailable_id = f"lbl_{control_id}_unavailable"
    label_id = f"lbl_{control_id}_state"
    on_label, off_label = TYPE_LABELS[control_type]
    on_color, off_color = (
        (0xFFD166, 0xAAB4C5)
        if control_type in ("light", "automation")
        else (0x68D391, 0xF87171)
    )
    return {
        "platform": "homeassistant",
        "id": state_id,
        "entity_id": control["entity_id"],
        "on_value": {
            "then": [
                {
                    "if": {
                        "condition": {
                            "lambda": 'return x == "unknown" || x == "unavailable";'
                        },
                        "then": [{"lvgl.widget.show": {"id": unavailable_id}}],
                        "else": [
                            {"lvgl.widget.hide": {"id": unavailable_id}},
                            {
                                "if": {
                                    "condition": {
                                        "lambda": (
                                            f'return x == "{"locked" if control_type == "lock" else "on"}";'
                                        )
                                    },
                                    "then": [
                                        {
                                            "lvgl.label.update": {
                                                "id": label_id,
                                                "text": on_label,
                                                "text_color": color(on_color),
                                            }
                                        }
                                    ],
                                    "else": [
                                        {
                                            "lvgl.label.update": {
                                                "id": label_id,
                                                "text": off_label,
                                                "text_color": color(off_color),
                                            }
                                        }
                                    ],
                                }
                            },
                        ],
                    }
                }
            ]
        },
    }


def make_entities(pages: list[dict[str, Any]]) -> dict[str, Any]:
    sensors = []
    text_sensors = []
    for page in pages:
        temperature = page.get("temperature")
        if temperature:
            label_id = f"lbl_temperature_{page['id']}"
            sensors.append(
                {
                    "platform": "homeassistant",
                    "id": f"ha_temperature_{page['id']}",
                    "entity_id": temperature["entity_id"],
                    "on_value": {
                        "then": [
                            {
                                "lvgl.label.update": {
                                    "id": label_id,
                                    "text": {
                                        "format": temperature["label"],
                                        "args": ["x"],
                                    },
                                }
                            }
                        ]
                    },
                }
            )
        text_sensors.extend(state_update(control) for control in page["controls"])
    result: dict[str, Any] = {}
    if sensors:
        result["sensor"] = sensors
    if text_sensors:
        result["text_sensor"] = text_sensors
    return result


def render(data: dict[str, Any]) -> str:
    return GENERATED_HEADER + yaml.dump(
        data,
        Dumper=ESPHomeDumper,
        sort_keys=False,
        allow_unicode=True,
        width=100,
    )


def generated_files() -> dict[Path, str]:
    with CONFIG_PATH.open(encoding="utf-8") as config_file:
        config = yaml.safe_load(config_file)
    pages = validate_config(config)
    return {
        DASHBOARD_PATH: render(make_dashboard(pages)),
        ENTITIES_PATH: render(make_entities(pages)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if generated ESPHome YAML is out of date instead of writing it",
    )
    args = parser.parse_args()

    try:
        outputs = generated_files()
    except (OSError, yaml.YAMLError, ValueError) as error:
        parser.error(str(error))

    stale = []
    for path, content in outputs.items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                stale.append(path.relative_to(ROOT))
        else:
            path.write_text(content, encoding="utf-8")

    if stale:
        parser.error(
            "generated files are out of date: "
            + ", ".join(str(path) for path in stale)
            + "; run python3 scripts/generate_dashboard.py"
        )
    if not args.check:
        for path in outputs:
            print(f"Generated {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
