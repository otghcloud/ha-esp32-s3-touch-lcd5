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
  using esphome::ProgmemStringRef;
  request.service = off ? ProgmemStringRef::from_lit("light.turn_off") : ProgmemStringRef::from_lit("light.turn_on");
  request.data.init(1);
  auto &entity = request.data.emplace_back();
  entity.key = ProgmemStringRef::from_lit("entity_id");
  entity.value = ProgmemStringRef{esphome::StringRef(primary.entity)};
  if (!off) {
    request.data_template.init(1);
    auto &parameter = request.data_template.emplace_back();
    parameter.key = mode == DetailMode::COLOUR ? ProgmemStringRef::from_lit("hs_color") :
                    mode == DetailMode::TEMPERATURE ? ProgmemStringRef::from_lit("color_temp_kelvin") :
                                                      ProgmemStringRef::from_lit("brightness_pct");
    parameter.value = ProgmemStringRef{esphome::StringRef(value)};
  }
  esphome::api::global_api_server->send_homeassistant_action(request);
  return true;
}
}  // namespace wallpanel
