from .shared import container, label


def build():
    return container("homepage", [
        label("homepage_title", "Homepage", "font_medium", x=24, y=14,
              width=560, height=32, long_mode="DOT"),
        label("homepage_placeholder", "Homepage", "font_large", align="CENTER"),
    ])
