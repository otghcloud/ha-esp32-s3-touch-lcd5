#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include "esphome/components/api/custom_api_device.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "mdi_icons.h"

namespace wallpanel {

struct ButtonWidgets {
  lv_obj_t *button;
  lv_obj_t *name;
  lv_obj_t *state;
  lv_obj_t *unavailable;
  lv_obj_t *icon;
};

struct PageWidgets {
  lv_obj_t *container;
  lv_obj_t *title;
  lv_obj_t *temperature;
  std::array<lv_obj_t *, 4> dots;
  std::array<ButtonWidgets, 4> buttons;
};

struct NavigationWidgets {
  lv_obj_t *container;
  lv_obj_t *menu;
  std::array<lv_obj_t *, 4> buttons;
  std::array<lv_obj_t *, 4> titles;
};

struct ButtonData {
  std::string entity;
  std::string name;
  std::string state;
  std::string icon;
};

struct PageData {
  std::string title;
  std::string temperature;
  std::array<ButtonData, 4> buttons;
  bool enabled{false};
};

enum class TouchAction { NONE, TAP, NEXT, PREVIOUS };

class DisplayIdle {
 public:
  void set_timeout(uint32_t seconds, uint32_t now) {
    timeout_ms_ = seconds * 1000;
    activity(now);
  }

  void activity(uint32_t now) { last_activity_ = now; }

  bool begin_touch(bool backlight_on, uint32_t now) {
    if (!touch_active_)
      wake_only_ = !backlight_on;
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

  bool expired(uint32_t now) const {
    return timeout_ms_ != 0 && !touch_active_ && uint32_t(now - last_activity_) >= timeout_ms_;
  }

 private:
  uint32_t timeout_ms_{0};
  uint32_t last_activity_{0};
  bool touch_active_{false};
  bool wake_only_{false};
};

class TouchGesture {
 public:
  void begin(int id, int x, int y) {
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
    const int dx = x_ - start_x_;
    const int dy = y_ - start_y_;
    if (std::abs(dx) >= 100 && std::abs(dx) > std::abs(dy))
      return dx < 0 ? TouchAction::NEXT : TouchAction::PREVIOUS;
    return moved_ ? TouchAction::NONE : TouchAction::TAP;
  }

  int x() const { return x_; }
  int y() const { return y_; }

 private:
  int id_{0};
  int start_x_{0};
  int start_y_{0};
  int x_{0};
  int y_{0};
  bool active_{false};
  bool cancelled_{false};
  bool moved_{false};
};

class Dashboard : public esphome::Component, public esphome::api::CustomAPIDevice {
 public:
  void bind_page(size_t index, const PageWidgets &widgets) { widgets_[index] = widgets; }
  void bind_navigation(const NavigationWidgets &widgets) { navigation_ = widgets; }

  void initialize(lv_obj_t *waiting, lv_obj_t *connection) {
    waiting_ = waiting;
    connection_ = connection;
    ready_ = true;
    render();
  }

  void receive(const std::string &payload) {
    if (!ready_) {
      ESP_LOGW("dashboard", "Dashboard is not initialized; Home Assistant should retry");
      return;
    }
    if (payload.size() > 16384) {
      ESP_LOGE("dashboard", "Dashboard payload exceeds 16384 bytes");
      return;
    }
    esphome::json::parse_json(payload, [this](JsonObject root) {
      if (!root["version"].is<int>() || root["version"].as<int>() != 1 ||
          !root["pages"].is<JsonArray>() || root["pages"].size() != 4) {
        ESP_LOGE("dashboard", "Expected dashboard version 1 with exactly four page slots");
        return false;
      }
      std::array<PageData, 4> next{};
      for (size_t p = 0; p < next.size(); p++) {
        JsonObject page = root["pages"][p];
        if (page.isNull() || !read_string(page["title"], next[p].title, 320) ||
            !read_string(page["temperature"], next[p].temperature, 320) ||
            !page["buttons"].is<JsonArray>() || page["buttons"].size() != 4) {
          ESP_LOGE("dashboard", "Invalid page %u; configuration unchanged", unsigned(p + 1));
          return false;
        }
        for (size_t b = 0; b < 4; b++) {
          JsonObject button = page["buttons"][b];
          auto &data = next[p].buttons[b];
          if (button.isNull() || !read_string(button["entity"], data.entity, 128) ||
              !read_string(button["name"], data.name, 320) ||
              !read_string(button["state"], data.state, 40) ||
              (!button["icon"].isNull() && !read_string(button["icon"], data.icon, 128)) ||
              (!data.entity.empty() && !valid_entity(data.entity))) {
            ESP_LOGE("dashboard", "Invalid button %u on page %u; configuration unchanged",
                     unsigned(b + 1), unsigned(p + 1));
            return false;
          }
          if (!data.icon.empty() && mdi_glyph(data.icon) == nullptr) {
            ESP_LOGW("dashboard", "Unsupported icon '%s' for '%s'; using state-aware default",
                     data.icon.c_str(), data.entity.c_str());
            data.icon.clear();
          }
          next[p].enabled |= !data.entity.empty();
        }
      }
      bool layout_changed = false;
      for (size_t p = 0; p < 4; p++) {
        layout_changed |= next[p].enabled != pages_[p].enabled || next[p].title != pages_[p].title;
        for (size_t b = 0; b < 4; b++) {
          if (next[p].buttons[b].entity != pages_[p].buttons[b].entity) {
            layout_changed = true;
            lv_label_set_text(widgets_[p].buttons[b].state, "UNKNOWN");
            lv_obj_set_style_text_color(widgets_[p].buttons[b].state, lv_color_hex(0xAAB4C5), 0);
            lv_label_set_text(widgets_[p].buttons[b].icon, mdi_glyph(default_icon(next[p].buttons[b])));
          }
        }
      }
      if (layout_changed)
        layout_revision_++;
      pages_ = std::move(next);
      configured_ = true;
      last_update_ = esphome::millis();
      online_ = true;
      if (wake_pending_) {
        wake();
      } else {
        ensure_current_page();
        render();
      }
      return true;
    });
  }

  void check_connection() {
    if (!ready_)
      return;
    const bool online = configured_ &&
                        esphome::api::global_api_server->is_connected_with_state_subscription() &&
                        uint32_t(esphome::millis() - last_update_) < 65000;
    if (online != online_) {
      online_ = online;
      render();
    }
  }

  void navigate(int direction) {
    if (!ready_)
      return;
    int p = int(current_);
    for (size_t step = 0; step < pages_.size(); step++) {
      p = (p + direction + int(pages_.size())) % int(pages_.size());
      if (pages_[p].enabled) {
        select_page(p);
        return;
      }
    }
  }

  void set_wake_page(size_t page) {
    if (page > pages_.size()) {
      ESP_LOGE("dashboard", "Invalid wake-up page %u", unsigned(page));
      return;
    }
    wake_page_ = page;
  }

  void wake() {
    navigation_open_ = false;
    layout_revision_++;
    if (!ready_ || !configured_) {
      wake_pending_ = true;
      return;
    }
    wake_pending_ = std::none_of(pages_.begin(), pages_.end(),
                                 [](const PageData &page) { return page.enabled; });
    if (wake_page_ != 0)
      current_ = wake_page_ - 1;
    ensure_current_page();
    render();
  }

  void begin_touch(int id, int x, int y) {
    gesture_.begin(id, x, y);
    touch_page_ = current_;
    touch_revision_ = layout_revision_;
    if (ready_) {
      lv_obj_update_layout(lv_screen_active());
      lv_obj_update_layout(lv_layer_top());
    }
    touch_target_ = ready_ ? hit_target(x, y) : TouchTarget{};
    touch_entity_ = touch_target_.kind == TargetKind::ENTITY
                        ? pages_[current_].buttons[touch_target_.index].entity : "";
  }

  void update_touch(int id, int x, int y) { gesture_.update(id, x, y); }

  TouchAction end_touch() {
    const auto action = gesture_.finish();
    if (!ready_ || touch_revision_ != layout_revision_ || touch_page_ != current_)
      return TouchAction::NONE;
    if (action == TouchAction::NEXT || action == TouchAction::PREVIOUS) {
      if (navigation_open_)
        return TouchAction::NONE;
      navigate(action == TouchAction::NEXT ? 1 : -1);
    } else if (action == TouchAction::TAP && touch_target_ == hit_target(gesture_.x(), gesture_.y())) {
      if (touch_target_.kind == TargetKind::MENU) {
        navigation_open_ = !navigation_open_;
        layout_revision_++;
        render();
      } else if (touch_target_.kind == TargetKind::PAGE) {
        select_page(touch_target_.index);
      } else if (touch_target_.kind == TargetKind::ENTITY &&
                 pages_[current_].buttons[touch_target_.index].entity == touch_entity_) {
        press(current_, touch_target_.index);
      }
    }
    return action;
  }

  void press(size_t page, size_t button) {
    check_connection();
    if (page >= 4 || button >= 4 || !online_ || !pages_[page].enabled) {
      ESP_LOGW("dashboard", "Ignoring button press without a live Home Assistant configuration");
      return;
    }
    const auto &data = pages_[page].buttons[button];
    if (data.entity.empty() || !known_state(data)) {
      ESP_LOGW("dashboard", "Ignoring button press for an unavailable entity");
      return;
    }
    const auto domain = data.entity.substr(0, data.entity.find('.'));
    const std::string action = domain == "lock"
                                   ? (data.state == "locked" ? "lock.unlock" : "lock.lock")
                                   : domain + ".toggle";
    call_homeassistant_service(action, {{"entity_id", data.entity}});
  }

 private:
  enum class TargetKind { NONE, ENTITY, MENU, PAGE };

  struct TouchTarget {
    TargetKind kind{TargetKind::NONE};
    size_t index{0};
    bool operator==(const TouchTarget &other) const {
      return kind == other.kind && index == other.index;
    }
  };

  void select_page(size_t page) {
    current_ = page;
    navigation_open_ = false;
    layout_revision_++;
    render();
  }

  void ensure_current_page() {
    if (pages_[current_].enabled)
      return;
    current_ = 0;
    for (size_t p = 0; p < pages_.size(); p++) {
      if (pages_[p].enabled) {
        current_ = p;
        break;
      }
    }
  }

  static bool hit_widget(lv_obj_t *widget, int x, int y) {
    if (!lv_obj_is_visible(widget) || lv_obj_has_state(widget, LV_STATE_DISABLED))
      return false;
    lv_area_t area;
    lv_obj_get_coords(widget, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
  }

  TouchTarget hit_target(int x, int y) const {
    if (hit_widget(navigation_.menu, x, y))
      return {TargetKind::MENU, 0};
    if (navigation_open_) {
      for (size_t p = 0; p < pages_.size(); p++) {
        if (pages_[p].enabled && hit_widget(navigation_.buttons[p], x, y))
          return {TargetKind::PAGE, p};
      }
      return {};
    }
    if (!pages_[current_].enabled)
      return {};
    for (size_t b = 0; b < 4; b++) {
      if (hit_widget(widgets_[current_].buttons[b].button, x, y))
        return {TargetKind::ENTITY, b};
    }
    return {};
  }

  static void position_button(lv_obj_t *button, size_t count, size_t rank) {
    const int x = count == 1 || (count == 3 && rank == 2) ? 220 : (rank % 2 == 0 ? 20 : 420);
    lv_obj_set_pos(button, x, rank < 2 ? 65 : 230);
  }

  std::string page_title(size_t page) const {
    const auto &title = pages_[page].title;
    return title.find_first_not_of(" \t\r\n") == std::string::npos
               ? "Page " + std::to_string(page + 1) : title;
  }

  static bool read_string(JsonVariant value, std::string &out, size_t limit) {
    if (!value.is<const char *>())
      return false;
    out = value.as<const char *>();
    return out.size() <= limit;
  }

  static bool valid_entity(const std::string &entity) {
    const auto dot = entity.find('.');
    if (dot == std::string::npos || dot + 1 == entity.size())
      return false;
    const auto domain = entity.substr(0, dot);
    if (domain != "light" && domain != "switch" && domain != "automation" && domain != "lock")
      return false;
    for (size_t i = dot + 1; i < entity.size(); i++) {
      const char c = entity[i];
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
        return false;
    }
    return true;
  }

  static bool known_state(const ButtonData &button) {
    if (button.entity.compare(0, 5, "lock.") == 0)
      return button.state == "locked" || button.state == "unlocked";
    return button.state == "on" || button.state == "off";
  }

  static const char *default_icon(const ButtonData &button) {
    const auto domain = button.entity.substr(0, button.entity.find('.'));
    if (domain == "light")
      return button.state == "off" ? "mdi:lightbulb-off" : "mdi:lightbulb";
    if (domain == "switch")
      return button.state == "off" ? "mdi:toggle-switch-variant-off" : "mdi:toggle-switch-variant";
    if (domain == "automation")
      return button.state == "off" ? "mdi:robot-off" : "mdi:robot";
    if (button.state == "jammed")
      return "mdi:lock-alert";
    if (button.state == "locking" || button.state == "unlocking" || button.state == "opening")
      return "mdi:lock-clock";
    return button.state == "unlocked" || button.state == "open" ? "mdi:lock-open-variant" : "mdi:lock";
  }

  static void visible(lv_obj_t *widget, bool show) {
    if (show)
      lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
  }

  void render() {
    size_t page_count = 0;
    for (const auto &page : pages_)
      page_count += page.enabled;
    if (page_count == 0 && navigation_open_) {
      navigation_open_ = false;
      layout_revision_++;
    }
    visible(navigation_.container, navigation_open_ && page_count > 0);
    if (page_count > 0)
      lv_obj_remove_state(navigation_.menu, LV_STATE_DISABLED);
    else
      lv_obj_add_state(navigation_.menu, LV_STATE_DISABLED);
    size_t navigation_rank = 0;
    for (size_t p = 0; p < pages_.size(); p++) {
      visible(navigation_.buttons[p], pages_[p].enabled);
      lv_label_set_text(navigation_.titles[p], page_title(p).c_str());
      if (pages_[p].enabled)
        position_button(navigation_.buttons[p], page_count, navigation_rank++);
    }
    visible(waiting_, page_count == 0);
    lv_label_set_text(waiting_, configured_ ? "No button pages configured" : "Waiting for Home Assistant");
    visible(connection_, configured_ && !online_ && page_count > 0);
    size_t page_rank = 0;
    for (size_t p = 0; p < 4; p++) {
      auto &widgets = widgets_[p];
      const auto &page = pages_[p];
      visible(widgets.container, !navigation_open_ && page.enabled && p == current_);
      lv_label_set_text(widgets.title, page_title(p).c_str());
      visible(widgets.temperature, !page.temperature.empty());
      lv_label_set_text(widgets.temperature,
                        online_ ? page.temperature.c_str() : "Temperature: UNAVAILABLE");
      for (size_t d = 0; d < 4; d++) {
        visible(widgets.dots[d], page_count > 1 && d < page_count);
        const int dot_width = page_count > 0 ? int((page_count - 1) * 20 + 12) : 0;
        lv_obj_set_x(widgets.dots[d], 400 - dot_width / 2 + int(d * 20));
        lv_obj_set_style_bg_opa(widgets.dots[d], d == page_rank ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      }
      size_t button_count = 0;
      for (const auto &button : page.buttons)
        button_count += !button.entity.empty();
      size_t button_rank = 0;
      for (size_t b = 0; b < 4; b++) {
        const auto &data = page.buttons[b];
        auto &button = widgets.buttons[b];
        visible(button.button, !data.entity.empty());
        if (data.entity.empty())
          continue;
        position_button(button.button, button_count, button_rank++);
        lv_label_set_text(button.name, data.name.c_str());
        const bool available = online_ && known_state(data);
        visible(button.unavailable, !available);
        if (available)
          lv_obj_remove_state(button.button, LV_STATE_DISABLED);
        else
          lv_obj_add_state(button.button, LV_STATE_DISABLED);
        const auto domain = data.entity.substr(0, data.entity.find('.'));
        const auto icon = data.icon.empty() ? default_icon(data) : data.icon.c_str();
        // Preserve the last icon when an entity becomes unavailable.
        if (known_state(data) || !data.icon.empty() || data.state == "jammed" ||
            data.state == "locking" || data.state == "unlocking" || data.state == "opening" ||
            data.state == "open")
          lv_label_set_text(button.icon, mdi_glyph(icon));
        if (known_state(data)) {
          const bool active = data.state == "on" || data.state == "locked";
          const char *label = domain == "lock" ? (active ? "LOCKED" : "UNLOCKED")
                              : domain == "automation" ? (active ? "ENABLED" : "DISABLED")
                                                       : (active ? "ON" : "OFF");
          const uint32_t color = domain == "lock" ? (active ? 0x68D391 : 0xF87171)
                                                 : (active ? 0xFFD166 : 0xAAB4C5);
          lv_label_set_text(button.state, label);
          lv_obj_set_style_text_color(button.state, lv_color_hex(color), 0);
        }
      }
      page_rank += page.enabled;
    }
  }

  std::array<PageWidgets, 4> widgets_{};
  NavigationWidgets navigation_{};
  std::array<PageData, 4> pages_{};
  lv_obj_t *waiting_{nullptr};
  lv_obj_t *connection_{nullptr};
  size_t current_{0};
  size_t wake_page_{0};
  uint32_t last_update_{0};
  uint32_t layout_revision_{0};
  uint32_t touch_revision_{0};
  bool navigation_open_{false};
  bool wake_pending_{false};
  bool ready_{false};
  bool configured_{false};
  bool online_{false};
  TouchGesture gesture_;
  size_t touch_page_{0};
  TouchTarget touch_target_;
  std::string touch_entity_;
};

}  // namespace wallpanel
