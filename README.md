# HA ESP32-S3 Touch LCD 5

ESPHome configuration for the Waveshare ESP32-S3-Touch-LCD-5 (800 × 480), with a touch dashboard connected to Home Assistant.

## Requirements

- Home Assistant with ESPHome Device Builder, or a standalone ESPHome installation.
- A Waveshare ESP32-S3-Touch-LCD-5.
- Home Assistant 2024.10 or newer, with entities to use for your controls.

## Install with ESPHome Device Builder

1. Create a new ESPHome device in Home Assistant and open its YAML configuration.
2. Add the Wi-Fi and API encryption values to the ESPHome `secrets.yaml` file. In Home Assistant this is usually in `/config/esphome/secrets.yaml`:

   ```yaml
   iot_wifi_ssid: "your-wifi-name"
   iot_wifi_password: "your-wifi-password"
   bedroom_nspanel_api_key: "your-generated-api-encryption-key"
   ```

   Keep this file private. The API key can be generated with `openssl rand -base64 32`.

3. Replace the generated device configuration with the following, adjusting the device name and friendly name:

   ```yaml
   substitutions:
     device_name: "example-devpanel"
     friendly_name: "Example DevPanel"
     wifi_ssid: !secret iot_wifi_ssid
     wifi_password: !secret iot_wifi_password

   api:
     encryption:
       key: !secret example_devpanel_api_key

   packages:
     remote_package:
       url: https://github.com/otghcloud/ha-esp32-s3-touch-lcd5
       ref: main
       refresh: 300s
       files:
         - ha-esp32-s3-touch-lcd5.yaml
   ```

4. Save the configuration, validate it, and install the firmware using ESPHome Device Builder.

The remote-package example assumes this project has been published to the `main` branch.
The initial build downloads the Roboto font and Material Design icons, so the build environment needs internet access.
The dashboard runtime component is also fetched from this repository's `main` branch.

Home Assistant's **Device info** identifies the panel as **HA Panel LCD5** by **OTGH Cloud**, using `esphome.project` in [`esphome/core.yaml`](./esphome/core.yaml). The project version is currently `dev`; update `esphome.project.version` when publishing versioned firmware. This metadata updates after installing the firmware and reconnecting, without changing the device name or entity IDs.

## Dashboard configuration

Pages and entities are configured at runtime by the [Home Assistant blueprint](./wallpanel_blueprint.yaml), not baked into firmware. Install the updated firmware once, then:

1. Add the device to the ESPHome integration in Home Assistant.
2. Open the device's ESPHome integration configuration and enable **Allow the device to perform Home Assistant actions**.
3. Under **Settings > Automations & scenes > Blueprints > Import blueprint**, import:

   ```text
   https://raw.githubusercontent.com/otghcloud/ha-esp32-s3-touch-lcd5/refs/heads/main/wallpanel_blueprint.yaml
   ```

   The import URL works after these changes are published to `main`. Before publication, copy `wallpanel_blueprint.yaml` to `/config/blueprints/automation/otghcloud/wallpanel_blueprint.yaml` and reload automations.

4. Create an automation from the blueprint and select your panel device.
5. Expand **Button page 01** through **Button page 08**. Each provides a layout, page icon/title, optional temperature sensor and up to eighteen entity selectors with optional **Title** overrides. Choose **2x2**, **4x2** or **6x3** for 4, 8 or 18 controls respectively. Leave all active button slots empty to hide a page.
6. Save and enable the automation. Run its actions once for an immediate update; future entity changes and reconnects update automatically. Keep only one configuration automation enabled per panel.

The **Dashboard endpoint** diagnostic sensor must remain enabled. It lets the blueprint discover the correct ESPHome action even if you rename the device's friendly name or HA entities.

Buttons use HA friendly names unless their **Title** field is filled in. Blank or whitespace-only titles retain the default name; titles without an entity do not create a button. Labels use at most 80 characters. Lights and switches toggle, automation buttons enable/disable (not trigger), and locks unlock when locked or lock when unlocked, without confirmation.

**Upgrade order:** install the updated firmware **first**, then re-import the updated blueprint and reload automations. Existing input IDs are preserved and new layouts default to 2x2. The new firmware still accepts the old four-page blueprint; older firmware cannot consume the new eight-page protocol.

The maximum is **eight pages / 144 controls**, using the shared-widget renderer. When moving from the temporary six-page build, update both firmware and blueprint: version-2 snapshots now require eight page slots, and six-page snapshots are rejected without replacing the UI. Existing page 1-6 inputs and saved wake choices retain their meaning; new page 7/8 inputs default to empty.

### Home Assistant icons

The firmware includes the complete **Material Design Icons 7.4.47** font at 72 pixels for 2x2 tiles and 36 pixels for denser tiles. The blueprint sends each entity's `icon` attribute, so custom `mdi:` icons (including light, door, lock and automation variants) display on the panel. Without an explicit icon, buttons use Home Assistant's state-aware defaults: lightbulb/lightbulb-off, robot/robot-off, toggle-switch-variant/toggle-switch-variant-off, and locked/unlocked/jammed/transition lock icons. Outlet switches use power-plug/power-plug-off. Explicit glyphs take precedence, but their colours still follow entity state.

Icons are amber for on/enabled and grey for off/disabled automations. Locks use green for locked, red for unlocked/open/jammed and amber for transitions. Unknown/unavailable entities and disconnected controls use grey, dimmed tiles. Lock transitions, jammed and open states remain non-actionable.

Each page's **Page icon** selector controls its icon beside the page name in navigation. Blank or unsupported page icons use `mdi:view-dashboard`; unsupported icons also log a warning.

This matches entity icons, not icon overrides defined only in individual Lovelace cards. Non-MDI icon packs and icons added after 7.4.47 are not supported; the panel logs a warning and uses a default instead. Door icons are available for existing controls; this does not add door sensors or cover controls.

The full icon library requires the board's 16 MB flash layout; existing installations built with the old default 4 MB layout need a **USB/manual factory flash**, because OTA cannot replace the partition table. Subsequent updates can use OTA. Icons are included during compilation; the panel does not download them at runtime.

### Layout and page navigation

Each button page has a 56-pixel black header with its title on the left and the clock on the right. A 48-pixel grey (`#999999`) footer contains the optional page temperature, centred page dots and a bottom-right menu icon. Long titles and temperature values are truncated to avoid overlapping neighbouring controls. Blank or whitespace-only page titles display as **Page N**, using the blueprint slot number.

Each button page independently selects its grid:

| Layout (columns x rows) | Active input slots | Tile content |
| --- | --- | --- |
| 2x2 | 1-4 | Icon, title and state |
| 4x2 | 1-8 | Icon, title and state |
| 6x3 | 1-18 | Icon and title; unavailable controls are dimmed/disabled |

Populated slots compact in configured order, with incomplete rows centred. A smaller layout does **not** delete higher-numbered settings: they are ignored with a Home Assistant warning until a larger layout is selected again. Ignored slots do not enable a page or generate watched-entity updates.

Tap the menu icon to open **Pages**, then tap a page button to select it. Only populated pages appear, in blueprint-slot order, using the independent **Navigation > Menu layout** setting. Page buttons show their configured icons and titles; duplicate titles still select separate slots. More than four populated pages require a **4x2** or **6x3** menu. An invalid menu configuration logs an error and retains the previous dashboard (or waiting screen), rather than hiding pages or adding scrolling/pagination. Tap the menu icon again to return without changing the current page. Navigation hides page-specific temperatures and dots, and ignores swipes.

Swipe left/right anywhere on a button page, including over buttons. Navigation wraps at both ends and skips empty pages. A horizontal movement of at least 100 pixels changes page when you lift your finger. Buttons activate only on release after a tap with no movement beyond 20 pixels; drags and multi-touch gestures do not activate controls. Bottom-centred dots highlight the current page; they are hidden for a single page. Temperature values use the sensor's unit of measurement.

After reboot the panel shows **Waiting for Home Assistant** until it receives configuration. During a disconnection it retains its layout and displays unavailable indicators with disabled controls. No configuration is saved to flash. The blueprint sends current configuration/states on selected entity changes, reconnect events and every 30 seconds; the panel also disables controls if updates stop for 65 seconds.

Page navigation remains available offline using the last known configuration. Renamed, added or removed pages update the menu automatically. With no populated pages, the menu is disabled and the panel shows its waiting/no-pages message. The header and footer remain visible on these screens.

The static 800 × 480 background in [`assets/background_dark.png`](./assets/background_dark.png) is embedded in the firmware and visible behind the content area of every view, including waiting and navigation.

Changing layouts, page icons or controls after the firmware/blueprint upgrade does not require recompilation.

### Display backlight

After installing the updated firmware, the device's **Configuration** section in Home Assistant includes a **Display backlight** switch. It controls the board's active-high CH422G EXIO2 backlight enable and turns on after every reboot. No blueprint update is needed.

The **Display timeout** setting in the same section turns the backlight off after inactivity. Enter seconds from **0 to 86400** (24 hours); **0** disables automatic switch-off. The default is 0, and Home Assistant's numeric field does not store a blank value. The setting is saved across reboots. Changing it or turning the backlight on restarts the countdown; timeouts are checked once per second.

Touching a dark screen turns the backlight on. That entire first gesture is wake-only: it cannot activate a button or change pages, even with multiple fingers. Release, then touch again to operate the dashboard. The timer pauses while touching and restarts on release. Manually switching the backlight off also cancels any in-progress control gesture.

Turning the backlight off does not stop the dashboard or Home Assistant connection. A timeout of 0 still permits manual switch-off, with touch-to-wake available.

The saved **Wake-up page** dropdown defaults to **Last used page**, or can select **Page 1** through **Page 8** (the blueprint's fixed page slots). Existing saved choices retain their meaning. It applies whenever the backlight turns on, whether by touch, Home Assistant or reboot. Waking closes navigation before applying the preference; **Last used page** means the last button page, not the navigation screen. An empty selected page falls back to the first populated page. If no pages have arrived yet, the preference is applied when Home Assistant sends them. Changing the dropdown does not immediately navigate, and normal touches while the backlight is on do not change pages.

The **Restart** button in **Configuration** reboots the panel. The timeout and wake-up page settings are retained; the dashboard waits for Home Assistant to resend its configuration as usual.

Brightness adjustment is not supported by this connection: [ESPHome's CH422G driver](https://esphome.io/components/ch422g/) does not support PWM. A true brightness slider would require a hardware-supported dimming connection; no simulated slider is exposed.

### Screensaver

Install the updated firmware, re-import the updated blueprint and reload automations. Expand **Screensaver** in the blueprint and turn on **Enable screensaver** (off by default), then save/run the automation.

The device's Home Assistant **Configuration** section now includes **Screensaver time**, saved across reboots. Enter **0 to 86400 seconds**; the default is **60**, and **0** disables the screensaver. After inactivity, a full-screen view covers all page/header/footer/menu content with the shared background, a large centred 24-hour clock and the smaller date below it, for example **18:35** / **Saturday, 14 November 2026**. It uses the same Home Assistant-synchronized time as the header; before synchronization it shows **--:--** and **Waiting for time**.

The first complete touch gesture dismisses the screensaver without activating controls, applies the saved **Wake-up page**, and restarts inactivity. Release before operating controls. Changing **Screensaver time** also dismisses the screensaver and restarts inactivity. Periodic Home Assistant updates and disconnections do not dismiss it.

**Display timeout** remains independent: showing the screensaver does not restart the backlight countdown. For example, set **Screensaver time** to **60** and **Display timeout** to **300** for the clock after one minute and backlight-off after five minutes. A shorter or equal display timeout switches the backlight off first; a display timeout of **0** leaves it on for the screensaver. Both timers pause while touching and restart on release. Existing blueprints without the screensaver toggle keep it disabled.

### Example configuration

| Blueprint input | Office (page 01) | Landing (page 02) |
| --- | --- | --- |
| Page title | Office | Landing |
| Temperature sensor | `sensor.office_nspanel_temperature` | `sensor.landing_nspanel_temperature` |
| Button 1 | `light.office_lamp` | `light.landing_light` |
| Button 2 | `light.office_ceiling` | `automation.lighting_landing` |
| Button 3 | `light.office_cupboard_light` | Leave empty |
| Button 4 | `lock.office_office_door` | Leave empty |

### Development

The old static configuration is retained privately under `dev/`; it is no longer used by the public package. The firmware stores eight page models with eighteen button slots each, but uses one shared pool of eighteen LVGL tiles for the selected page. Hidden-page updates retain each entity's own last-known state and icon in the model, not in hidden widgets. This reduces the generated startup tree from 841 objects with eight duplicated control trees to 215, including the three screensaver widgets. LVGL's boot-time resolution/theme refresh traverses this smaller tree; watchdog settings are unchanged. [`scripts/generate_dashboard.py`](./scripts/generate_dashboard.py) generates this fixed widget scaffold, not user configuration:

```sh
python3 scripts/generate_dashboard.py
python3 scripts/generate_dashboard.py --check
python3 -m unittest discover -s scripts -p 'test_*.py'
```

The generator also maintains the MDI lookup header from the pinned [`scripts/mdi-icons.json`](./scripts/mdi-icons.json) catalogue. Use `python3 scripts/generate_dashboard.py --update-icons` to refresh that catalogue from the pinned upstream stylesheet. Change `MDI_VERSION` and regenerate together when upgrading the font. MDI licensing is recorded in [`MDI-LICENSE`](./esphome/components/wallpanel/MDI-LICENSE). The one-bit icon font keeps the complete library within flash without loading every icon bitmap into RAM.

The headless runtime test uses real ESPHome/LVGL with a no-output display driver. It checks startup widget/refresh-work bounds, independent per-page appearance retention, display idle/wake handling, screensaver timing/rollover/date formatting/wake-only gestures, rendered background/header/footer pixels, layout bounds, menu and swipe navigation, live page changes, dots, state labels, offline controls and rejection of invalid snapshots:

```sh
esphome compile scripts/runtime_tests/runtime.yaml
scripts/runtime_tests/.esphome/build/dynamic-host/.pioenvs/dynamic-host/program
python3 scripts/runtime_tests/check_native_api.py
```

The connected native API check also requires `aioesphomeapi` (included with ESPHome). It starts/stops its own local host process, requires port 16053 to be unused, and tests blueprint-generated 144-control snapshots, Unicode, partial-transfer atomicity and real entity actions through all three layouts. It runs with a 128 KiB host stack and checks bounded retained heap after repeated transfers.

The generator requires PyYAML; blueprint template tests also use Jinja2 (both are included with ESPHome). Version 2 uses eight page slots with per-page layout/icon metadata and exactly 4, 8 or 18 button slots. The runtime validates the entire snapshot before changing the UI; unsupported entities and malformed payloads are logged and rejected without replacing the previous configuration. Legacy version-1 four-page snapshots remain supported by `esphome.<device_name>_set_dashboard`.

The new blueprint appends `_chunk` to the discovered endpoint and sends ordered `set_dashboard_chunk` calls with a transfer ID, zero-based index, total and string payload. Each payload has a `~` text prefix followed by at most 4096 bytes of ASCII JSON; the complete snapshot is capped at 256 KiB/64 chunks. Incomplete transfers expire after 30 seconds of inactivity. Only a complete, validated snapshot replaces the UI. Chunking avoids ESPHome's 32 KiB native API message limit without modifying ESPHome internals.

Automated validation includes a 167,811-byte Unicode snapshot across 41 native API chunks. The eight-page shared-widget startup regression records 1,446 host LVGL refresh requests including the screensaver, below the unchanged 1,500-request limit used for the six-page build. The six-page shared-widget firmware and layouts have been confirmed on the panel; the restored eight-page build and screensaver still need device confirmation. Host heap/stack checks and firmware static RAM reports do not establish ESP32 runtime headroom: after installation, confirm successful boot, PSRAM/free heap, dense-label legibility and touch responsiveness, particularly with all 144 controls configured.

For local firmware development, set the `wallpanel_component_source` substitution to your local `esphome/components` directory (relative to the device YAML). Its default is `github://otghcloud/ha-esp32-s3-touch-lcd5@main`; if pinning a remote package to another revision, pin this source to the same revision.

## Notes

- The display uses `mipi_rgb` with the original 800 × 480 timings and data-bit mapping. GPIO46/GPIO3 strapping warnings are explicitly acknowledged for the board's fixed sync connections; do not add external pull resistors to these pins.
- The ESP-IDF configuration selects the `generic` flash driver reported by the target panel. If another hardware revision reports a different flash driver, override `esp32.framework.advanced.flash_chip` accordingly.
- The fallback access point and captive portal are not enabled.
- The public package does not currently implement `boot_sound` or `language` settings.
