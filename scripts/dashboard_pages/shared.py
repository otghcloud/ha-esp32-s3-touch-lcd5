MAX_PAGES = 8
MAX_BUTTONS = 18


def label(widget_id, text, font, **properties):
    return {"label": {"id": widget_id, "text": text, "text_font": font,
                      "text_color": 0xF2F4F8, **properties}}


def container(widget_id, children, **properties):
    return {"obj": {"id": widget_id, "x": 0, "y": 0, "width": 800, "height": 480,
                    "pad_all": 0, "border_width": 0, "bg_opa": "TRANSP",
                    "scrollable": False, "hidden": True, "widgets": children, **properties}}


def chrome():
    return [{"obj": {
        "id": name, "x": 0, "y": y, "width": 800, "height": height,
        "bg_color": color, "bg_opa": "COVER", "radius": 0,
        "pad_all": 0, "border_width": 0, "clickable": False, "scrollable": False,
    }} for name, y, height, color in (
        ("dashboard_header", 0, 56, 0x000000), ("dashboard_footer", 432, 48, 0x999999)
    )]


def dots():
    return [{"obj": {
        "id": f"dashboard_dot_{d}", "x": 364 + d * 20, "align": "BOTTOM_LEFT",
        "y": -18, "width": 12, "height": 12, "radius": 6,
        "pad_all": 0, "border_width": 2, "border_color": 0x111111,
        "bg_color": 0x111111, "bg_opa": "TRANSP",
        "clickable": False, "scrollable": False, "hidden": True,
    }} for d in range(MAX_PAGES + 1)]
