#pragma once
#include "esphome/components/touchscreen/touchscreen.h"

class TestTouchscreen : public esphome::touchscreen::Touchscreen {
 protected:
  void update_touches() override { this->skip_update_ = true; }
};
