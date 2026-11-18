#pragma once
#include "page_light_detail.h"
#include "esphome/components/api/custom_api_device.h"

namespace wallpanel {
inline bool send_light_command(const ButtonData &primary, const LightData *light, DetailMode mode,
                                float first, float second, bool online) {
  if (!online || !known_state(primary) || !primary.entity.starts_with("light.") ||
      !supports_mode(light, mode) || !std::isfinite(first) || !std::isfinite(second) ||
      (mode == DetailMode::BRIGHTNESS && (first < 0 || first > 100)) ||
      (mode == DetailMode::TEMPERATURE && (first < light->min_kelvin || first > light->max_kelvin)) ||
      (mode == DetailMode::COLOUR && (first < 0 || first > 360 || second < 0 || second > 100))) {
    ESP_LOGW("dashboard", "Ignoring unsupported, stale or invalid light command");
    return false;
  }
  const bool off = mode == DetailMode::BRIGHTNESS && first == 0;
  const std::string literal = mode == DetailMode::COLOUR
      ? "[" + std::to_string(first) + "," + std::to_string(second) + "]"
      : std::to_string(int(std::lround(first)));
  const std::string value = "{{ " + literal + " }}";
  esphome::api::HomeassistantActionRequest request;
  // ESPHome 2026.11 wraps these StringRefs in ProgmemStringRef.
  using ActionString = decltype(request.service);
  request.service = ActionString{esphome::StringRef(off ? "light.turn_off" : "light.turn_on")};
  request.data.init(1);
  auto &entity = request.data.emplace_back();
  entity.key = ActionString{esphome::StringRef("entity_id")};
  entity.value = ActionString{esphome::StringRef(primary.entity)};
  if (!off) {
    request.data_template.init(1);
    auto &parameter = request.data_template.emplace_back();
    parameter.key = ActionString{esphome::StringRef(mode == DetailMode::COLOUR ? "hs_color" :
                                    mode == DetailMode::TEMPERATURE ? "color_temp_kelvin" : "brightness_pct")};
    parameter.value = ActionString{esphome::StringRef(value)};
  }
  esphome::api::global_api_server->send_homeassistant_action(request);
  return true;
}
}  // namespace wallpanel
