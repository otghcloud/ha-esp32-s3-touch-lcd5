#pragma once
#include <array>
#include <string>
#include <vector>
#include "esphome/components/lvgl/lvgl_esphome.h"

namespace wallpanel {
inline constexpr size_t MAX_PAGES = 8;
inline constexpr size_t WAKE_NAVIGATION = MAX_PAGES + 1;
inline constexpr size_t HOME_SLOT = MAX_PAGES;
inline constexpr size_t WAKE_HOMEPAGE = WAKE_NAVIGATION + 1;
inline constexpr size_t MAX_BUTTONS = 18;
inline constexpr size_t MAX_PAYLOAD_BYTES = 262144;
inline constexpr size_t CHUNK_BYTES = 4096;

enum class Layout { TWO_BY_TWO, FOUR_BY_TWO, SIX_BY_THREE };
enum class View { BUTTONS, NAVIGATION, HOMEPAGE, SETTINGS, LIGHT_DETAIL };
enum class TapAction { DEFAULT, DETAILS };

struct Grid {
  int columns, rows, width, height, gap;
  size_t capacity() const { return columns * rows; }
};

inline Grid grid(Layout layout) {
  if (layout == Layout::FOUR_BY_TWO)
    return {4, 2, 175, 155, 20};
  if (layout == Layout::SIX_BY_THREE)
    return {6, 3, 118, 100, 10};
  return {2, 2, 360, 155, 40};
}

struct ButtonWidgets {
  lv_obj_t *button, *name, *state, *unavailable, *icon;
};
struct PageWidgets {
  lv_obj_t *container, *title, *temperature, *icon, *thermometer;
};
struct NavigationWidgets {
  lv_obj_t *container, *menu;
};
struct Fonts {
  const lv_font_t *icons, *dense, *medium, *small, *ui;
};
struct ButtonData {
  std::string entity, name, state, icon;
  bool reverse_active{false};
  TapAction tap_action{TapAction::DEFAULT};
  // These pointers reference static labels/glyphs, never shared widget text.
  const char *display_state{"UNKNOWN"};
  const char *display_icon{nullptr};
  uint32_t display_color{0xAAB4C5};
  bool operator==(const ButtonData &) const = default;
};
struct LightData {
  std::string entity;
  std::string colour_mode;
  bool known{false}, dimmable{false}, colour{false}, temperature{false};
  bool temperature_issue{false};
  int brightness{-1}, kelvin{-1}, min_kelvin{0}, max_kelvin{0};
  float hue{-1}, saturation{-1};
  std::array<std::string, 2> related{};
  std::array<std::string, 2> related_titles{};
  bool operator==(const LightData &) const = default;
};
struct DetailData {
  std::vector<LightData> lights;
  std::vector<ButtonData> related;
  bool operator==(const DetailData &) const = default;
};
struct PageData {
  std::string title, temperature, icon;
  Layout layout{Layout::TWO_BY_TWO};
  std::array<ButtonData, MAX_BUTTONS> buttons;
  bool enabled{false};
  bool compact{true};
  bool operator==(const PageData &) const = default;
};
struct Homepage {
  bool enabled{false};
  size_t position{0};
  std::string title{"Homepage"};
};
}  // namespace wallpanel
