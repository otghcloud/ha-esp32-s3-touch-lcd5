#pragma once
#include <cmath>
#include "page_shared.h"
#include "esphome/components/json/json_util.h"

namespace wallpanel {
inline bool read_text(JsonVariant value, std::string &out, size_t limit) {
  if (!value.is<const char *>()) return false;
  out = value.as<const char *>();
  return out.size() <= limit;
}
inline bool valid_entity_id(const std::string &entity) {
  const auto dot = entity.find('.');
  if (dot == std::string::npos || dot + 1 == entity.size()) return false;
  const auto domain = entity.substr(0, dot);
  if (domain != "light" && domain != "switch" && domain != "automation" && domain != "lock") return false;
  return std::all_of(entity.begin() + dot + 1, entity.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}
inline void update_appearance(ButtonData &data, const ButtonData *previous = nullptr) {
  if (previous && previous->entity == data.entity) {
    data.display_state = previous->display_state;
    data.display_color = previous->display_color;
    data.display_icon = previous->display_icon;
  }
  if (data.entity.empty()) return;
  if (!data.display_icon || known_state(data) || !data.icon.empty() ||
      data.state == "jammed" || data.state == "locking" || data.state == "unlocking" ||
      data.state == "opening" || data.state == "open")
    data.display_icon = mdi_glyph(data.icon.empty() ? default_icon(data) : data.icon.c_str());
  if (known_state(data)) {
    const bool on = data.state == "on", locked = data.state == "locked";
    data.display_state = data.entity.starts_with("lock.") ? (locked ? "LOCKED" : "UNLOCKED")
                         : data.entity.starts_with("automation.") ? (on ? "ENABLED" : "DISABLED")
                                                                 : (on ? "ON" : "OFF");
    data.display_color = state_color(data);
  }
}
inline const LightData *find_light(const DetailData &details, const std::string &entity) {
  for (const auto &light : details.lights)
    if (light.entity == entity) return &light;
  return nullptr;
}
inline const ButtonData *find_related(const DetailData &details, const std::string &entity) {
  for (const auto &button : details.related)
    if (button.entity == entity) return &button;
  return nullptr;
}
inline bool read_detail_data(JsonObject root, DetailData &out, const std::vector<std::string> &reverse) {
  auto entities = root["detail_entities"];
  auto lights = root["light_details"];
  if (!entities.is<JsonArray>() || entities.size() > MAX_PAGES * MAX_BUTTONS * 2 ||
      !lights.is<JsonArray>() || lights.size() > MAX_PAGES * MAX_BUTTONS)
    return false;
  for (JsonObject item : entities.as<JsonArray>()) {
    ButtonData data;
    if (!read_text(item["entity"], data.entity, 128) || !valid_entity_id(data.entity) ||
        data.entity.starts_with("lock.") || find_related(out, data.entity) ||
        !read_text(item["name"], data.name, 320) || !read_text(item["state"], data.state, 40) ||
        !read_text(item["icon"], data.icon, 128)) return false;
    if (!data.icon.empty() && !mdi_glyph(data.icon)) {
      ESP_LOGW("dashboard", "Unsupported related icon '%s'; using default", data.icon.c_str());
      data.icon.clear();
    }
    data.reverse_active = std::find(reverse.begin(), reverse.end(), data.entity) != reverse.end();
    out.related.push_back(std::move(data));
  }
  for (JsonObject item : lights.as<JsonArray>()) {
    LightData data;
    if (!read_text(item["entity"], data.entity, 128) || !valid_entity_id(data.entity) ||
        !data.entity.starts_with("light.") || find_light(out, data.entity)) return false;
    if (!read_text(item["colour_mode"], data.colour_mode, 24)) return false;
    if (!data.colour_mode.empty() && data.colour_mode != "onoff" && data.colour_mode != "brightness" &&
        data.colour_mode != "color_temp" && data.colour_mode != "hs" && data.colour_mode != "xy" &&
        data.colour_mode != "rgb" && data.colour_mode != "rgbw" && data.colour_mode != "rgbww" &&
        data.colour_mode != "white") return false;
    for (const auto *key : {"known", "dimmable", "colour", "temperature"})
      if (!item[key].is<bool>()) return false;
    data.known = item["known"];
    data.dimmable = item["dimmable"];
    data.colour = item["colour"];
    data.temperature = item["temperature"];
    if (!item["temperature_issue"].is<bool>()) return false;
    data.temperature_issue = item["temperature_issue"];
    for (const auto *key : {"brightness", "kelvin", "min_kelvin", "max_kelvin"})
      if (!item[key].is<int>()) return false;
    data.brightness = item["brightness"];
    data.kelvin = item["kelvin"];
    data.min_kelvin = item["min_kelvin"];
    data.max_kelvin = item["max_kelvin"];
    if (!item["hue"].is<float>() || !item["saturation"].is<float>()) return false;
    data.hue = item["hue"];
    data.saturation = item["saturation"];
    if (data.brightness < -1 || data.brightness > 255 || data.kelvin < -1 || data.kelvin > 100000 ||
        !std::isfinite(data.hue) || !std::isfinite(data.saturation) ||
        (data.hue < 0 && data.hue != -1) || data.hue > 360 ||
        (data.saturation < 0 && data.saturation != -1) || data.saturation > 100 ||
        ((data.hue == -1) != (data.saturation == -1)) ||
        (!data.known && (data.dimmable || data.colour || data.temperature)) ||
        (!data.dimmable && (data.colour || data.temperature)) ||
        (!data.temperature && (data.min_kelvin != 0 || data.max_kelvin != 0)) ||
        (data.temperature && (data.min_kelvin < 1 || data.max_kelvin > 100000 ||
                              data.min_kelvin >= data.max_kelvin))) return false;
    auto related = item["related"];
    if (!related.is<JsonArray>() || related.size() > 2) return false;
    if (!item["related_titles"].is<JsonArray>() || item["related_titles"].size() != related.size()) return false;
    for (size_t i = 0; i < related.size(); i++) {
      if (!read_text(related[i], data.related[i], 128) || !find_related(out, data.related[i]) ||
          data.related[i] == data.entity || (i == 1 && data.related[0] == data.related[1])) return false;
      if (!read_text(item["related_titles"][i], data.related_titles[i], 320)) return false;
    }
    out.lights.push_back(std::move(data));
  }
  return true;
}
}  // namespace wallpanel
