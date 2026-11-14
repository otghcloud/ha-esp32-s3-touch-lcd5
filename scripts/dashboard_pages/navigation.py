from .shared import container, label


def build():
    widget = container("navigation", [
        label("navigation_title", "Navigation", "font_medium",
              x=24, y=14, width=560, height=32, long_mode="DOT"),
    ])
    binding = "id(dashboard).bind_navigation({id(navigation), id(dashboard_menu)});"
    return widget, binding
