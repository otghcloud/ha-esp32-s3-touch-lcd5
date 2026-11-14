from .shared import container, label


def build():
    return container("dashboard_screensaver", [
        label("screensaver_clock", "--:--", "font_screensaver",
              x=20, y=145, width=760, height=128, text_align="CENTER"),
        label("screensaver_date", "Waiting for time", "font_large",
              x=20, y=285, width=760, height=50, text_align="CENTER", long_mode="DOT"),
    ], bg_color=0x111827, bg_opa="COVER", bg_image_src="dashboard_background", radius=0)
