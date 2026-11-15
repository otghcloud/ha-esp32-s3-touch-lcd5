#pragma once
#include <cmath>
#include "detail_data.h"

namespace wallpanel {
enum class DetailMode { BRIGHTNESS, TEMPERATURE, COLOUR };
inline constexpr int DETAIL_SLIDER_TOUCH_PADDING = 24;
struct DetailWidgets {
  lv_obj_t *container, *title, *status, *slider, *value, *wheel, *marker;
};
inline bool supports_mode(const LightData *light, DetailMode mode) {
  return light && light->known && (mode == DetailMode::BRIGHTNESS ? light->dimmable :
                                  mode == DetailMode::TEMPERATURE ? light->temperature : light->colour);
}
inline void detail_tile(const ButtonWidgets &tile, int x, int y, int width, int height,
                        const std::string &name, const char *icon, const Fonts &fonts) {
  visible(tile.button, true);
  visible(tile.icon, true);
  visible(tile.state, false);
  visible(tile.unavailable, false);
  lv_obj_set_pos(tile.button, x, y);
  lv_obj_set_size(tile.button, width, height);
  lv_obj_set_style_pad_all(tile.button, 0, 0);
  lv_obj_set_style_border_width(tile.button, 1, 0);
  lv_obj_set_style_radius(tile.button, 18, 0);
  lv_obj_set_style_bg_color(tile.button, lv_color_hex(0x171F2C), 0);
  lv_obj_set_style_bg_opa(tile.button, LV_OPA_90, 0);
  lv_obj_set_style_opa(tile.button, LV_OPA_COVER, 0);
  lv_obj_remove_state(tile.button, LV_STATE_DISABLED);
  lv_obj_set_style_text_color(tile.name, lv_color_hex(0xF2F4F8), 0);
  lv_obj_set_style_text_color(tile.icon, lv_color_hex(0xF2F4F8), 0);
  place_label(tile.icon, (width - 36) / 2, 6, 36, 36, fonts.dense, LV_TEXT_ALIGN_CENTER);
  place_label(tile.name, 5, height > 80 ? 60 : 42, width - 10, 22, fonts.small, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(tile.name, name.c_str());
  lv_label_set_text(tile.icon, mdi_glyph(icon));
}
inline void render_detail_back(const ButtonWidgets &tile, const Fonts &fonts) {
  detail_tile(tile, 640, 434, 48, 44, "", "mdi:arrow-left", fonts);
  lv_obj_set_style_bg_opa(tile.button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tile.button, 0, 0);
  lv_obj_set_style_radius(tile.button, 0, 0);
  place_label(tile.icon, 8, 6, 32, 32, fonts.ui, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_style_text_color(tile.icon, lv_color_hex(0x111111), 0);
}
inline void render_detail_related(const std::array<ButtonWidgets, MAX_BUTTONS> &tiles, const Fonts &fonts,
                                  const DetailBinding *binding, const DetailData &details,
                                  bool online, int x, int width) {
  for (size_t i = 0; i < 2; i++) {
    const auto *related = binding ? find_related(details, binding->related[i]) : nullptr;
    const auto &tile = tiles[5 + i];
    visible(tile.button, related != nullptr);
    if (!related) continue;
    detail_tile(tile, x, 94 + int(i) * 170, width, 150,
                binding->related_titles[i].empty() ? related->name : binding->related_titles[i],
                related->icon.empty() ? default_icon(*related) : related->icon.c_str(), fonts);
    style_entity_tile(tile, *related, online);
    visible(tile.state, true);
    place_label(tile.state, 6, 108, width - 12, 28, fonts.small, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(tile.state, state_text(*related));
    visible(tile.unavailable, !online || !known_state(*related));
    place_label(tile.unavailable, 4, 128, width - 8, 20, fonts.small, LV_TEXT_ALIGN_CENTER);
  }
}
inline void render_light_detail(const DetailWidgets &widgets, const std::array<ButtonWidgets, MAX_BUTTONS> &tiles,
                                const Fonts &fonts, const std::string &title, const ButtonData &primary, const LightData *light,
                                const DetailData &details, DetailMode mode, bool online) {
  lv_label_set_text(widgets.title, title.c_str());
  detail_tile(tiles[0], 24, 64, 90, 86, "State", default_icon(primary), fonts);
  style_entity_tile(tiles[0], primary, online, false);
  render_detail_back(tiles[1], fonts);
  for (size_t i = 0; i < 3; i++) {
    const auto candidate = static_cast<DetailMode>(i);
    visible(tiles[2 + i].button, supports_mode(light, candidate));
    if (!supports_mode(light, candidate)) continue;
    detail_tile(tiles[2 + i], 434, 110 + int(i) * 96, 142, 82,
                i == 0 ? "Brightness" : i == 1 ? "Temperature" : "Colour",
                i == 0 ? "mdi:brightness-6" : i == 1 ? "mdi:thermometer" : "mdi:palette", fonts);
    lv_obj_set_style_border_color(tiles[2 + i].button,
                                  lv_color_hex(mode == candidate ? 0xFFD166 : 0x344154), 0);
  }
  render_detail_related(tiles, fonts, light, details, online, 610, 170);
  const bool enabled = supports_mode(light, mode) && online && known_state(primary);
  visible(widgets.slider, supports_mode(light, mode) && mode != DetailMode::COLOUR);
  visible(widgets.wheel, supports_mode(light, mode) && mode == DetailMode::COLOUR);
  visible(widgets.marker, enabled && mode == DetailMode::COLOUR && light->hue >= 0 && light->saturation >= 0);
  visible(widgets.value, supports_mode(light, mode));
  lv_obj_set_style_opa(widgets.slider, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
  lv_obj_set_ext_click_area(widgets.slider, DETAIL_SLIDER_TOUCH_PADDING);
  lv_obj_set_style_opa(widgets.wheel, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
}
inline void render_lock_detail(const DetailWidgets &widgets, const std::array<ButtonWidgets, MAX_BUTTONS> &tiles,
                               const Fonts &fonts, const std::string &title, const ButtonData &primary,
                               const DetailData &details, bool online) {
  lv_label_set_text(widgets.title, title.c_str());
  lv_label_set_text(widgets.status, online ? state_text(primary, false) : "Home Assistant disconnected");
  visible(widgets.status, true);
  for (auto *editor : {widgets.slider, widgets.value, widgets.wheel, widgets.marker}) visible(editor, false);
  for (size_t i = 2; i < 5; i++) visible(tiles[i].button, false);
  const char *action = primary.state == "locked" ? "Unlock" : primary.state == "unlocked" ? "Lock" : "Unavailable";
  detail_tile(tiles[0], 24, 94, 352, 320, action, default_icon(primary), fonts);
  style_entity_tile(tiles[0], primary, online, false);
  place_label(tiles[0].icon, 140, 44, 72, 72, fonts.icons, LV_TEXT_ALIGN_CENTER);
  place_label(tiles[0].name, 10, 155, 332, 40, fonts.medium, LV_TEXT_ALIGN_CENTER);
  place_label(tiles[0].state, 10, 215, 332, 34, fonts.medium, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(tiles[0].state, state_text(primary, false));
  visible(tiles[0].state, true);
  render_detail_back(tiles[1], fonts);
  render_detail_related(tiles, fonts, find_detail(details, primary.entity), details, online, 420, 360);
}
inline void render_detail_value(const DetailWidgets &widgets, DetailMode mode, const LightData *light,
                                float first, float second, bool hide_missing = false) {
  if (!light || !supports_mode(light, mode)) {
    visible(widgets.value, false);
    return;
  }
  const bool missing = first < 0 || (mode == DetailMode::COLOUR && second < 0);
  visible(widgets.value, !missing || !hide_missing);
  lv_obj_update_layout(widgets.container);
  auto *editor = mode == DetailMode::COLOUR ? widgets.wheel : widgets.slider;
  lv_obj_set_x(widgets.value, lv_obj_get_x(editor) +
                              (lv_obj_get_width(editor) - lv_obj_get_width(widgets.value)) / 2);
  char text[64];
  if (mode == DetailMode::COLOUR) {
    if (missing) std::snprintf(text, sizeof(text), "Choose colour");
    else {
      // Match the wheel without LVGL's intermediate 8-bit hue approximation.
      const auto channel = [&](float offset) {
        const float sector = std::fmod(first / 60 + offset, 6);
        return unsigned(std::lround(255 * (1 - second / 100 *
                            std::clamp(std::min(sector, 4 - sector), 0.0f, 1.0f))));
      };
      std::snprintf(text, sizeof(text), "#%02X%02X%02X", channel(5), channel(3), channel(1));
    }
    const float angle = first * float(M_PI) / 180;
    const float radius = std::max(0.0f, second) * 1.195f;
    lv_obj_set_pos(widgets.marker, int(239.5f + std::cos(angle) * radius) - 8,
                    int(251.5f - std::sin(angle) * radius) - 8);
  } else {
    const bool temperature = mode == DetailMode::TEMPERATURE;
    lv_slider_set_range(widgets.slider, temperature ? light->min_kelvin : 0,
                         temperature ? light->max_kelvin : 100);
    lv_slider_set_value(widgets.slider, first < 0 ? (temperature ? light->min_kelvin : 0) : int(first), LV_ANIM_OFF);
    if (first < 0) std::snprintf(text, sizeof(text), temperature ? "Choose temperature" : "Choose brightness");
    else std::snprintf(text, sizeof(text), temperature ? "%.0f K" : "%.0f%%", first);
  }
  lv_label_set_text(widgets.value, text);
}
}  // namespace wallpanel
