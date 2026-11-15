#pragma once
#include "page_navigation.h"
#include "esphome/components/network/util.h"

namespace wallpanel {
#ifdef USE_NETWORK
inline std::string settings_ip_address(bool connected, const esphome::network::IPAddresses &addresses) {
  if (connected) {
    for (const auto &address : addresses) {
      if (!address.is_ip4()) continue;
      char buffer[esphome::network::IP_ADDRESS_BUFFER_SIZE]{};
      const std::string ip = address.str_to(buffer);
      if (!ip.empty() && ip != "0.0.0.0") return ip;
    }
  }
  return "Unavailable";
}
#endif

inline void render_settings(const std::array<ButtonWidgets, MAX_BUTTONS> &buttons, const Fonts &fonts) {
  for (size_t i = 0; i < 2; i++) {
    const auto &button = buttons[i];
    render_menu_tile(button, i == 0 ? "Restart" : "Sleep",
                     i == 0 ? "mdi:restart" : "mdi:power-sleep", Layout::TWO_BY_TWO, 4, i * 2, fonts);
    lv_obj_set_width(button.button, 240);
    place_label(button.icon, 16, 41, 72, 72, fonts.icons, LV_TEXT_ALIGN_CENTER);
    place_label(button.name, 104, 61, 120, 32, fonts.medium, LV_TEXT_ALIGN_LEFT);
  }
}
}  // namespace wallpanel
