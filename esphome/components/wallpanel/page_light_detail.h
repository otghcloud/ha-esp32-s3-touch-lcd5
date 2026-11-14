#pragma once
#include <cmath>
#include "detail_data.h"

namespace wallpanel {
enum class DetailMode { BRIGHTNESS, TEMPERATURE, COLOUR };
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
inline void render_light_detail(const DetailWidgets &widgets, const std::array<ButtonWidgets, MAX_BUTTONS> &tiles,
                                const Fonts &fonts, const ButtonData &primary, const LightData *light,
                                const DetailData &details, DetailMode mode, bool online) {
  lv_label_set_text(widgets.title, primary.name.c_str());
  detail_tile(tiles[0], 24, 326, 90, 86, "Power", default_icon(primary), fonts);
  style_entity_tile(tiles[0], primary, online);
  detail_tile(tiles[1], 24, 434, 90, 44, "Back", "mdi:arrow-left", fonts);
  visible(tiles[1].icon, false);
  place_label(tiles[1].name, 4, 10, 82, 24, fonts.medium, LV_TEXT_ALIGN_CENTER);
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
  for (size_t i = 0; i < 2; i++) {
    const auto *related = light ? find_related(details, light->related[i]) : nullptr;
    visible(tiles[5 + i].button, related != nullptr);
    if (!related) continue;
    detail_tile(tiles[5 + i], 610, 94 + int(i) * 170, 170, 150,
                light->related_titles[i].empty() ? related->name : light->related_titles[i],
                related->icon.empty() ? default_icon(*related) : related->icon.c_str(), fonts);
    style_entity_tile(tiles[5 + i], *related, online);
    visible(tiles[5 + i].state, true);
    place_label(tiles[5 + i].state, 6, 108, 158, 28, fonts.small, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(tiles[5 + i].state, related->display_state);
    visible(tiles[5 + i].unavailable, !online || !known_state(*related));
    place_label(tiles[5 + i].unavailable, 4, 128, 162, 20, fonts.small, LV_TEXT_ALIGN_CENTER);
  }
  const bool enabled = supports_mode(light, mode) && online && known_state(primary);
  visible(widgets.slider, supports_mode(light, mode) && mode != DetailMode::COLOUR);
  visible(widgets.wheel, supports_mode(light, mode) && mode == DetailMode::COLOUR);
  visible(widgets.marker, enabled && mode == DetailMode::COLOUR && light->hue >= 0);
  visible(widgets.value, supports_mode(light, mode));
  lv_obj_set_style_opa(widgets.slider, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
  lv_obj_set_style_opa(widgets.wheel, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
}
inline void render_detail_value(const DetailWidgets &widgets, DetailMode mode, const LightData *light,
                                float first, float second) {
  if (!light || !supports_mode(light, mode)) return;
  char text[64];
  if (mode == DetailMode::COLOUR) {
    if (first < 0) std::snprintf(text, sizeof(text), "Colour unknown");
    else std::snprintf(text, sizeof(text), "%.0f deg / %.0f%%", first, second);
    const float angle = first * float(M_PI) / 180;
    const float radius = std::max(0.0f, second) * 1.195f;
    lv_obj_set_pos(widgets.marker, int(239.5f + std::cos(angle) * radius) - 8,
                    int(251.5f - std::sin(angle) * radius) - 8);
  } else {
    const bool temperature = mode == DetailMode::TEMPERATURE;
    lv_slider_set_range(widgets.slider, temperature ? light->min_kelvin : 0,
                         temperature ? light->max_kelvin : 100);
    lv_slider_set_value(widgets.slider, first < 0 ? (temperature ? light->min_kelvin : 0) : int(first), LV_ANIM_OFF);
    if (first < 0) std::snprintf(text, sizeof(text), "Unknown");
    else std::snprintf(text, sizeof(text), temperature ? "%.0f K" : "%.0f%%", first);
  }
  lv_label_set_text(widgets.value, text);
}
}  // namespace wallpanel
