#pragma once
#include "page_shared.h"

namespace wallpanel {
inline void render_menu_tile(const ButtonWidgets &button, const std::string &title, const std::string &icon,
                             Layout layout, size_t count, size_t rank, const Fonts &fonts) {
  visible(button.button, true);
  visible(button.icon, true);
  visible(button.state, false);
  visible(button.unavailable, false);
  lv_obj_remove_state(button.button, LV_STATE_DISABLED);
  lv_obj_set_style_opa(button.button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(button.button, lv_color_hex(0x171F2C), 0);
  lv_obj_set_style_bg_opa(button.button, LV_OPA_90, 0);
  lv_obj_set_style_text_color(button.name, lv_color_hex(0xF2F4F8), 0);
  position_button(button.button, layout, count, rank);
  const auto profile = grid(layout);
  const bool large = layout == Layout::TWO_BY_TWO;
  place_label(button.icon, large ? 22 : 8, (profile.height - (large ? 72 : 36)) / 2,
              large ? 72 : 36, large ? 72 : 36, large ? fonts.icons : fonts.dense, LV_TEXT_ALIGN_CENTER);
  place_label(button.name, large ? 112 : 50, (profile.height - (large ? 32 : 22)) / 2,
              profile.width - (large ? 136 : 58), large ? 32 : 22,
              large ? fonts.medium : fonts.small, LV_TEXT_ALIGN_LEFT);
  lv_label_set_text(button.name, title.c_str());
  lv_label_set_text(button.icon, mdi_glyph(icon));
  lv_obj_set_style_text_color(button.icon, lv_color_hex(0xF2F4F8), 0);
}
}  // namespace wallpanel
