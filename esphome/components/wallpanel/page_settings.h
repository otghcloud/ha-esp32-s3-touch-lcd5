#pragma once
#include "page_navigation.h"

namespace wallpanel {
inline void render_settings(const std::array<ButtonWidgets, MAX_BUTTONS> &buttons, const Fonts &fonts) {
  render_menu_tile(buttons[0], "Restart", "mdi:restart", Layout::TWO_BY_TWO, 2, 0, fonts);
  render_menu_tile(buttons[1], "Sleep", "mdi:power-sleep", Layout::TWO_BY_TWO, 2, 1, fonts);
}
}  // namespace wallpanel
