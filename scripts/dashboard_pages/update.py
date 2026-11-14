from .shared import container, label


def build():
    return container("dashboard_updating", [
        label("update_title", "Updating firmware", "font_large", align="CENTER", y=-28),
        label("update_hint", "Please wait. Do not disconnect power.", "font_medium", align="CENTER", y=32),
    ], bg_color=0x111827, bg_opa="COVER", radius=0)
