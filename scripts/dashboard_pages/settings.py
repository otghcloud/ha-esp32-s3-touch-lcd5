from .shared import container, label


def build():
    return container("settings", [
        label("settings_title", "Settings", "font_medium", x=24, y=14,
              width=560, height=32, long_mode="DOT"),
    ])
