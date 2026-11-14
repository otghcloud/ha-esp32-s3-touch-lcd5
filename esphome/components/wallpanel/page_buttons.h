#pragma once
#include "page_shared.h"

namespace wallpanel {
inline void render_entity_tile(const ButtonWidgets &button, const ButtonData &data, Layout layout,
                               size_t count, size_t rank, const Fonts &fonts, bool online) {
  position_button(button.button, layout, count, rank);
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
  visible(button.unavailable, !available && !compact);
  lv_obj_set_style_opa(button.button, available ? LV_OPA_COVER : LV_OPA_50, 0);
  lv_obj_set_style_text_color(button.icon, lv_color_hex(state_color(data, online)), 0);
  if (available)
    lv_obj_remove_state(button.button, LV_STATE_DISABLED);
  else
    lv_obj_add_state(button.button, LV_STATE_DISABLED);
  lv_label_set_text(button.icon, data.display_icon);
  lv_label_set_text(button.state, data.display_state);
  lv_obj_set_style_text_color(button.state, lv_color_hex(data.display_color), 0);
}
}  // namespace wallpanel
