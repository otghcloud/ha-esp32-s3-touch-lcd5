from .shared import container, label


def build():
    return container("settings", [
        label("settings_title", "Settings", "font_medium", x=24, y=14,
              width=560, height=32, long_mode="DOT"),
        label("settings_overview", "Overview", "font_medium", x=292, y=80),
        label("settings_name_label", "Device name", "font_small", x=292, y=132),
        label("settings_name", "", "font_medium", x=292, y=155, width=484, height=32, long_mode="DOT"),
        label("settings_version_label", "Firmware version", "font_small", x=292, y=214),
        label("settings_version", "", "font_medium", x=292, y=237, width=484, height=32, long_mode="DOT"),
        label("settings_ip_label", "IP address", "font_small", x=292, y=296),
        label("settings_ip", "Unavailable", "font_medium", x=292, y=319, width=484, height=32, long_mode="DOT"),
    ])
