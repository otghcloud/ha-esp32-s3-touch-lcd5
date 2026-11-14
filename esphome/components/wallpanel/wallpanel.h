#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include "esphome/components/api/custom_api_device.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
#include "mdi_icons.h"

namespace wallpanel {

inline constexpr size_t MAX_PAGES = 8;
inline constexpr size_t WAKE_NAVIGATION = MAX_PAGES + 1;
inline constexpr size_t MAX_BUTTONS = 18;
inline constexpr size_t MAX_PAYLOAD_BYTES = 262144;
inline constexpr size_t CHUNK_BYTES = 4096;

enum class Layout { TWO_BY_TWO, FOUR_BY_TWO, SIX_BY_THREE };

struct Grid {
  int columns;
  int rows;
  int width;
  int height;
  int gap;
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
  std::array<lv_obj_t *, MAX_PAGES> dots;
};

struct NavigationWidgets {
  lv_obj_t *container;
  lv_obj_t *menu;
  std::array<lv_obj_t *, MAX_PAGES> buttons;
  std::array<lv_obj_t *, MAX_PAGES> titles;
  std::array<lv_obj_t *, MAX_PAGES> icons;
};

struct ButtonData {
  std::string entity;
  std::string name;
  std::string state;
  std::string icon;
  // Cache static labels/glyphs per entity slot, independently of the shared tile.
  const char *display_state{"UNKNOWN"};
  const char *display_icon{nullptr};
  uint32_t display_color{0xAAB4C5};
};

struct PageData {
  std::string title;
  std::string temperature;
  std::string icon;
  Layout layout{Layout::TWO_BY_TWO};
  std::array<ButtonData, MAX_BUTTONS> buttons;
  bool enabled{false};
};

enum class TouchAction { NONE, TAP, NEXT, PREVIOUS };

inline void update_clock(esphome::ESPTime now, lv_obj_t *header, lv_obj_t *clock, lv_obj_t *date) {
  char time_text[esphome::ESPTime::STRFTIME_BUFFER_SIZE] = "--:--";
  char date_text[esphome::ESPTime::STRFTIME_BUFFER_SIZE] = "Waiting for time";
  if (now.is_valid()) {
    now.strftime_to(time_text, "%H:%M");
    now.strftime_to(date_text, "%A, %d %B %Y");
  }
  lv_label_set_text(header, time_text);
  lv_label_set_text(clock, time_text);
  lv_label_set_text(date, date_text);
}

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

  bool expired(uint32_t now) const {
    return inactive_for(timeout_seconds_, now);
  }

  bool inactive_for(uint32_t seconds, uint32_t now) const {
    return seconds != 0 && !touch_active_ && uint32_t(now - last_activity_) >= seconds * 1000;
  }

 private:
  uint32_t timeout_seconds_{0};
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
  Dashboard() {
    for (size_t p = 0; p < MAX_PAGES; p++)
      page_order_[p] = p;
  }

  void bind_page(size_t index, const PageWidgets &widgets) { widgets_[index] = widgets; }
  void bind_buttons(lv_obj_t *container, const std::array<ButtonWidgets, MAX_BUTTONS> &buttons) {
    buttons_container_ = container;
    buttons_ = buttons;
  }
  void bind_navigation(const NavigationWidgets &widgets) { navigation_ = widgets; }
  void bind_screensaver(lv_obj_t *screensaver) { screensaver_ = screensaver; }
  void bind_fonts(const lv_font_t *icons, const lv_font_t *dense_icons,
                  const lv_font_t *medium, const lv_font_t *small) {
    icons_ = icons;
    dense_icons_ = dense_icons;
    medium_ = medium;
    small_ = small;
  }

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
    if (!transfer_id_.empty()) {
      ESP_LOGW("dashboard", "Full snapshot supersedes incomplete dashboard transfer");
      clear_transfer();
    }
    if (payload.size() > MAX_PAYLOAD_BYTES) {
      ESP_LOGE("dashboard", "Dashboard payload exceeds %u bytes", unsigned(MAX_PAYLOAD_BYTES));
      return;
    }
    esphome::json::parse_json(payload, [this](JsonObject root) {
      const int version = root["version"].is<int>() ? root["version"].as<int>() : 0;
      if ((version != 1 && version != 2) || !root["pages"].is<JsonArray>() ||
          root["pages"].size() != (version == 1 ? 4 : MAX_PAGES)) {
        ESP_LOGE("dashboard", "Expected version 1 with four pages or version 2 with %u pages",
                 unsigned(MAX_PAGES));
        return false;
      }
      Layout navigation_layout = Layout::TWO_BY_TWO;
      if (version == 2 && !read_layout(root["navigation_layout"], navigation_layout))
        return false;
      if (!root["screensaver_enabled"].isNull() && !root["screensaver_enabled"].is<bool>()) {
        ESP_LOGE("dashboard", "Screensaver enabled must be a boolean; configuration unchanged");
        return false;
      }
      const bool screensaver_enabled = root["screensaver_enabled"] | false;
      const auto order = root["page_order"];
      if (!order.isNull() && (!order.is<JsonArray>() || order.size() != MAX_PAGES)) {
        ESP_LOGE("dashboard", "Page order must list all eight page slots; configuration unchanged");
        return false;
      }
      std::array<size_t, MAX_PAGES> next_order{};
      std::array<bool, MAX_PAGES> seen{};
      for (size_t rank = 0; rank < MAX_PAGES; rank++) {
        next_order[rank] = rank;
        if (order.isNull())
          continue;
        if (!order[rank].is<int>() || order[rank].as<int>() < 0 ||
            order[rank].as<int>() >= int(MAX_PAGES) || seen[order[rank].as<int>()]) {
          ESP_LOGE("dashboard", "Invalid or duplicate page order slot; configuration unchanged");
          return false;
        }
        next_order[rank] = order[rank].as<int>();
        seen[next_order[rank]] = true;
      }
      // The expanded replacement snapshot must not consume the ESP32 task stack.
      auto storage = std::unique_ptr<std::array<PageData, MAX_PAGES>>(
          new (std::nothrow) std::array<PageData, MAX_PAGES>{});
      if (!storage) {
        ESP_LOGE("dashboard", "Unable to allocate replacement dashboard");
        return false;
      }
      auto &next = *storage;
      size_t page_count = 0;
      for (size_t p = 0; p < root["pages"].size(); p++) {
        JsonObject page = root["pages"][p];
        if (version == 2 && (!read_layout(page["layout"], next[p].layout) ||
                            !read_string(page["icon"], next[p].icon, 128))) {
          ESP_LOGE("dashboard", "Invalid layout or icon on page %u", unsigned(p + 1));
          return false;
        }
        if (!next[p].icon.empty() && mdi_glyph(next[p].icon) == nullptr) {
          ESP_LOGW("dashboard", "Unsupported page icon '%s'; using dashboard icon", next[p].icon.c_str());
          next[p].icon.clear();
        }
        if (page.isNull() || !read_string(page["title"], next[p].title, 320) ||
            !read_string(page["temperature"], next[p].temperature, 320) ||
            !page["buttons"].is<JsonArray>() || page["buttons"].size() != grid(next[p].layout).capacity()) {
          ESP_LOGE("dashboard", "Invalid page %u; configuration unchanged", unsigned(p + 1));
          return false;
        }
        for (size_t b = 0; b < page["buttons"].size(); b++) {
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
        page_count += next[p].enabled;
      }
      if (page_count > grid(navigation_layout).capacity()) {
        ESP_LOGE("dashboard", "Navigation layout cannot fit %u populated pages; configuration unchanged",
                 unsigned(page_count));
        return false;
      }
      bool layout_changed = navigation_layout != navigation_layout_ || screensaver_enabled != screensaver_enabled_ ||
                            next_order != page_order_;
      for (size_t p = 0; p < MAX_PAGES; p++) {
        layout_changed |= next[p].enabled != pages_[p].enabled || next[p].title != pages_[p].title ||
                          next[p].icon != pages_[p].icon || next[p].layout != pages_[p].layout;
        for (size_t b = 0; b < MAX_BUTTONS; b++) {
          auto &data = next[p].buttons[b];
          const auto &previous = pages_[p].buttons[b];
          if (data.entity != previous.entity) {
            layout_changed = true;
          } else {
            data.display_state = previous.display_state;
            data.display_color = previous.display_color;
            data.display_icon = previous.display_icon;
          }
          if (data.entity.empty())
            continue;
          if (data.display_icon == nullptr || known_state(data) || !data.icon.empty() ||
              data.state == "jammed" || data.state == "locking" || data.state == "unlocking" ||
              data.state == "opening" || data.state == "open")
            data.display_icon = mdi_glyph(data.icon.empty() ? default_icon(data) : data.icon.c_str());
          if (known_state(data)) {
            const bool active = data.state == "on" || data.state == "locked";
            const auto domain = data.entity.substr(0, data.entity.find('.'));
            data.display_state = domain == "lock" ? (active ? "LOCKED" : "UNLOCKED")
                                 : domain == "automation" ? (active ? "ENABLED" : "DISABLED")
                                                          : (active ? "ON" : "OFF");
            data.display_color = state_color(data);
          }
        }
      }
      if (layout_changed)
        layout_revision_++;
      pages_ = std::move(next);
      page_order_ = next_order;
      if (!configured_)
        current_ = page_order_.front();
      navigation_layout_ = navigation_layout;
      const bool dismiss_screensaver = screensaver_active_ && !screensaver_enabled;
      screensaver_enabled_ = screensaver_enabled;
      configured_ = true;
      last_update_ = esphome::millis();
      online_ = true;
      if ((wake_pending_ && !screensaver_active_) || dismiss_screensaver) {
        wake();
      } else {
        ensure_current_page();
        render();
      }
      return true;
    });
  }

  void receive_chunk(const std::string &transfer, int index, int total, const std::string &payload) {
    if (transfer.empty() || transfer.size() > 64 || total < 1 ||
        total > int(MAX_PAYLOAD_BYTES / CHUNK_BYTES) || index < 0 || index >= total ||
        payload.size() < 2 || payload.size() > CHUNK_BYTES + 1 || payload[0] != '~') {
      ESP_LOGE("dashboard", "Invalid dashboard chunk");
      clear_transfer();
      return;
    }
    if (index == 0) {
      if (!transfer_id_.empty())
        ESP_LOGW("dashboard", "Replacing incomplete dashboard transfer");
      clear_transfer();
      transfer_id_ = transfer;
      transfer_total_ = total;
    }
    if (transfer != transfer_id_ || total != transfer_total_ || index != transfer_next_ ||
        (uint32_t(esphome::millis() - transfer_update_) > 30000 && index != 0) ||
        incoming_.size() + payload.size() - 1 > MAX_PAYLOAD_BYTES) {
      ESP_LOGE("dashboard", "Out-of-order, expired or oversized dashboard transfer");
      clear_transfer();
      return;
    }
    incoming_.append(payload, 1, std::string::npos);
    transfer_update_ = esphome::millis();
    transfer_next_++;
    if (transfer_next_ == transfer_total_) {
      std::string complete = std::move(incoming_);
      clear_transfer();
      receive(complete);
    }
  }

  void check_connection() {
    if (!transfer_id_.empty() && uint32_t(esphome::millis() - transfer_update_) > 30000) {
      ESP_LOGW("dashboard", "Dashboard transfer expired");
      clear_transfer();
    }
    if (!ready_)
      return;
    const bool online = configured_ &&
                        esphome::api::global_api_server->is_connected_with_state_subscription() &&
                        uint32_t(esphome::millis() - last_update_) < 65000;
    if (online != online_) {
      online_ = online;
      for (auto &page : pages_) {
        for (auto &button : page.buttons) {
          if (known_state(button))
            button.display_color = state_color(button, online_);
        }
      }
      render();
    }
  }

  void navigate(int direction) {
    if (!ready_ || screensaver_active_)
      return;
    int rank = std::find(page_order_.begin(), page_order_.end(), current_) - page_order_.begin();
    for (size_t step = 0; step < pages_.size(); step++) {
      rank = (rank + direction + int(pages_.size())) % int(pages_.size());
      const auto p = page_order_[rank];
      if (pages_[p].enabled) {
        select_page(p);
        return;
      }
    }
  }

  void set_wake_page(size_t page) {
    if (page > WAKE_NAVIGATION) {
      ESP_LOGE("dashboard", "Invalid wake-up page %u", unsigned(page));
      return;
    }
    wake_page_ = page;
  }

  void wake() {
    screensaver_active_ = false;
    navigation_open_ = false;
    layout_revision_++;
    if (!ready_ || !configured_) {
      wake_pending_ = true;
      return;
    }
    wake_pending_ = std::none_of(pages_.begin(), pages_.end(),
                                 [](const PageData &page) { return page.enabled; });
    if (wake_page_ != 0 && wake_page_ != WAKE_NAVIGATION)
      current_ = wake_page_ - 1;
    ensure_current_page();
    navigation_open_ = wake_page_ == WAKE_NAVIGATION && !wake_pending_;
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
    touch_target_ = ready_ && !screensaver_active_ ? hit_target(x, y) : TouchTarget{};
    touch_entity_ = touch_target_.kind == TargetKind::ENTITY
                        ? pages_[current_].buttons[touch_target_.index].entity : "";
  }

  void update_touch(int id, int x, int y) { gesture_.update(id, x, y); }

  TouchAction end_touch() {
    const auto action = gesture_.finish();
    if (!ready_ || screensaver_active_ || touch_revision_ != layout_revision_ || touch_page_ != current_)
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
    if (screensaver_active_) {
      ESP_LOGW("dashboard", "Ignoring button press while the screensaver is active");
      return;
    }
    if (page >= MAX_PAGES || button >= MAX_BUTTONS || !online_ || !pages_[page].enabled) {
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

  void set_screensaver_time(uint32_t seconds) {
    if (seconds > 86400) {
      ESP_LOGE("dashboard", "Screensaver time must be between 0 and 86400 seconds");
      return;
    }
    screensaver_seconds_ = seconds;
    if (screensaver_active_)
      wake();
  }

  bool screensaver_active() const { return screensaver_active_; }

  void check_screensaver(const DisplayIdle &idle, uint32_t now) {
    if (!ready_ || !screensaver_enabled_ || screensaver_active_ ||
        !idle.inactive_for(screensaver_seconds_, now))
      return;
    screensaver_active_ = true;
    navigation_open_ = false;
    layout_revision_++;
    render();
  }

 private:
  void clear_transfer() {
    transfer_id_.clear();
    std::string().swap(incoming_);
    transfer_next_ = transfer_total_ = 0;
  }

  static bool read_layout(JsonVariant value, Layout &layout) {
    if (value.is<const char *>()) {
      const std::string name = value.as<const char *>();
      if (name == "2x2" || name == "4x2" || name == "6x3") {
        layout = name == "2x2" ? Layout::TWO_BY_TWO
                             : name == "4x2" ? Layout::FOUR_BY_TWO : Layout::SIX_BY_THREE;
        return true;
      }
    }
    ESP_LOGE("dashboard", "Layout must be 2x2, 4x2 or 6x3; configuration unchanged");
    return false;
  }

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
    for (const auto p : page_order_) {
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
    for (size_t b = 0; b < MAX_BUTTONS; b++) {
      if (hit_widget(buttons_[b].button, x, y))
        return {TargetKind::ENTITY, b};
    }
    return {};
  }

  static void position_button(lv_obj_t *button, Layout layout, size_t count, size_t rank) {
    const auto profile = grid(layout);
    const int row = rank / profile.columns;
    const int in_row = std::min(int(count) - row * profile.columns, profile.columns);
    const int left = (800 - in_row * profile.width - (in_row - 1) * profile.gap) / 2;
    lv_obj_set_size(button, profile.width, profile.height);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_pos(button, left + int(rank % profile.columns) * (profile.width + profile.gap),
                   65 + row * (profile.height + 10));
  }

  static void place_label(lv_obj_t *label, int x, int y, int width, int height,
                          const lv_font_t *font, lv_text_align_t align) {
    lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, align, 0);
  }

  static uint32_t state_color(const ButtonData &button, bool online = true) {
    if (!online || button.state == "unknown" || button.state == "unavailable")
      return 0xAAB4C5;
    if (button.entity.compare(0, 5, "lock.") == 0) {
      if (button.state == "locked")
        return 0x68D391;
      if (button.state == "unlocked" || button.state == "open" || button.state == "jammed")
        return 0xF87171;
      if (button.state == "locking" || button.state == "unlocking" || button.state == "opening")
        return 0xFFD166;
    }
    return button.state == "on" ? 0xFFD166 : 0xAAB4C5;
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
    visible(screensaver_, screensaver_active_);
    if (screensaver_active_)
      return;
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
    for (const auto p : page_order_) {
      visible(navigation_.buttons[p], pages_[p].enabled);
      lv_label_set_text(navigation_.titles[p], page_title(p).c_str());
      lv_label_set_text(navigation_.icons[p],
                        mdi_glyph(pages_[p].icon.empty() ? "mdi:view-dashboard" : pages_[p].icon));
      if (pages_[p].enabled) {
        position_button(navigation_.buttons[p], navigation_layout_, page_count, navigation_rank++);
        const auto profile = grid(navigation_layout_);
        const bool large = navigation_layout_ == Layout::TWO_BY_TWO;
        place_label(navigation_.icons[p], large ? 22 : 8, (profile.height - (large ? 72 : 36)) / 2,
                    large ? 72 : 36, large ? 72 : 36, large ? icons_ : dense_icons_, LV_TEXT_ALIGN_CENTER);
        place_label(navigation_.titles[p], large ? 112 : 50, (profile.height - (large ? 32 : 22)) / 2,
                    profile.width - (large ? 136 : 58), large ? 32 : 22,
                    large ? medium_ : small_, LV_TEXT_ALIGN_LEFT);
      }
    }
    visible(waiting_, page_count == 0);
    lv_label_set_text(waiting_, configured_ ? "No button pages configured" : "Waiting for Home Assistant");
    visible(connection_, configured_ && !online_ && page_count > 0);
    size_t page_rank = 0;
    for (const auto p : page_order_) {
      auto &widgets = widgets_[p];
      const auto &page = pages_[p];
      visible(widgets.container, !navigation_open_ && page.enabled && p == current_);
      lv_label_set_text(widgets.title, page_title(p).c_str());
      visible(widgets.temperature, !page.temperature.empty());
      lv_label_set_text(widgets.temperature,
                        online_ ? page.temperature.c_str() : "Temperature: UNAVAILABLE");
      for (size_t d = 0; d < MAX_PAGES; d++) {
        visible(widgets.dots[d], page_count > 1 && d < page_count);
        const int dot_width = page_count > 0 ? int((page_count - 1) * 20 + 12) : 0;
        lv_obj_set_x(widgets.dots[d], 400 - dot_width / 2 + int(d * 20));
        lv_obj_set_style_bg_opa(widgets.dots[d], d == page_rank ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      }
      page_rank += page.enabled;
    }
    const auto &page = pages_[current_];
    visible(buttons_container_, !navigation_open_ && page.enabled);
    size_t button_count = 0;
    for (const auto &button : page.buttons)
      button_count += !button.entity.empty();
    size_t button_rank = 0;
    for (size_t b = 0; b < MAX_BUTTONS; b++) {
      const auto &data = page.buttons[b];
      auto &button = buttons_[b];
      visible(button.button, !data.entity.empty());
      if (data.entity.empty())
        continue;
      position_button(button.button, page.layout, button_count, button_rank++);
      const auto profile = grid(page.layout);
      const bool large = page.layout == Layout::TWO_BY_TWO;
      const bool compact = page.layout == Layout::SIX_BY_THREE;
      place_label(button.icon, large ? 22 : (profile.width - 36) / 2, large ? 42 : compact ? 10 : 16,
                  large ? 72 : 36, large ? 72 : 36, large ? icons_ : dense_icons_, LV_TEXT_ALIGN_CENTER);
      place_label(button.name, large ? 112 : 6, large ? 30 : compact ? 59 : 65,
                  profile.width - (large ? 136 : 12), large ? 32 : 22,
                  large ? medium_ : small_, large ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER);
      place_label(button.state, large ? 112 : 6, large ? 78 : 100,
                  profile.width - (large ? 136 : 12), large ? 32 : 22,
                  large ? medium_ : small_, large ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER);
      place_label(button.unavailable, 8, large ? 8 : 132, profile.width - 16, 20,
                  small_, large ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER);
      visible(button.state, !compact);
      lv_label_set_text(button.name, data.name.c_str());
      const bool available = online_ && known_state(data);
      visible(button.unavailable, !available && !compact);
      lv_obj_set_style_opa(button.button, available ? LV_OPA_COVER : LV_OPA_50, 0);
      lv_obj_set_style_text_color(button.icon, lv_color_hex(state_color(data, online_)), 0);
      if (available)
        lv_obj_remove_state(button.button, LV_STATE_DISABLED);
      else
        lv_obj_add_state(button.button, LV_STATE_DISABLED);
      lv_label_set_text(button.icon, data.display_icon);
      lv_label_set_text(button.state, data.display_state);
      lv_obj_set_style_text_color(button.state, lv_color_hex(data.display_color), 0);
    }
  }

  std::array<PageWidgets, MAX_PAGES> widgets_{};
  std::array<size_t, MAX_PAGES> page_order_{};
  lv_obj_t *screensaver_{nullptr};
  uint32_t screensaver_seconds_{60};
  bool screensaver_enabled_{false};
  bool screensaver_active_{false};
  lv_obj_t *buttons_container_{nullptr};
  std::array<ButtonWidgets, MAX_BUTTONS> buttons_{};
  NavigationWidgets navigation_{};
  std::array<PageData, MAX_PAGES> pages_{};
  Layout navigation_layout_{Layout::TWO_BY_TWO};
  const lv_font_t *icons_{nullptr};
  const lv_font_t *dense_icons_{nullptr};
  const lv_font_t *medium_{nullptr};
  const lv_font_t *small_{nullptr};
  std::string incoming_;
  std::string transfer_id_;
  int transfer_next_{0};
  int transfer_total_{0};
  uint32_t transfer_update_{0};
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
