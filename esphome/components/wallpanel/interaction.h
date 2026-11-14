#pragma once
#include <cstdint>
#include <cstdlib>

namespace wallpanel {
enum class TouchAction { NONE, TAP, NEXT, PREVIOUS };

class DisplayIdle {
 public:
  void set_timeout(uint32_t seconds, uint32_t now) {
    timeout_seconds_ = seconds;
    activity(now);
  }
  void activity(uint32_t now) { last_activity_ = now; }
  bool begin_touch(bool awake, uint32_t now) {
    if (!touch_active_)
      wake_only_ = !awake;
    touch_active_ = true;
    activity(now);
    return accepts_touch();
  }
  bool accepts_touch() const { return touch_active_ && !wake_only_; }
  bool end_touch(uint32_t now) {
    const bool accepted = accepts_touch();
    touch_active_ = false;
    activity(now);
    return accepted;
  }
  void cancel_touch() { wake_only_ = true; }
  bool expired(uint32_t now) const { return inactive_for(timeout_seconds_, now); }
  bool inactive_for(uint32_t seconds, uint32_t now) const {
    return seconds != 0 && !touch_active_ && uint32_t(now - last_activity_) >= seconds * 1000;
  }
 private:
  uint32_t timeout_seconds_{0}, last_activity_{0};
  bool touch_active_{false}, wake_only_{false};
};

class TouchGesture {
 public:
  void begin(int id, int x, int y, uint32_t now = 0) {
    began_at_ = now;
    id_ = id;
    start_x_ = x_ = x;
    start_y_ = y_ = y;
    active_ = true;
    cancelled_ = moved_ = false;
  }
  void update(int id, int x, int y) {
    if (!active_)
      return;
    if (id != id_) {
      cancelled_ = true;
      return;
    }
    x_ = x;
    y_ = y;
    moved_ |= std::abs(x_ - start_x_) > 20 || std::abs(y_ - start_y_) > 20;
  }
  TouchAction finish() {
    if (!active_)
      return TouchAction::NONE;
    active_ = false;
    if (cancelled_)
      return TouchAction::NONE;
    const int dx = x_ - start_x_, dy = y_ - start_y_;
    if (std::abs(dx) >= 100 && std::abs(dx) > std::abs(dy))
      return dx < 0 ? TouchAction::NEXT : TouchAction::PREVIOUS;
    return moved_ ? TouchAction::NONE : TouchAction::TAP;
  }
  int x() const { return x_; }
  int y() const { return y_; }
  bool valid() const { return active_ && !cancelled_; }
  bool stationary() const { return valid() && !moved_; }
  bool held(uint32_t now) const { return stationary() && uint32_t(now - began_at_) >= 600; }
 private:
  uint32_t began_at_{0};
  int id_{0}, start_x_{0}, start_y_{0}, x_{0}, y_{0};
  bool active_{false}, cancelled_{false}, moved_{false};
};
}  // namespace wallpanel
