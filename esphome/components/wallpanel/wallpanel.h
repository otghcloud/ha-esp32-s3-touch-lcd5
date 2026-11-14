#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include "esphome/components/api/custom_api_device.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
#include "model.h"
#include "interaction.h"
#include "page_buttons.h"
#include "page_navigation.h"
#include "page_screensaver.h"
#include "page_homepage.h"
#include "page_settings.h"

namespace wallpanel {

class Dashboard : public esphome::Component, public esphome::api::CustomAPIDevice {
 public:
  Dashboard() {
    for (size_t p = 0; p < MAX_PAGES; p++)
      page_order_[p] = p;
  }

  void bind_page(size_t index, const PageWidgets &widgets) { widgets_[index] = widgets; }
  void bind_dots(const std::array<lv_obj_t *, MAX_PAGES + 1> &dots) { dots_ = dots; }
  void bind_buttons(lv_obj_t *container, const std::array<ButtonWidgets, MAX_BUTTONS> &buttons) {
    buttons_container_ = container;
    buttons_ = buttons;
  }
  void bind_navigation(const NavigationWidgets &widgets) { navigation_ = widgets; }
  void bind_screensaver(lv_obj_t *screensaver) { screensaver_ = screensaver; }
  void bind_homepage(lv_obj_t *container, lv_obj_t *title) {
    homepage_container_ = container;
    homepage_title_ = title;
  }
  void bind_settings(lv_obj_t *container, lv_obj_t *button) {
    settings_container_ = container;
    settings_button_ = button;
  }
  void bind_restart(std::function<void()> restart) { restart_ = std::move(restart); }
  void bind_power(std::function<void()> wake, std::function<void()> sleep, std::function<void()> activity) {
    power_wake_ = std::move(wake);
    power_sleep_ = std::move(sleep);
    activity_ = std::move(activity);
  }
  void bind_clock(std::function<void()> refresh) { refresh_clock_ = std::move(refresh); }
  const Localization &localization() const { return localization_; }
  bool sensor_hold() const { return sensor_hold_ && !manual_sleep_; }
  void touch_activity() { manual_sleep_ = false; }
  void backlight_on() { manual_sleep_ = false; wake(); }
  void backlight_off() {
    if (!automatic_sleep_)
      manual_sleep_ = true;
  }
  void sensor_wake() {
    manual_sleep_ = false;
    if (power_wake_) power_wake_();
    else ESP_LOGE("dashboard", "Wake action is not bound");
  }
  void sleep(bool automatic = false) {
    if (!automatic)
      manual_sleep_ = true;
    automatic_sleep_ = automatic;
    if (power_sleep_) power_sleep_();
    else ESP_LOGE("dashboard", "Sleep action is not bound");
    automatic_sleep_ = false;
  }
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
      Homepage homepage;
      const auto home = root["homepage"];
      if (!home.isNull()) {
        if (!home.is<JsonObject>() || !home["enabled"].is<bool>() || !home["position"].is<int>() ||
            home["position"].as<int>() < 0 || home["position"].as<int>() > int(MAX_PAGES) ||
            !read_string(home["title"], homepage.title, 320)) {
          ESP_LOGE("dashboard", "Invalid Homepage configuration; configuration unchanged");
          return false;
        }
        homepage.enabled = home["enabled"].as<bool>();
        homepage.position = home["position"].as<int>();
        if (homepage.title.find_first_not_of(" \t\r\n") == std::string::npos)
          homepage.title = "Homepage";
      }
      Localization localization;
      if (!read_localization(root["localization"], localization))
        return false;
      if (!root["wake_hold"].isNull() && !root["wake_hold"].is<bool>()) {
        ESP_LOGE("dashboard", "Wake hold must be a boolean; configuration unchanged");
        return false;
      }
      const bool wake_hold = root["wake_hold"] | false;
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
      size_t page_count = homepage.enabled;
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
                            next_order != page_order_ || homepage.enabled != homepage_.enabled ||
                            homepage.position != homepage_.position || homepage.title != homepage_.title;
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
      homepage_ = std::move(homepage);
      localization_ = std::move(localization);
      if (!configured_)
        current_ = content_order().front();
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
        if (view_ == View::BUTTONS || view_ == View::HOMEPAGE)
          view_ = content_view();
        render();
      }
      set_sensor_hold(wake_hold);
      if (refresh_clock_)
        refresh_clock_();
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

  void check_connection(uint32_t now = esphome::millis()) {
    if (!transfer_id_.empty() && uint32_t(now - transfer_update_) > 30000) {
      ESP_LOGW("dashboard", "Dashboard transfer expired");
      clear_transfer();
    }
    if (!ready_)
      return;
    const bool online = configured_ &&
                        esphome::api::global_api_server->is_connected_with_state_subscription() &&
                        uint32_t(now - last_update_) < 65000;
    if (online != online_) {
      online_ = online;
      if (!online_)
        set_sensor_hold(false);
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
    if (!ready_ || screensaver_active_ || view_ == View::NAVIGATION || view_ == View::SETTINGS)
      return;
    const auto order = content_order();
    int rank = std::find(order.begin(), order.end(), current_) - order.begin();
    for (size_t step = 0; step < order.size(); step++) {
      rank = (rank + direction + int(order.size())) % int(order.size());
      const auto p = order[rank];
      if (route_enabled(p)) {
        select_page(p);
        return;
      }
    }
  }

  void set_wake_page(size_t page) {
    if (page > WAKE_HOMEPAGE) {
      ESP_LOGE("dashboard", "Invalid wake-up page %u", unsigned(page));
      return;
    }
    wake_page_ = page;
  }

  void wake() {
    screensaver_active_ = false;
    view_ = content_view();
    layout_revision_++;
    if (!ready_ || !configured_) {
      wake_pending_ = true;
      return;
    }
    wake_pending_ = !homepage_.enabled && std::none_of(pages_.begin(), pages_.end(),
                                  [](const PageData &page) { return page.enabled; });
    if (wake_page_ != 0 && wake_page_ != WAKE_NAVIGATION)
      current_ = wake_page_ == WAKE_HOMEPAGE ? HOME_SLOT : wake_page_ - 1;
    ensure_current_page();
    view_ = wake_page_ == WAKE_NAVIGATION && !wake_pending_ ? View::NAVIGATION : content_view();
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
      if (view_ == View::NAVIGATION || view_ == View::SETTINGS)
        return TouchAction::NONE;
      navigate(action == TouchAction::NEXT ? 1 : -1);
    } else if (action == TouchAction::TAP && touch_target_ == hit_target(gesture_.x(), gesture_.y())) {
      if (touch_target_.kind == TargetKind::MENU) {
        view_ = view_ == View::NAVIGATION ? content_view() : View::NAVIGATION;
        layout_revision_++;
        render();
      } else if (touch_target_.kind == TargetKind::SETTINGS) {
        view_ = View::SETTINGS;
        wake_pending_ = false;
        layout_revision_++;
        render();
      } else if (touch_target_.kind == TargetKind::RESTART) {
        if (restart_) restart_();
        else ESP_LOGE("dashboard", "Restart action is not bound");
      } else if (touch_target_.kind == TargetKind::SLEEP) {
        sleep();
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
    if (!ready_ || !screensaver_enabled_ || screensaver_active_ || sensor_hold() ||
        !idle.inactive_for(screensaver_seconds_, now))
      return;
    screensaver_active_ = true;
    view_ = content_view();
    layout_revision_++;
    render();
  }

 private:
  void set_sensor_hold(bool active) {
    if (active == sensor_hold_)
      return;
    sensor_hold_ = active;
    if (active && !manual_sleep_) {
      if (power_wake_) power_wake_();
      else ESP_LOGE("dashboard", "Wake action is not bound");
    } else if (!active && activity_) {
      activity_();
    }
  }

  View content_view() const { return current_ == HOME_SLOT ? View::HOMEPAGE : View::BUTTONS; }
  bool route_enabled(size_t page) const {
    return page == HOME_SLOT ? homepage_.enabled : page < MAX_PAGES && pages_[page].enabled;
  }
  std::array<size_t, MAX_PAGES + 1> content_order() const {
    std::array<size_t, MAX_PAGES + 1> result{};
    size_t rank = 0;
    for (size_t i = 0; i <= MAX_PAGES; i++)
      result[i] = i == homepage_.position ? HOME_SLOT : page_order_[rank++];
    return result;
  }

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

  enum class TargetKind { NONE, ENTITY, MENU, PAGE, SETTINGS, RESTART, SLEEP };

  struct TouchTarget {
    TargetKind kind{TargetKind::NONE};
    size_t index{0};
    bool operator==(const TouchTarget &other) const {
      return kind == other.kind && index == other.index;
    }
  };

  void select_page(size_t page) {
    current_ = page;
    view_ = content_view();
    layout_revision_++;
    render();
  }

  void ensure_current_page() {
    if (route_enabled(current_))
      return;
    current_ = 0;
    for (const auto p : content_order()) {
      if (route_enabled(p)) {
        current_ = p;
        break;
      }
    }
  }

  static bool hit_widget(lv_obj_t *widget, int x, int y) {
    if (!widget || !lv_obj_is_visible(widget) || lv_obj_has_state(widget, LV_STATE_DISABLED))
      return false;
    lv_area_t area;
    lv_obj_get_coords(widget, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
  }

  TouchTarget hit_target(int x, int y) const {
    if (hit_widget(settings_button_, x, y))
      return {TargetKind::SETTINGS, 0};
    if (hit_widget(navigation_.menu, x, y))
      return {TargetKind::MENU, 0};
    if (view_ == View::NAVIGATION) {
      for (const auto p : content_order()) {
        if (route_enabled(p) && hit_widget(buttons_[p].button, x, y))
          return {TargetKind::PAGE, p};
      }
      return {};
    }
    if (view_ == View::SETTINGS) {
      if (hit_widget(buttons_[0].button, x, y)) return {TargetKind::RESTART, 0};
      if (hit_widget(buttons_[1].button, x, y)) return {TargetKind::SLEEP, 0};
      return {};
    }
    if (view_ != View::BUTTONS || !route_enabled(current_))
      return {};
    for (size_t b = 0; b < MAX_BUTTONS; b++) {
      if (hit_widget(buttons_[b].button, x, y))
        return {TargetKind::ENTITY, b};
    }
    return {};
  }

  std::string page_title(size_t page) const {
    if (page == HOME_SLOT)
      return homepage_.title;
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

  void render() {
    visible(screensaver_, screensaver_active_);
    if (screensaver_active_)
      return;
    size_t page_count = homepage_.enabled;
    for (const auto &page : pages_)
      page_count += page.enabled;
    if (page_count == 0 && view_ == View::NAVIGATION) {
      view_ = content_view();
      layout_revision_++;
    }
    visible(navigation_.container, view_ == View::NAVIGATION && page_count > 0);
    visible(homepage_container_, view_ == View::HOMEPAGE && homepage_.enabled);
    visible(settings_container_, view_ == View::SETTINGS);
    if (homepage_title_)
      render_homepage(homepage_title_, homepage_);
    if (page_count > 0)
      lv_obj_remove_state(navigation_.menu, LV_STATE_DISABLED);
    else
      lv_obj_add_state(navigation_.menu, LV_STATE_DISABLED);
    visible(waiting_, page_count == 0 && view_ != View::SETTINGS);
    lv_label_set_text(waiting_, configured_ ? "No button pages configured" : "Waiting for Home Assistant");
    visible(connection_, configured_ && !online_ && page_count > 0 && view_ != View::SETTINGS);
    size_t page_rank = 0, current_rank = 0;
    for (const auto p : page_order_) {
      auto &widgets = widgets_[p];
      const auto &page = pages_[p];
      visible(widgets.container, view_ == View::BUTTONS && page.enabled && p == current_);
      lv_label_set_text(widgets.title, page_title(p).c_str());
      visible(widgets.temperature, !page.temperature.empty());
      lv_label_set_text(widgets.temperature,
                        online_ ? page.temperature.c_str() : "Temperature: UNAVAILABLE");
    }
    for (const auto p : content_order()) {
      if (p == current_) current_rank = page_rank;
      page_rank += route_enabled(p);
    }
    for (size_t d = 0; d < dots_.size(); d++) {
      visible(dots_[d], (view_ == View::BUTTONS || view_ == View::HOMEPAGE) &&
                        page_count > 1 && d < page_count);
      const int width = page_count > 0 ? int((page_count - 1) * 20 + 12) : 0;
      lv_obj_set_x(dots_[d], 400 - width / 2 + int(d * 20));
      lv_obj_set_style_bg_opa(dots_[d], d == current_rank ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    visible(buttons_container_, view_ == View::NAVIGATION || view_ == View::SETTINGS ||
                                (view_ == View::BUTTONS && route_enabled(current_)));
    for (const auto &button : buttons_)
      visible(button.button, false);
    const Fonts fonts{icons_, dense_icons_, medium_, small_};
    if (view_ == View::SETTINGS) {
      render_settings(buttons_, fonts);
      return;
    }
    if (view_ == View::HOMEPAGE)
      return;
    if (view_ == View::NAVIGATION) {
      size_t rank = 0;
      for (const auto p : content_order()) {
        if (route_enabled(p))
          render_menu_tile(buttons_[p], page_title(p),
                           p == HOME_SLOT ? "mdi:home" :
                             pages_[p].icon.empty() ? "mdi:view-dashboard" : pages_[p].icon,
                           navigation_layout_, page_count, rank++, fonts);
      }
      return;
    }
    const auto &page = pages_[current_];
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
      render_entity_tile(button, data, page.layout, button_count, button_rank++, fonts, online_);
    }
  }

  std::array<PageWidgets, MAX_PAGES> widgets_{};
  std::array<lv_obj_t *, MAX_PAGES + 1> dots_{};
  std::array<size_t, MAX_PAGES> page_order_{};
  lv_obj_t *screensaver_{nullptr};
  uint32_t screensaver_seconds_{60};
  bool screensaver_enabled_{false};
  bool screensaver_active_{false};
  Homepage homepage_{};
  Localization localization_{};
  lv_obj_t *homepage_container_{nullptr};
  lv_obj_t *homepage_title_{nullptr};
  lv_obj_t *settings_container_{nullptr};
  lv_obj_t *settings_button_{nullptr};
  std::function<void()> restart_, power_wake_, power_sleep_, activity_, refresh_clock_;
  bool sensor_hold_{false};
  bool manual_sleep_{false};
  bool automatic_sleep_{false};
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
  View view_{View::BUTTONS};
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
