from .shared import container, label


def build():
    return container("light_detail", [
        label("detail_title", "", "font_medium", x=24, y=14, width=560, height=32, long_mode="DOT"),
        label("detail_status", "", "font_small", x=138, y=64, width=438, height=28, long_mode="DOT"),
        {"slider": {
            "id": "detail_slider", "x": 254, "y": 158, "width": 24, "height": 214,
            "min_value": 0, "max_value": 100, "value": 0, "animated": False,
            "indicator": {"bg_color": 0xF2F4F8},
            "knob": {"bg_color": 0xFFD166, "pad_all": 8, "radius": 6,
                     "shadow_width": 0, "pressed": {"bg_color": 0xFFD166, "pad_all": 8}},
        }},
        label("detail_value", "", "font_medium", x=136, y=98, width=260, height=32, text_align="CENTER"),
        {"image": {"id": "detail_wheel", "src": "light_colour_wheel", "x": 120, "y": 132}},
        {"obj": {
            "id": "detail_marker", "width": 16, "height": 16, "radius": 8,
            "bg_opa": "TRANSP", "border_color": 0xFFFFFF, "border_width": 3,
            "outline_color": 0x111827, "outline_width": 2,
            "pad_all": 0, "clickable": False, "scrollable": False,
        }},
    ])
