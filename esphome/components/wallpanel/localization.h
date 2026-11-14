#pragma once
#include <cstring>
#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
#include "model.h"

namespace wallpanel {
struct Localization {
  bool twelve_hour{false};
  std::string date{"long"};
  const char *date_pattern() const {
    if (date == "weekday_short") return "%A, %d/%m";
    if (date == "dmy") return "%d/%m/%Y";
    if (date == "mdy") return "%m/%d/%Y";
    if (date == "iso") return "%Y-%m-%d";
    if (date == "abbreviated") return "%d %b %Y";
    return "%A, %d %B %Y";
  }
};

inline bool read_localization(JsonVariant value, Localization &out) {
  if (value.is<JsonObject>() && value["time"].is<const char *>() && value["date"].is<const char *>()) {
    const std::string time = value["time"].as<const char *>();
    out.date = value["date"].as<const char *>();
    if ((time == "24h" || time == "12h") &&
        (out.date == "long" || out.date == "weekday_short" || out.date == "dmy" ||
         out.date == "mdy" || out.date == "iso" || out.date == "abbreviated")) {
      out.twelve_hour = time == "12h";
      return true;
    }
  }
  ESP_LOGE("dashboard", "Invalid localization preset; configuration unchanged");
  return false;
}

inline void set_label_text(lv_obj_t *label, const char *text) {
  if (std::strcmp(lv_label_get_text(label), text) != 0)
    lv_label_set_text(label, text);
}
}  // namespace wallpanel
