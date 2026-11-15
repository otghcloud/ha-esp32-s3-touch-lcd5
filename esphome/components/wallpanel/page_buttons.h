#pragma once
#include "page_shared.h"

namespace wallpanel {
inline void render_entity_tile(const ButtonWidgets &button, const ButtonData &data, Layout layout,
                               size_t count, size_t rank, const Fonts &fonts, bool online) {
  position_button(button.button, layout, count, rank);
  visible(button.icon, true);
  const auto profile = grid(layout);
  const bool large = layout == Layout::TWO_BY_TWO;
  const bool compact = layout == Layout::SIX_BY_THREE;
  place_label(button.icon, large ? 22 : (profile.width - 36) / 2, large ? 42 : compact ? 10 : 16,
              large ? 72 : 36, large ? 72 : 36, large ? fonts.icons : fonts.dense, LV_TEXT_ALIGN_CENTER);
  place_label(button.name, large ? 112 : 6, large ? 30 : compact ? 59 : 65,
              profile.width - (large ? 136 : 12), large ? 32 : 22,
              large ? fonts.medium : fonts.small, large ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER);
  place_label(button.state, large ? 112 : 6, large ? 78 : 100,
              profile.width - (large ? 136 : 12), large ? 32 : 22,
              large ? fonts.medium : fonts.small, large ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER);
  place_label(button.unavailable, 8, large ? 8 : 132, profile.width - 16, 20,
              fonts.small, large ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER);
  visible(button.state, !compact);
  lv_label_set_text(button.name, data.name.c_str());
  const bool available = online && known_state(data);
  visible(button.unavailable, (!available || (data.indicator.configured && !data.indicator.available)) && !compact);
  style_entity_tile(button, data, online);
  lv_label_set_text(button.icon, data.display_icon);
  lv_label_set_text(button.state, state_text(data));
}
}  // namespace wallpanel
