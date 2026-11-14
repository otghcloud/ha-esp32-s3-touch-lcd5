#pragma once
#include <malloc.h>
#include "esphome/components/display/display_buffer.h"
class TestDisplay : public esphome::display::DisplayBuffer {
 public:
  void update() override {}
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
