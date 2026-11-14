from .shared import MAX_BUTTONS, MAX_PAGES, container, label


def build():
    widgets, bindings = [], []
    for p in range(MAX_PAGES):
        widgets.append(container(f"page_{p}", [
            label(f"title_{p}", "", "font_medium", x=24, y=14, width=560, height=32, long_mode="DOT"),
            label(f"temperature_{p}", "", "font_small", align="BOTTOM_LEFT", x=24, y=-14,
                  width=280, height=20, long_mode="DOT", text_color=0x111111),
        ]))
        bindings.append(
            f"id(dashboard).bind_page({p}, "
            + f"{{id(page_{p}), id(title_{p}), id(temperature_{p})}});"
        )
    tiles, tile_bindings = [], []
    for b in range(MAX_BUTTONS):
        prefix = f"button_0_{b}"
        tiles.append({"button": {
            "id": prefix, "x": 20 if b % 2 == 0 else 420,
            "y": 65 if b < 2 else 230, "width": 360, "height": 155,
            "bg_color": 0x171F2C, "bg_opa": "90%", "border_color": 0x344154,
            "border_width": 1, "radius": 18, "scrollable": False,
            "widgets": [
                label(f"{prefix}_icon", "\U000F0335", "font_icons",
                      x=22, y=42, width=72, height=72, text_align="CENTER"),
                label(f"{prefix}_name", "", "font_medium", x=112, y=30,
                      width=220, height=32, long_mode="DOT"),
                label(f"{prefix}_state", "UNKNOWN", "font_medium", x=112, y=78,
                      text_color=0xAAB4C5),
                label(f"{prefix}_unavailable", "UNAVAILABLE", "font_small",
                      align="TOP_RIGHT", x=-14, y=10, text_color=0xF2A65A),
            ],
        }})
        tile_bindings.append(
            "{" + ", ".join(f"id({prefix}{suffix})" for suffix in
                             ("", "_name", "_state", "_unavailable", "_icon")) + "}"
        )
    widgets.append(container("dashboard_tiles", tiles, clickable=False))
    bindings.append("id(dashboard).bind_buttons(id(dashboard_tiles), {{"
                    + ", ".join(tile_bindings) + "}});")
    return widgets, bindings
