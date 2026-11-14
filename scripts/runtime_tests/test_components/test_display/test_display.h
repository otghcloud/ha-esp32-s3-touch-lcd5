#pragma once
#include <malloc.h>
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "src/widgets/slider/lv_slider_private.h"
class TestDisplay : public esphome::display::DisplayBuffer {
 public:
  void setup() override {
    lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t *event) {
      auto *display = static_cast<TestDisplay *>(lv_event_get_user_data(event));
      display->startup_refresh_requests++;
    }, LV_EVENT_REFR_REQUEST, this);
  }
  float get_setup_priority() const override { return esphome::setup_priority::HARDWARE; }
  void update() override {}
  size_t startup_refresh_requests{0};
  int sample_x{5};
  int sample_y{400};
  esphome::Color sample_pixel;
  size_t allocated_bytes() const { return mallinfo2().uordblks; }
  esphome::display::DisplayType get_display_type() override {
    return esphome::display::DISPLAY_TYPE_COLOR;
  }
 protected:
  int get_width_internal() override { return 800; }
  int get_height_internal() override { return 480; }
  void draw_absolute_pixel_internal(int x, int y, esphome::Color color) override {
    if (x == sample_x && y == sample_y)
      this->sample_pixel = color;
  }
};
