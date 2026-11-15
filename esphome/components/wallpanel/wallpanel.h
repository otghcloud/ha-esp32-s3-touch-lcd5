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
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
#include "model.h"
#include "interaction.h"
#include "page_buttons.h"
#include "page_navigation.h"
#include "page_screensaver.h"
#include "page_homepage.h"
#include "page_settings.h"
#include "detail_data.h"
#include "light_actions.h"
#include "releases.h"

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
  void bind_update(lv_obj_t *overlay) { update_overlay_ = overlay; }
  void bind_detail(const DetailWidgets &widgets) { detail_widgets_ = widgets; }
  void loop() override {
    check_touch_hold();
    if (ready_ && !updating_ && !screensaver_active_ && view_ == View::SETTINGS &&
        uint32_t(esphome::millis() - settings_updated_) >= 1000)
      refresh_settings_ip();
    if (!updating_ && detail_pending_ && uint32_t(esphome::millis() - pending_at_) >= 5000) {
      detail_pending_ = false;
      detail_error_ = true;
      ESP_LOGW("dashboard", "Light command was not confirmed within five seconds");
      render();
    }
  }
  bool updating() const { return updating_; }
  std::string release_status() const {
    if (blueprint_release_.empty()) return "Waiting for Home Assistant";
    if (blueprint_release_ != PROJECT_VERSION)
      return "Firmware/blueprint mismatch: " + blueprint_release_;
    if (latest_release_.empty()) return "Latest release unavailable";
    if (latest_release_parts_ > PROJECT_VERSION_PARTS)
      return "Update available: " + latest_release_;
    if (latest_release_parts_ < PROJECT_VERSION_PARTS) return "Ahead of latest stable";
    return "Up to date";
  }
  void begin_update() {
    if (!ready_ || updating_) return;
    updating_ = true;
    detail_editing_ = detail_pending_ = false;
    layout_revision_++;
    gesture_.finish();
    clear_transfer();
    visible(update_overlay_, true);
    lv_refr_now(nullptr);
  }
  void finish_update() {
    if (!updating_) return;
    updating_ = false;
    visible(update_overlay_, false);
    layout_revision_++;
    if (activity_) activity_();
    render();
    if (refresh_clock_) refresh_clock_();
  }
  void bind_homepage(lv_obj_t *container, lv_obj_t *title) {
    homepage_container_ = container;
    homepage_title_ = title;
  }
  void bind_settings(lv_obj_t *container, lv_obj_t *button, const std::array<lv_obj_t *, 3> &info) {
    settings_container_ = container;
    settings_button_ = button;
    settings_info_ = info;
    lv_label_set_text(info[0], esphome::App.get_name().c_str());
    lv_label_set_text(info[1], PROJECT_VERSION);
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
  void touch_activity() { if (!updating_) manual_sleep_ = false; }
  void backlight_on() { if (!updating_) { manual_sleep_ = false; wake(); } }
  void backlight_off() {
    if (updating_) return;
    gesture_.finish();
    hold_candidate_ = detail_editing_ = detail_pending_ = false;
    layout_revision_++;
    if (!automatic_sleep_)
      manual_sleep_ = true;
  }
  void sensor_wake() {
    if (updating_) return;
    manual_sleep_ = false;
    if (power_wake_) power_wake_();
    else ESP_LOGE("dashboard", "Wake action is not bound");
  }
  void sleep(bool automatic = false) {
    if (updating_) return;
    if (!automatic)
      manual_sleep_ = true;
    automatic_sleep_ = automatic;
    if (power_sleep_) power_sleep_();
    else ESP_LOGE("dashboard", "Sleep action is not bound");
    automatic_sleep_ = false;
  }
  void bind_fonts(const lv_font_t *icons, const lv_font_t *dense_icons,
                  const lv_font_t *medium, const lv_font_t *small, const lv_font_t *ui_icons) {
    icons_ = icons;
    dense_icons_ = dense_icons;
    medium_ = medium;
    small_ = small;
    ui_icons_ = ui_icons;
  }

  void initialize(lv_obj_t *waiting, lv_obj_t *connection) {
    waiting_ = waiting;
    connection_ = connection;
    ready_ = true;
    render();
  }

  void receive(const std::string &payload) {
    if (updating_) {
      ESP_LOGW("dashboard", "Ignoring configuration during OTA; resend after recovery");
      return;
    }
    if (!ready_) {
      ESP_LOGW("dashboard", "Dashboard is not initialized; Home Assistant should retry");
      return;
    }
    if (payload.size() > MAX_PAYLOAD_BYTES) {
      ESP_LOGE("dashboard", "Dashboard payload exceeds %u bytes", unsigned(MAX_PAYLOAD_BYTES));
      return;
    }
    esphome::json::parse_json(payload, [this](JsonObject root) {
      const int version = root["version"].is<int>() ? root["version"].as<int>() : 0;
      if (version != 3 || !root["pages"].is<JsonArray>() || root["pages"].size() != MAX_PAGES) {
        ESP_LOGE("dashboard", "Expected protocol version 3 with %u pages", unsigned(MAX_PAGES));
        return false;
      }
      std::string blueprint_release, latest_release;
      std::array<uint32_t, 3> blueprint_parts{}, latest_parts{};
      if (!read_text(root["blueprint_release"], blueprint_release, 32) ||
          !read_release(blueprint_release, blueprint_parts) ||
          !read_text(root["latest_release"], latest_release, 32) ||
          (!latest_release.empty() && !read_release(latest_release, latest_parts))) {
        ESP_LOGE("dashboard", "Invalid release metadata; configuration unchanged");
        return false;
      }
      Layout navigation_layout = Layout::TWO_BY_TWO;
      if (!read_layout(root["navigation_layout"], navigation_layout))
        return false;
      if (!root["screensaver_enabled"].is<bool>()) {
        ESP_LOGE("dashboard", "Screensaver enabled must be a boolean; configuration unchanged");
        return false;
      }
      const bool screensaver_enabled = root["screensaver_enabled"];
      Homepage homepage;
      const auto home = root["homepage"];
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
      Localization localization;
      if (!read_localization(root["localization"], localization))
        return false;
      if (!root["wake_hold"].is<bool>()) {
        ESP_LOGE("dashboard", "Wake hold must be a boolean; configuration unchanged");
        return false;
      }
      const bool wake_hold = root["wake_hold"];
      std::vector<std::string> reverse;
      auto reversed = root["reverse_active"];
      if (!reversed.is<JsonArray>() || reversed.size() > MAX_PAGES * MAX_BUTTONS * 3) {
        ESP_LOGE("dashboard", "Invalid reverse-active entity list");
        return false;
      }
      for (JsonVariant value : reversed.as<JsonArray>()) {
        std::string entity;
        if (!read_text(value, entity, 128) || !valid_entity_id(entity) ||
            std::find(reverse.begin(), reverse.end(), entity) != reverse.end()) {
          ESP_LOGE("dashboard", "Invalid or duplicate reverse-active entity");
          return false;
        }
        reverse.push_back(std::move(entity));
      }
      const auto order = root["page_order"];
      if (!order.is<JsonArray>() || order.size() != MAX_PAGES) {
        ESP_LOGE("dashboard", "Page order must list all eight page slots; configuration unchanged");
        return false;
      }
      std::array<size_t, MAX_PAGES> next_order{};
      std::array<bool, MAX_PAGES> seen{};
      for (size_t rank = 0; rank < MAX_PAGES; rank++) {
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
        if (!read_layout(page["layout"], next[p].layout) ||
            !read_string(page["icon"], next[p].icon, 128)) {
          ESP_LOGE("dashboard", "Invalid layout or icon on page %u", unsigned(p + 1));
          return false;
        }
        if (!next[p].icon.empty() && mdi_glyph(next[p].icon) == nullptr) {
          ESP_LOGW("dashboard", "Unsupported page icon '%s'; header omitted, navigation uses dashboard icon", next[p].icon.c_str());
          next[p].icon.clear();
        }
        if (page.isNull() || !read_string(page["title"], next[p].title, 320) ||
            !read_string(page["temperature"], next[p].temperature, 320) ||
            !page["compact"].is<bool>() ||
            !page["buttons"].is<JsonArray>() || page["buttons"].size() != MAX_BUTTONS) {
          ESP_LOGE("dashboard", "Invalid page %u; configuration unchanged", unsigned(p + 1));
          return false;
        }
        next[p].compact = page["compact"].as<bool>();
        size_t populated = 0;
        const auto capacity = grid(next[p].layout).capacity();
        for (size_t b = 0; b < MAX_BUTTONS; b++) {
          JsonObject button = page["buttons"][b];
          auto &data = next[p].buttons[b];
          if (button.isNull() || !read_string(button["entity"], data.entity, 128) ||
              !read_string(button["name"], data.name, 320) ||
              !read_string(button["state"], data.state, 40) ||
              !read_string(button["icon"], data.icon, 128) ||
              (!data.entity.empty() && !valid_entity(data.entity)) ||
              !button["tap_action"].is<const char *>()) {
            ESP_LOGE("dashboard", "Invalid button %u on page %u; configuration unchanged",
                     unsigned(b + 1), unsigned(p + 1));
            return false;
          }
          const std::string tap_action = button["tap_action"].as<const char *>();
          populated += !data.entity.empty();
          if ((tap_action != "default" && tap_action != "details") ||
              (tap_action == "details" && !data.entity.starts_with("light.")) ||
              populated > capacity || (!next[p].compact && !data.entity.empty() && b >= capacity)) {
            ESP_LOGE("dashboard", "Invalid tap action or grid position on page %u button %u",
                     unsigned(p + 1), unsigned(b + 1));
            return false;
          }
          data.tap_action = tap_action == "details" ? TapAction::DETAILS : TapAction::DEFAULT;
          data.reverse_active = std::find(reverse.begin(), reverse.end(), data.entity) != reverse.end();
          if (!data.icon.empty() && mdi_glyph(data.icon) == nullptr) {
            ESP_LOGW("dashboard", "Unsupported icon '%s' for '%s'; using state-aware default",
                     data.icon.c_str(), data.entity.c_str());
            data.icon.clear();
          }
          next[p].enabled |= !data.entity.empty();
        }
        page_count += next[p].enabled;
      }
      DetailData details;
      if (!read_detail_data(root, details, reverse)) {
        ESP_LOGE("dashboard", "Invalid light-detail metadata; configuration unchanged");
        return false;
      }
      for (const auto &light : details.lights) {
        bool found = false;
        for (const auto &page : next)
          for (const auto &button : page.buttons)
            found |= button.entity == light.entity;
        if (!found) {
          ESP_LOGE("dashboard", "Detail light is absent from active page slots");
          return false;
        }
      }
      if (page_count > grid(navigation_layout).capacity()) {
        ESP_LOGE("dashboard", "Navigation layout cannot fit %u populated pages; configuration unchanged",
                 unsigned(page_count));
        return false;
      }
      bool layout_changed = navigation_layout != navigation_layout_ || screensaver_enabled != screensaver_enabled_ ||
                            next_order != page_order_ || homepage.enabled != homepage_.enabled ||
                            homepage.position != homepage_.position || homepage.title != homepage_.title;
      const auto *old_light = find_light(details_, detail_entity_);
      const auto *new_light = find_light(details, detail_entity_);
      layout_changed |= (old_light == nullptr) != (new_light == nullptr);
      if (old_light && new_light)
        layout_changed |= old_light->known != new_light->known || old_light->dimmable != new_light->dimmable ||
                          old_light->colour != new_light->colour || old_light->temperature != new_light->temperature ||
                          old_light->min_kelvin != new_light->min_kelvin || old_light->max_kelvin != new_light->max_kelvin ||
                          old_light->related != new_light->related || old_light->related_titles != new_light->related_titles;
      for (size_t p = 0; p < MAX_PAGES; p++) {
        layout_changed |= next[p].enabled != pages_[p].enabled || next[p].title != pages_[p].title ||
                          next[p].icon != pages_[p].icon || next[p].layout != pages_[p].layout ||
                          next[p].compact != pages_[p].compact;
        for (size_t b = 0; b < MAX_BUTTONS; b++) {
          auto &data = next[p].buttons[b];
          const auto &previous = pages_[p].buttons[b];
          layout_changed |= data.entity != previous.entity || data.tap_action != previous.tap_action;
          update_appearance(data, &previous);
        }
      }
      for (auto &button : details.related)
        update_appearance(button, find_related(details_, button.entity));
      const bool clock_changed = !configured_ || localization.twelve_hour != localization_.twelve_hour ||
                                 localization.date != localization_.date;
      const bool content_changed = layout_changed || !online_ || next != pages_ || details != details_;
      const bool was_pending = detail_pending_;
      if (layout_changed) {
        layout_revision_++;
        detail_editing_ = detail_pending_ = false;
      }
      if (blueprint_release != PROJECT_VERSION && blueprint_release != blueprint_release_)
        ESP_LOGW("dashboard", "Blueprint release %s differs from firmware %s",
                 blueprint_release.c_str(), PROJECT_VERSION);
      blueprint_release_ = std::move(blueprint_release);
      latest_release_ = std::move(latest_release);
      latest_release_parts_ = latest_parts;
      pages_ = std::move(next);
      details_ = std::move(details);
      if (view_ == View::LIGHT_DETAIL) {
        if (!primary_detail()) view_ = content_view();
        else {
          if (!known_state(*primary_detail())) {
            detail_editing_ = detail_pending_ = false;
            layout_revision_++;
          }
          const auto *light = find_light(details_, detail_entity_);
          if (!supports_mode(light, detail_mode_)) detail_mode_ = initial_detail_mode(light);
          if (detail_pending_ && confirmed_detail(light)) detail_pending_ = false;
        }
      }
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
        if (content_changed || clock_changed || was_pending != detail_pending_)
          render();
      }
      set_sensor_hold(wake_hold);
      if (refresh_clock_ && clock_changed)
        refresh_clock_();
      return true;
    });
  }

  void receive_chunk(const std::string &transfer, int index, int total, const std::string &payload) {
    if (updating_) {
      ESP_LOGW("dashboard", "Ignoring configuration chunk during OTA");
      return;
    }
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
    if (updating_) return;
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
      detail_editing_ = detail_pending_ = false;
      layout_revision_++;
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
    if (!ready_ || updating_ || screensaver_active_ || view_ == View::NAVIGATION ||
        view_ == View::SETTINGS || view_ == View::LIGHT_DETAIL)
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
    if (updating_) return;
    screensaver_active_ = false;
    detail_editing_ = detail_pending_ = false;
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

  void begin_touch(int id, int x, int y, uint32_t now = esphome::millis()) {
    if (updating_) return;
    gesture_.begin(id, x, y, now);
    hold_candidate_ = false;
    detail_editing_ = false;
    touch_page_ = current_;
    touch_revision_ = layout_revision_;
    if (ready_) {
      lv_obj_update_layout(lv_screen_active());
      lv_obj_update_layout(lv_layer_top());
    }
    touch_target_ = ready_ && !screensaver_active_ ? hit_target(x, y) : TouchTarget{};
    touch_entity_ = touch_target_.kind == TargetKind::ENTITY
                        ? pages_[current_].buttons[touch_target_.index].entity : "";
    hold_candidate_ = touch_target_.kind == TargetKind::ENTITY && touch_entity_.starts_with("light.");
    if (touch_target_.kind == TargetKind::RELATED) {
      const auto *light = find_light(details_, detail_entity_);
      touch_entity_ = light ? light->related[touch_target_.index] : "";
    }
    if (touch_target_.kind == TargetKind::EDIT) {
      detail_editing_ = true;
      detail_pending_ = detail_error_ = false;
      preview_detail(x, y);
    }
  }

  void update_touches(const esphome::touchscreen::TouchPoints_t &touches) {
    if (!gesture_.valid()) return;
    const auto active = [](const esphome::touchscreen::TouchPoint &point) {
      return point.state == esphome::touchscreen::STATE_PRESSED ||
             point.state == esphome::touchscreen::STATE_UPDATED;
    };
    const auto owner = std::find_if(touches.begin(), touches.end(), [&](const auto &point) {
      return active(point) && point.id == gesture_.id();
    });
    if (owner != touches.end()) {
      update_touch(owner->id, owner->x, owner->y);
    } else {
      const auto remaining = std::find_if(touches.begin(), touches.end(), active);
      if (remaining != touches.end()) {
        ESP_LOGD("dashboard", "Touch owner ID %d missing; remaining ID %u state %d at (%u,%u)",
                 gesture_.id(), unsigned(remaining->id), int(remaining->state),
                 unsigned(remaining->x), unsigned(remaining->y));
        update_touch(remaining->id, remaining->x, remaining->y);
      }
    }
  }

  void update_touch(int id, int x, int y) {
    gesture_.update(id, x, y);
    if (hold_candidate_ && !gesture_.hold_stationary()) {
      if (gesture_.valid())
        ESP_LOGD("dashboard", "Light hold cancelled: movement exceeded %dpx", TouchGesture::HOLD_SLOP_PX);
      else
        ESP_LOGD("dashboard", "Light hold cancelled: owner ID %d replaced by ID %d at (%d,%d)",
                 gesture_.id(), id, x, y);
      hold_candidate_ = false;
    }
    if (detail_editing_ && gesture_.valid()) preview_detail(x, y);
  }

  void check_touch_hold(uint32_t now = esphome::millis()) {
    if (!ready_ || updating_ || screensaver_active_ || !hold_candidate_) return;
    if (view_ != View::BUTTONS || touch_revision_ != layout_revision_) {
      ESP_LOGD("dashboard", "Light hold cancelled: view or layout changed");
      hold_candidate_ = false;
      return;
    }
    if (!gesture_.held(now)) return;
    hold_candidate_ = false;
    const auto &button = pages_[current_].buttons[touch_target_.index];
    if (button.entity != touch_entity_) return;
    open_details(current_, touch_target_.index);
  }

  TouchAction end_touch() {
    if (hold_candidate_) ESP_LOGD("dashboard", "Light hold released before opening");
    hold_candidate_ = false;
    const bool valid_edit = detail_editing_ && gesture_.valid();
    detail_editing_ = false;
    const auto action = gesture_.finish();
    if (!ready_ || updating_ || screensaver_active_ || touch_revision_ != layout_revision_ || touch_page_ != current_)
      return TouchAction::NONE;
    if (touch_target_.kind == TargetKind::EDIT) {
      const auto *primary = primary_detail();
      if (valid_edit && primary) {
        check_connection();
        detail_pending_ = send_light_command(*primary, find_light(details_, detail_entity_), detail_mode_,
                                              edit_first_, edit_second_, online_);
        detail_error_ = !detail_pending_;
        pending_at_ = esphome::millis();
      }
      render();
      return valid_edit ? TouchAction::TAP : TouchAction::NONE;
    }
    if (action == TouchAction::NEXT || action == TouchAction::PREVIOUS) {
      if (view_ == View::NAVIGATION || view_ == View::SETTINGS || view_ == View::LIGHT_DETAIL)
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
      } else if (touch_target_.kind == TargetKind::BACK) {
        select_page(route_enabled(detail_page_) ? detail_page_ : current_);
      } else if (touch_target_.kind == TargetKind::MODE) {
        detail_mode_ = static_cast<DetailMode>(touch_target_.index);
        detail_pending_ = detail_error_ = false;
        layout_revision_++;
        render();
      } else if (touch_target_.kind == TargetKind::POWER) {
        const auto *primary = primary_detail();
        if (primary) press_data(*primary);
      } else if (touch_target_.kind == TargetKind::RELATED) {
        const auto *data = find_related(details_, touch_entity_);
        const auto *light = find_light(details_, detail_entity_);
        if (data && light && light->related[touch_target_.index] == touch_entity_) press_data(*data);
      } else if (touch_target_.kind == TargetKind::PAGE) {
        select_page(touch_target_.index);
      } else if (touch_target_.kind == TargetKind::ENTITY &&
                 pages_[current_].buttons[touch_target_.index].entity == touch_entity_) {
        if (pages_[current_].buttons[touch_target_.index].tap_action == TapAction::DETAILS)
          open_details(current_, touch_target_.index);
        else
          press(current_, touch_target_.index);
      }
    }
    return action;
  }

  void press(size_t page, size_t button) {
    if (updating_) return;
    check_connection();
    if (screensaver_active_) {
      ESP_LOGW("dashboard", "Ignoring button press while the screensaver is active");
      return;
    }
    if (page >= MAX_PAGES || button >= MAX_BUTTONS || !online_ || !pages_[page].enabled) {
      ESP_LOGW("dashboard", "Ignoring button press without a live Home Assistant configuration");
      return;
    }
    press_data(pages_[page].buttons[button]);
  }

  void press_data(const ButtonData &data) {
    check_connection();
    if (updating_ || !online_) {
      ESP_LOGW("dashboard", "Ignoring action while offline or updating");
      return;
    }
    detail_pending_ = detail_error_ = false;
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
    if (!ready_ || updating_ || !screensaver_enabled_ || screensaver_active_ || sensor_hold() ||
        !idle.inactive_for(screensaver_seconds_, now))
      return;
    screensaver_active_ = true;
    detail_editing_ = detail_pending_ = false;
    view_ = content_view();
    layout_revision_++;
    render();
  }

 private:
  void refresh_settings_ip() {
    std::string ip = "Unavailable";
#ifdef USE_NETWORK
    ip = settings_ip_address(esphome::network::is_connected(), esphome::network::get_ip_addresses());
#endif
    settings_updated_ = esphome::millis();
    if (ip != settings_ip_) {
      lv_label_set_text(settings_info_[2], ip.c_str());
      settings_ip_ = std::move(ip);
    }
  }
  const ButtonData *primary_detail() const {
    if (detail_page_ < MAX_PAGES && detail_button_ < MAX_BUTTONS && pages_[detail_page_].enabled) {
      const auto &button = pages_[detail_page_].buttons[detail_button_];
      if (!detail_entity_.empty() && button.entity == detail_entity_) return &button;
    }
    return nullptr;
  }
  void open_details(size_t page, size_t slot) {
    const auto &button = pages_[page].buttons[slot];
    if (!online_ || !known_state(button) || !button.entity.starts_with("light.")) {
      ESP_LOGW("dashboard", "Cannot open light details without an available light");
      return;
    }
    detail_entity_ = button.entity;
    ESP_LOGD("dashboard", "Opening light details for '%s'", detail_entity_.c_str());
    detail_page_ = page;
    detail_button_ = slot;
    detail_mode_ = initial_detail_mode(find_light(details_, detail_entity_));
    detail_editing_ = detail_pending_ = detail_error_ = hold_candidate_ = false;
    gesture_.finish();
    view_ = View::LIGHT_DETAIL;
    layout_revision_++;
    render();
  }
  static DetailMode initial_detail_mode(const LightData *light) {
    return supports_mode(light, DetailMode::BRIGHTNESS) ? DetailMode::BRIGHTNESS :
           supports_mode(light, DetailMode::TEMPERATURE) ? DetailMode::TEMPERATURE :
           supports_mode(light, DetailMode::COLOUR) ? DetailMode::COLOUR : DetailMode::BRIGHTNESS;
  }
  std::pair<float, float> reported_detail(const LightData *light) const {
    if (!light) return {-1, -1};
    if (detail_mode_ == DetailMode::COLOUR) return {light->hue, light->saturation};
    if (detail_mode_ == DetailMode::TEMPERATURE) return {float(light->kelvin), 0};
    return {light->brightness < 0 ? -1 : float(std::lround(light->brightness * 100.0f / 255)), 0};
  }
  bool confirmed_detail(const LightData *light) const {
    const auto *primary = primary_detail();
    if (detail_mode_ == DetailMode::BRIGHTNESS && edit_first_ == 0 && primary && primary->state == "off")
      return true;
    const auto value = reported_detail(light);
    if (value.first < 0) return false;
    const float difference = std::abs(value.first - edit_first_);
    return detail_mode_ == DetailMode::COLOUR
        ? std::min(difference, 360 - difference) <= 2 && std::abs(value.second - edit_second_) <= 2
        : difference <= (detail_mode_ == DetailMode::TEMPERATURE ? 50 : 1);
  }
  void preview_detail(int x, int y) {
    const auto *light = find_light(details_, detail_entity_);
    if (!supports_mode(light, detail_mode_)) return;
    if (detail_mode_ == DetailMode::COLOUR) {
      const float dx = x - 239.5f, dy = 251.5f - y;
      edit_first_ = std::fmod(std::atan2(dy, dx) * 180 / float(M_PI) + 360, 360);
      edit_second_ = std::min(100.0f, std::hypot(dx, dy) / 1.195f);
    } else {
      lv_area_t area;
      lv_obj_get_coords(detail_widgets_.slider, &area);
      const float fraction = std::clamp(float(area.y2 - y) / (lv_area_get_height(&area) - 1), 0.0f, 1.0f);
      edit_first_ = std::round(detail_mode_ == DetailMode::BRIGHTNESS ? fraction * 100 :
                       light->min_kelvin + fraction * (light->max_kelvin - light->min_kelvin));
      edit_second_ = 0;
    }
    render_detail_value(detail_widgets_, detail_mode_, light, edit_first_, edit_second_);
    if (detail_mode_ == DetailMode::COLOUR) visible(detail_widgets_.marker, true);
  }
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

  enum class TargetKind { NONE, ENTITY, MENU, PAGE, SETTINGS, RESTART, SLEEP, BACK, MODE, POWER, RELATED, EDIT };

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

  static bool hit_widget(lv_obj_t *widget, int x, int y, int padding = 0) {
    if (!widget || !lv_obj_is_visible(widget) || lv_obj_has_state(widget, LV_STATE_DISABLED))
      return false;
    lv_area_t area;
    lv_obj_get_coords(widget, &area);
    return x >= area.x1 - padding && x <= area.x2 + padding &&
           y >= area.y1 - padding && y <= area.y2 + padding;
  }

  TouchTarget hit_target(int x, int y) const {
    if (hit_widget(settings_button_, x, y))
      return {TargetKind::SETTINGS, 0};
    if (hit_widget(navigation_.menu, x, y))
      return {TargetKind::MENU, 0};
    if (view_ == View::LIGHT_DETAIL) {
      if (hit_widget(buttons_[1].button, x, y)) return {TargetKind::BACK, 0};
      if (hit_widget(buttons_[0].button, x, y)) return {TargetKind::POWER, 0};
      for (size_t i = 0; i < 3; i++)
        if (hit_widget(buttons_[2 + i].button, x, y)) return {TargetKind::MODE, i};
      for (size_t i = 0; i < 2; i++)
        if (hit_widget(buttons_[5 + i].button, x, y)) return {TargetKind::RELATED, i};
      const auto *primary = primary_detail();
      if (primary && online_ && known_state(*primary) &&
          ((detail_mode_ != DetailMode::COLOUR &&
            hit_widget(detail_widgets_.slider, x, y, DETAIL_SLIDER_TOUCH_PADDING)) ||
           (detail_mode_ == DetailMode::COLOUR && hit_widget(detail_widgets_.wheel, x, y) &&
            std::hypot(x - 239.5f, y - 251.5f) <= 120)))
        return {TargetKind::EDIT, 0};
      return {};
    }
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
    return read_text(value, out, limit);
  }

  static bool valid_entity(const std::string &entity) {
    return valid_entity_id(entity);
  }

  void render() {
    if (updating_) return;
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
    if (view_ == View::SETTINGS) refresh_settings_ip();
    visible(detail_widgets_.container, view_ == View::LIGHT_DETAIL);
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
                        online_ ? page.temperature.c_str() : "UNAVAILABLE");
      visible(widgets.thermometer, !page.temperature.empty());
      visible(widgets.icon, !page.icon.empty());
      if (!page.icon.empty()) lv_label_set_text(widgets.icon, mdi_glyph(page.icon));
      lv_obj_set_x(widgets.title, page.icon.empty() ? 24 : 70);
      lv_obj_set_width(widgets.title, (!page.temperature.empty() ? 570 : 776) -
                                         (page.icon.empty() ? 24 : 70));
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
                                view_ == View::LIGHT_DETAIL || (view_ == View::BUTTONS && route_enabled(current_)));
    for (const auto &button : buttons_)
      visible(button.button, false);
    const Fonts fonts{icons_, dense_icons_, medium_, small_, ui_icons_};
    if (view_ == View::LIGHT_DETAIL) {
      const auto *primary = primary_detail();
      if (!primary) return;
      const auto *light = find_light(details_, detail_entity_);
      render_light_detail(detail_widgets_, buttons_, fonts, page_title(detail_page_) + " - " + primary->name,
                          *primary, light, details_, detail_mode_, online_);
      const char *status = !online_ ? "Home Assistant disconnected" :
                          !known_state(*primary) ? "Light unavailable" :
                          detail_error_ ? "Change not confirmed; showing reported state" :
                          detail_pending_ ? "Waiting for light..." :
                          !light || !light->known ? "Light capabilities not provided by Home Assistant" :
                          light->temperature_issue ? "Temperature range unavailable" : "";
      lv_label_set_text(detail_widgets_.status, status);
      visible(detail_widgets_.status, status[0] != '\0');
      const auto reported = reported_detail(light);
      render_detail_value(detail_widgets_, detail_mode_, light,
                            detail_editing_ || detail_pending_ ? edit_first_ : reported.first,
                            detail_editing_ || detail_pending_ ? edit_second_ : reported.second,
                            primary->state == "off" && !detail_editing_ && !detail_pending_);
      if ((detail_editing_ || detail_pending_) && detail_mode_ == DetailMode::COLOUR)
        visible(detail_widgets_.marker, true);
      return;
    }
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
      render_entity_tile(button, data, page.layout,
                         page.compact ? button_count : grid(page.layout).capacity(),
                         page.compact ? button_rank++ : b, fonts, online_);
    }
  }

  std::array<PageWidgets, MAX_PAGES> widgets_{};
  std::array<lv_obj_t *, MAX_PAGES + 1> dots_{};
  std::array<size_t, MAX_PAGES> page_order_{};
  lv_obj_t *screensaver_{nullptr};
  lv_obj_t *update_overlay_{nullptr};
  bool updating_{false};
  uint32_t screensaver_seconds_{60};
  bool screensaver_enabled_{false};
  bool screensaver_active_{false};
  Homepage homepage_{};
  Localization localization_{};
  lv_obj_t *homepage_container_{nullptr};
  lv_obj_t *homepage_title_{nullptr};
  lv_obj_t *settings_container_{nullptr};
  lv_obj_t *settings_button_{nullptr};
  std::array<lv_obj_t *, 3> settings_info_{};
  std::string settings_ip_;
  uint32_t settings_updated_{0};
  std::function<void()> restart_, power_wake_, power_sleep_, activity_, refresh_clock_;
  bool sensor_hold_{false};
  bool manual_sleep_{false};
  bool automatic_sleep_{false};
  lv_obj_t *buttons_container_{nullptr};
  std::array<ButtonWidgets, MAX_BUTTONS> buttons_{};
  NavigationWidgets navigation_{};
  std::array<PageData, MAX_PAGES> pages_{};
  DetailData details_{};
  DetailWidgets detail_widgets_{};
  std::string detail_entity_;
  size_t detail_page_{0};
  size_t detail_button_{0};
  DetailMode detail_mode_{DetailMode::BRIGHTNESS};
  bool hold_candidate_{false}, detail_editing_{false}, detail_pending_{false}, detail_error_{false};
  float edit_first_{0}, edit_second_{0};
  uint32_t pending_at_{0};
  Layout navigation_layout_{Layout::TWO_BY_TWO};
  const lv_font_t *icons_{nullptr};
  const lv_font_t *dense_icons_{nullptr};
  const lv_font_t *medium_{nullptr};
  const lv_font_t *small_{nullptr};
  const lv_font_t *ui_icons_{nullptr};
  std::string incoming_;
  std::string transfer_id_;
  int transfer_next_{0};
  int transfer_total_{0};
  uint32_t transfer_update_{0};
  lv_obj_t *waiting_{nullptr};
  lv_obj_t *connection_{nullptr};
  size_t current_{0};
  std::string blueprint_release_, latest_release_;
  std::array<uint32_t, 3> latest_release_parts_{};
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
