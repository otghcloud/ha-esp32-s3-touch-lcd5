#pragma once
#include "esphome/core/time.h"
#include "model.h"
#include "localization.h"

namespace wallpanel {
inline void update_clock(esphome::ESPTime now, lv_obj_t *header, lv_obj_t *clock, lv_obj_t *date,
                         const Localization &locale = {}) {
  char time_text[esphome::ESPTime::STRFTIME_BUFFER_SIZE] = "--:--";
  char date_text[esphome::ESPTime::STRFTIME_BUFFER_SIZE] = "Waiting for time";
  if (now.is_valid()) {
    now.strftime_to(time_text, locale.twelve_hour ? "%I:%M %p" : "%H:%M");
    now.strftime_to(date_text, locale.date_pattern());
  }
  set_label_text(header, time_text);
  set_label_text(clock, time_text);
  set_label_text(date, date_text);
}
}  // namespace wallpanel
