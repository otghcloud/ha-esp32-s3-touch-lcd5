#pragma once
#include <algorithm>
#include "model.h"
#include "mdi_icons.h"

namespace wallpanel {
inline void visible(lv_obj_t *widget, bool show) {
  lv_obj_set_hidden(widget, !show);
}
inline void position_button(lv_obj_t *button, Layout layout, size_t count, size_t rank) {
  const auto profile = grid(layout);
  const int row = rank / profile.columns;
  const int in_row = std::min(int(count) - row * profile.columns, profile.columns);
  const int left = (800 - in_row * profile.width - (in_row - 1) * profile.gap) / 2;
  lv_obj_set_size(button, profile.width, profile.height);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_radius(button, 18, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0x344154), 0);
  lv_obj_set_pos(button, left + int(rank % profile.columns) * (profile.width + profile.gap),
                 65 + row * (profile.height + 10));
}
inline void place_label(lv_obj_t *label, int x, int y, int width, int height,
                        const lv_font_t *font, lv_text_align_t align) {
  lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, width, height);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_align(label, align, 0);
}
inline bool known_state(const ButtonData &button) {
  if (button.entity.compare(0, 5, "lock.") == 0)
    return button.state == "locked" || button.state == "unlocked";
  return button.state == "on" || button.state == "off";
}
inline bool supports_details(const ButtonData &button) {
  return button.entity.starts_with("light.") || button.entity.starts_with("lock.");
}
inline const char *state_text(const ButtonData &button, bool indicator = true) {
  if (indicator && button.indicator.configured)
    return button.indicator.available ? button.indicator.state.c_str() : "UNAVAILABLE";
  return button.display_state;
}
inline uint32_t state_color(const ButtonData &button, bool online = true, bool indicator = true) {
  if (indicator && button.indicator.configured) {
    if (!online || !button.indicator.available) return 0xAAB4C5;
    return button.indicator.active ? 0xFFD166 : 0xAAB4C5;
  }
  if (!online || button.state == "unknown" || button.state == "unavailable")
    return 0xAAB4C5;
  if (button.entity.compare(0, 5, "lock.") == 0) {
    if (button.state == "locked")
      return 0x68D391;
    if (button.state == "unlocked" || button.state == "open" || button.state == "jammed")
      return 0xF87171;
    return 0xFFD166;
  }
  return button.state == "on" ? 0xFFD166 : 0xAAB4C5;
}
inline bool active_state(const ButtonData &button, bool online = true, bool indicator = true) {
  const bool overridden = indicator && button.indicator.configured;
  if (!online || (overridden ? !button.indicator.available : !known_state(button)))
    return false;
  const bool active = overridden ? button.indicator.active : button.entity.compare(0, 5, "lock.") == 0
                          ? button.state == "unlocked" : button.state == "on";
  return active != button.reverse_active;
}
inline void style_entity_tile(const ButtonWidgets &widgets, const ButtonData &data, bool online,
                              bool indicator = true) {
  const bool active = active_state(data, online, indicator);
  const bool available = online && known_state(data);
  lv_obj_set_style_bg_color(widgets.button, lv_color_hex(active ? 0xFFFFFF : 0x171F2C), 0);
  lv_obj_set_style_bg_opa(widgets.button, active ? LV_OPA_COVER : LV_OPA_90, 0);
  lv_obj_set_style_text_color(widgets.name, lv_color_hex(active ? 0x111827 : 0xF2F4F8), 0);
  uint32_t colour = state_color(data, online, indicator);
  if (active) {
    if (colour == 0x68D391) colour = 0x176D3F;
    else if (colour == 0xF87171) colour = 0xA12B2B;
    else if (colour == 0xFFD166) colour = 0x956000;
    else colour = 0x445166;
  }
  lv_obj_set_style_text_color(widgets.icon, lv_color_hex(colour), 0);
  lv_obj_set_style_text_color(widgets.state,
      lv_color_hex(!online ? 0xAAB4C5 : active || (indicator && data.indicator.configured)
                   ? colour : data.display_color), 0);
  lv_obj_set_style_opa(widgets.button, available ? LV_OPA_COVER : LV_OPA_50, 0);
  if (available) lv_obj_remove_state(widgets.button, LV_STATE_DISABLED);
  else lv_obj_add_state(widgets.button, LV_STATE_DISABLED);
}
inline const char *default_icon(const ButtonData &button) {
  const auto domain = button.entity.substr(0, button.entity.find('.'));
  if (domain == "light")
    return button.state == "off" ? "mdi:lightbulb-off" : "mdi:lightbulb";
  if (domain == "switch")
    return button.state == "off" ? "mdi:toggle-switch-variant-off" : "mdi:toggle-switch-variant";
  if (domain == "automation")
    return button.state == "off" ? "mdi:robot-off" : "mdi:robot";
  if (domain == "script")
    return "mdi:script-text-play";
  if (button.state == "jammed")
    return "mdi:lock-alert";
  if (button.state == "locking" || button.state == "unlocking" || button.state == "opening")
    return "mdi:lock-clock";
  return button.state == "unlocked" || button.state == "open" ? "mdi:lock-open-variant" : "mdi:lock";
}
}  // namespace wallpanel
