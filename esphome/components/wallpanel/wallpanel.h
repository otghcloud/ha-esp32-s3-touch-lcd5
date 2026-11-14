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
      for (size_t p = 0; p < 4; p++) {
        for (size_t b = 0; b < 4; b++) {
          if (next[p].buttons[b].entity != pages_[p].buttons[b].entity) {
            lv_label_set_text(widgets_[p].buttons[b].state, "UNKNOWN");
            lv_obj_set_style_text_color(widgets_[p].buttons[b].state, lv_color_hex(0xAAB4C5), 0);
            lv_label_set_text(widgets_[p].buttons[b].icon, mdi_glyph(default_icon(next[p].buttons[b])));
          }
        }
      }
      pages_ = std::move(next);
      configured_ = true;
      last_update_ = esphome::millis();
      online_ = true;
      if (!pages_[current_].enabled) {
        current_ = 0;
        for (size_t p = 0; p < pages_.size(); p++) {
          if (pages_[p].enabled) {
            current_ = p;
            break;
          }
        }
      }
      render();
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
        current_ = p;
        render();
        return;
      }
    }
  }

  void begin_touch(int id, int x, int y) {
    gesture_.begin(id, x, y);
    touch_page_ = current_;
    if (ready_)
      lv_obj_update_layout(widgets_[current_].container);
    touch_button_ = ready_ ? hit_button(current_, x, y) : -1;
    touch_entity_ = touch_button_ >= 0 ? pages_[current_].buttons[touch_button_].entity : "";
  }

  void update_touch(int id, int x, int y) { gesture_.update(id, x, y); }

  TouchAction end_touch() {
    const auto action = gesture_.finish();
    if (!ready_)
      return TouchAction::NONE;
    if (action == TouchAction::NEXT)
      navigate(1);
    else if (action == TouchAction::PREVIOUS)
      navigate(-1);
    else if (action == TouchAction::TAP && touch_button_ >= 0 && touch_page_ == current_ &&
             hit_button(current_, gesture_.x(), gesture_.y()) == touch_button_ &&
             pages_[current_].buttons[touch_button_].entity == touch_entity_)
      press(current_, touch_button_);
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
  int hit_button(size_t page, int x, int y) const {
    if (!pages_[page].enabled)
      return -1;
    for (size_t b = 0; b < 4; b++) {
      auto *button = widgets_[page].buttons[b].button;
      if (lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_state(button, LV_STATE_DISABLED))
        continue;
      lv_area_t area;
      lv_obj_get_coords(button, &area);
      if (x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2)
        return int(b);
    }
    return -1;
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
    visible(waiting_, page_count == 0);
    lv_label_set_text(waiting_, configured_ ? "No button pages configured" : "Waiting for Home Assistant");
    visible(connection_, configured_ && !online_ && page_count > 0);
    size_t page_rank = 0;
    for (size_t p = 0; p < 4; p++) {
      auto &widgets = widgets_[p];
      const auto &page = pages_[p];
      visible(widgets.container, page.enabled && p == current_);
      lv_label_set_text(widgets.title, page.title.c_str());
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
        const int x = button_count == 1 || (button_count == 3 && button_rank == 2)
                          ? 220
                          : (button_rank % 2 == 0 ? 20 : 420);
        lv_obj_set_pos(button.button, x, button_rank < 2 ? 65 : 230);
        button_rank++;
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
  std::array<PageData, 4> pages_{};
  lv_obj_t *waiting_{nullptr};
  lv_obj_t *connection_{nullptr};
  size_t current_{0};
  uint32_t last_update_{0};
  bool ready_{false};
  bool configured_{false};
  bool online_{false};
  TouchGesture gesture_;
  size_t touch_page_{0};
  int touch_button_{-1};
  std::string touch_entity_;
};

}  // namespace wallpanel
