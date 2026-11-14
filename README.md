# HA ESP32-S3 Touch LCD 5

ESPHome configuration for the Waveshare ESP32-S3-Touch-LCD-5 (800 × 480), with a touch dashboard connected to Home Assistant.

## Requirements

- Home Assistant with ESPHome Device Builder, or a standalone ESPHome installation.
- A Waveshare ESP32-S3-Touch-LCD-5.
- Home Assistant 2025.7 or newer, with entities to use for your controls (the blueprint uses native structured, draggable lists).

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
5. Expand **Button page 01** through **Button page 08**. Each provides a layout, page icon, optional temperature sensor and up to eighteen entity selectors with optional **Title** overrides. Choose **2x2**, **4x2** or **6x3** for 4, 8 or 18 controls respectively. Leave all active button slots empty to hide a page.
6. Under **Navigation > Page order and titles**, edit each row to set its page title, then drag the rows into the desired display order. Page slots still identify the corresponding numbered configuration sections.
7. Save and enable the automation. Run its actions once for an immediate update; future entity changes and reconnects update automatically. Keep only one configuration automation enabled per panel.

The **Dashboard endpoint** diagnostic sensor must remain enabled. It lets the blueprint discover the correct ESPHome action even if you rename the device's friendly name or HA entities.

Buttons use HA friendly names unless their **Title** field is filled in. Blank or whitespace-only titles retain the default name; titles without an entity do not create a button. Labels use at most 80 characters. Lights and switches toggle, automation buttons enable/disable (not trigger), and locks unlock when locked or lock when unlocked, without confirmation.

**Upgrade order:** install the updated firmware **first**, then re-import the updated blueprint and reload automations. Existing input IDs are preserved and new layouts default to 2x2. The new firmware still accepts the old four-page blueprint; older firmware cannot consume the new eight-page protocol.

The maximum is **eight button pages / 144 controls**, plus an optional Homepage, using the shared-widget renderer. When moving from the temporary six-page build, update both firmware and blueprint: version-2 snapshots now require eight page slots, and six-page snapshots are rejected without replacing the UI. Existing page 1-6 inputs and saved wake choices retain their meaning; new page 7/8 inputs default to empty.

### Home Assistant icons

The firmware includes the complete **Material Design Icons 7.4.47** font at 72 pixels for 2x2 tiles and 36 pixels for denser tiles. The blueprint sends each entity's `icon` attribute, so custom `mdi:` icons (including light, door, lock and automation variants) display on the panel. Without an explicit icon, buttons use Home Assistant's state-aware defaults: lightbulb/lightbulb-off, robot/robot-off, toggle-switch-variant/toggle-switch-variant-off, and locked/unlocked/jammed/transition lock icons. Outlet switches use power-plug/power-plug-off. Explicit glyphs take precedence, but their colours still follow entity state.

Active entity tiles are white with contrasting dark text; inactive tiles stay dark. Normally ON/enabled is active, while locks are active when **unlocked**. **Appearance > Reverse active state** selects entities that invert only this white-fill rule, consistently on pages and related controls. Actions and reported state labels are never reversed. Unknown/unavailable, transitional and disconnected controls never become white through reversal.

Icons retain amber for on/enabled and grey for off/disabled, with darker shades on white. Locks retain green for locked, red for unlocked/open/jammed and amber for transitions. Unknown/unavailable entities and disconnected controls are grey and dimmed. Lock transitions, jammed and open states remain non-actionable.

Each page's **Page icon** selector controls its icon beside the page name in navigation. Blank or unsupported page icons use `mdi:view-dashboard`; unsupported icons also log a warning.

This matches entity icons, not icon overrides defined only in individual Lovelace cards. Non-MDI icon packs and icons added after 7.4.47 are not supported; the panel logs a warning and uses a default instead. Door icons are available for existing controls; this does not add door sensors or cover controls.

The full icon library requires the board's 16 MB flash layout; existing installations built with the old default 4 MB layout need a **USB/manual factory flash**, because OTA cannot replace the partition table. Subsequent updates can use OTA. Icons are included during compilation; the panel does not download them at runtime.

### Layout and page navigation

Each button page has a 56-pixel black header with its title on the left and the clock on the right. A 48-pixel grey (`#999999`) footer contains the optional page temperature, centred page dots, a Settings cog and a bottom-right menu icon. Long titles and temperature values are truncated to avoid overlapping neighbouring controls. Blank or whitespace-only page titles display as **Page N**, using the blueprint slot number.

Each button page independently selects its grid:

| Layout (columns x rows) | Active input slots | Tile content |
| --- | --- | --- |
| 2x2 | 1-4 | Icon, title and state |
| 4x2 | 1-8 | Icon, title and state |
| 6x3 | 1-18 | Icon and title; unavailable controls are dimmed/disabled |

Populated slots compact in configured order, with incomplete rows centred. A smaller layout does **not** delete higher-numbered settings: they are ignored with a Home Assistant warning until a larger layout is selected again. Ignored slots do not enable a page or generate watched-entity updates.

Use **Navigation > Page order and titles** to reorder pages with Home Assistant's native drag handles. Edit a row with its pencil button to set **Page title**; the row displays that title with its fixed page slot underneath. Keep the **Page slot** unchanged when moving a row: it links to the original page's entities, layout, icon and temperature. Menu positions, swipe order and page dots follow the row order, while wake selections retain their original slot identities. Reordering does not jump away from the currently displayed page. Unlisted pages follow in numeric slot order; removing a row does not hide or delete its page. Empty pages are always skipped. Duplicate or invalid slots log an error and retain the previous dashboard.

**Existing title migration:** Home Assistant cannot dynamically populate a row label from another blueprint input. Existing **Legacy page title** inputs keep their IDs and continue supplying panel titles when a row has no title field. Copy an existing name once into its row to display it in the draggable list; after that, edit the row title rather than maintaining both fields. Explicitly clearing a row title uses **Page N** on the panel. Install firmware first, then re-import the updated blueprint and reload automations; older blueprints retain numeric page order.

Tap the menu icon to open **Navigation**, then tap a page button to select it. Only populated pages appear, in the configured order (numeric slot order by default), using the independent **Navigation > Menu layout** setting. Page buttons show their configured icons and titles; duplicate titles still select separate slots. More than four populated pages require a **4x2** or **6x3** menu; all eight button pages plus Homepage require **6x3**. An invalid menu configuration logs an error and retains the previous dashboard (or waiting screen), rather than hiding pages or adding scrolling/pagination. Tap the menu icon again to return without changing the current page. Navigation hides page-specific temperatures and dots, and ignores swipes.

Swipe left/right anywhere on a button page, including over buttons. Navigation wraps at both ends and skips empty pages. A horizontal movement of at least 100 pixels changes page when you lift your finger. Buttons activate only on release after a tap with no movement beyond 20 pixels; drags and multi-touch gestures do not activate controls. Bottom-centred dots highlight the current page; they are hidden for a single page. Temperature values use the sensor's unit of measurement.

After reboot the panel shows **Waiting for Home Assistant** until it receives configuration. During a disconnection it retains its layout and displays unavailable indicators with disabled controls. No configuration is saved to flash. The blueprint sends current configuration/states on selected entity changes, reconnect events and every 30 seconds; the panel also disables controls if updates stop for 65 seconds.

Page navigation remains available offline using the last known configuration. Renamed, added or removed pages update the menu automatically. With no populated pages, the menu is disabled and the panel shows its waiting/no-pages message. The header and footer remain visible on these screens.

The static 800 × 480 background in [`assets/background_dark.png`](./assets/background_dark.png) is embedded in the firmware and visible behind the content area of every view, including waiting and navigation.

The darker artwork was verified byte-for-byte against generated RGB565 images in local and independently resolved published-package builds. Compare exposed areas between tiles or the screensaver: inactive tiles are 90% opaque, and active white tiles are opaque. If artwork appears stale, allow the package/component 300-second refresh windows to expire, use **Clean Build Files**, rebuild and check the installed compilation time. These checks do not establish which firmware is currently on your device.


Changing layouts, page icons or controls after the firmware/blueprint upgrade does not require recompilation.

### Light details and related controls

Install the firmware first, re-import the updated blueprint and reload automations. Short taps still toggle. **Hold a light's icon for 600 ms** to open its details; moving more than 20 pixels or adding another finger cancels the hold. Opening details consumes that entire gesture, so release before using a control. Holding the title/body does not open details.

The small icons in 4x2/6x3 layouts have a padded hold region targeting at least 72 pixels per axis, clipped by the tile and excluding its title. A tile's pressed/hover appearance alone does not mean the separate icon hold is armed. If a hold does not open details, DEBUG logs now distinguish **Light hold armed**, **outside icon area**, cancellation by movement/additional touch/layout changes, release before opening, and **Opening light details**. A first gesture after sleep/screensaver remains wake-only and logs **Touch reserved for wake**: release before operating controls. Bulb capability metadata controls which editors appear, not whether an available light can open its detail page. This hold-target fix requires updated firmware; no blueprint input changes are needed.

The left three quarters contain Power and the supported brightness, temperature and colour controls. Select a mode at the right of that area. The HS wheel changes hue/saturation; colour-capable RGB/XY lights use Home Assistant's conversion. Temperature appears only when the light advertises colour-temperature support and a valid Kelvin range. Missing bounds show **Temperature range unavailable**; missing readings remain **Unknown**. On/off-only lights show Power without invented sliders. Older snapshots or lights without capability information still offer Power with an explanatory status.

Drag to preview locally; **one command is sent on release**, not continuously. Brightness zero calls `light.turn_off`; other brightness, temperature and colour edits call `light.turn_on`, including when the light is off. Incoming state updates do not overwrite an active drag. The view waits for reported confirmation, then returns to authoritative state; an unconfirmed change shows a warning after five seconds. Offline/unavailable controls cannot send commands. Capability changes, entity removal, manual backlight-off, wake/screensaver transitions and OTA cancel edits safely.

Under **Light details > Related controls**, add a structured row for a primary light and select up to two related **light, switch or automation** entities, with optional titles. They occupy the right-hand quarter and behave as ordinary toggles. Related entities need no main-page tile and are watched for updates automatically. Shared entities are transmitted once; title overrides remain specific to each binding. No row is required merely to enable normal light details. Existing automation tiles are not removed automatically: clear them yourself if replaced by related controls. Rows for lights outside the active layouts are retained but inactive.

Back returns to the source content page. Details never enter page ordering, dots, wake choices or last-used content identity. The footer menu/cog remain available. Configurable short-tap actions, continuous/throttled drag commands, related sensors/energy readings, HA `button` entities, four related controls and non-light detail pages are deferred.

### Firmware updates

OTA uploads show a static **Updating firmware** overlay. The frame is flushed before flash writes and LVGL is paused without animation; touches, commands, clock redraws, wake/idle transitions and incoming dashboard transfers are blocked. A previously dark display stays dark. Successful uploads remain paused until the normal reboot. On abort/error, the prior view/backlight/manual-sleep policy is restored, inactivity deadlines restart and the panel requests fresh configuration rather than replaying partial transfers.

Pausing LVGL does **not** stop the RGB controller scanning its PSRAM framebuffer. Physical upload testing is still required: a static frame alone cannot guarantee that flash/PSRAM-related corruption is eliminated. If corruption persists, set this device-YAML substitution and rebuild:

```yaml
substitutions:
  ota_blank_display: "true"
```

The fallback blanks the backlight for the upload and restores its previous state on abort/error. The default is `"false"`. Display timings and watchdog settings are unchanged.

### Homepage and Settings

Enable **Homepage > Enable Homepage** in the blueprint to add a placeholder view with centred **Homepage** text and the shared background/header/footer. It is off by default, is additional to the eight button pages, and works even when no button pages are configured.

Homepage participates in the menu, swipes and page dots. It comes first unless a **Homepage** row in **Navigation > Page order and titles** places it elsewhere. Existing automations can add that row; its optional title changes the header/menu label, not the placeholder text. Disabling Homepage retains its ordering input and safely returns to another available page.

If an earlier blueprint reports `int got invalid input 'home'`, update the installed blueprint and reload automations. The title lookup must compare page-slot strings, not convert the Homepage slot to a number. This blueprint-only fix needs no firmware rebuild or changes to your automation inputs.

Tap the footer cog for **Settings**. **Restart** immediately reboots the panel, without confirmation; **Sleep** immediately turns the backlight off. These are local actions and work without Home Assistant, including before any pages arrive. Settings has no page dots, is excluded from swipes/order/wake choices, and never replaces the last content page. The menu icon opens Navigation from Settings; touching a sleeping panel wakes only, as below.

### Display backlight

After installing the updated firmware, the device's **Configuration** section in Home Assistant includes a **Display backlight** switch. It controls the board's active-high CH422G EXIO2 backlight enable and turns on after every reboot. No blueprint update is needed.

The **Display timeout** setting in the same section turns the backlight off after inactivity. Enter seconds from **0 to 86400** (24 hours); **0** disables automatic switch-off. The default is 0, and Home Assistant's numeric field does not store a blank value. The setting is saved across reboots. Changing it or turning the backlight on restarts the countdown; timeouts are checked once per second.

Touching a dark screen turns the backlight on. That entire first gesture is wake-only: it cannot activate a button or change pages, even with multiple fingers. Release, then touch again to operate the dashboard. The timer pauses while touching and restarts on release. Manually switching the backlight off also cancels any in-progress control gesture.

Turning the backlight off does not stop the dashboard or Home Assistant connection. A timeout of 0 still permits manual switch-off, with touch-to-wake available.

The saved **Wake-up page** dropdown defaults to **Last used page**, or can select **Page 1** through **Page 8** (the blueprint's fixed page slots), **Navigation / menu**, or **Homepage**. New options are appended, preserving saved indices. It applies whenever the backlight turns on, whether by touch, Home Assistant or reboot, and when dismissing the screensaver. **Navigation / menu** opens the page-selection menu; other choices close it. **Last used page** means the last button page or Homepage, never Navigation or Settings. An empty/disabled selected page falls back to the first available content page in display order. If no pages have arrived yet, the panel retains its waiting screen and applies the preference when Home Assistant sends populated pages. Changing the dropdown does not immediately navigate, and normal touches while the backlight is on do not change pages. Install the updated firmware to add the Homepage option; enable Homepage through the updated blueprint to use it.

The **Restart** button in **Configuration** reboots the panel. The timeout and wake-up page settings are retained; the dashboard waits for Home Assistant to resend its configuration as usual.

Brightness adjustment is not supported by this connection: [ESPHome's CH422G driver](https://esphome.io/components/ch422g/) does not support PWM. A true brightness slider would require a hardware-supported dimming connection; no simulated slider is exposed.

### Screensaver

Install the updated firmware, re-import the updated blueprint and reload automations. Expand **Screensaver** in the blueprint and turn on **Enable screensaver** (off by default), then save/run the automation.

The device's Home Assistant **Configuration** section now includes **Screensaver time**, saved across reboots. Enter **0 to 86400 seconds**; the default is **60**, and **0** disables the screensaver. After inactivity, a full-screen view covers all page/header/footer/menu content with the shared background, a large centred clock and the smaller date below it, by default **18:35** / **Saturday, 14 November 2026**. It uses the same Home Assistant-synchronized time as the header; before synchronization it shows **--:--** and **Waiting for time**.

The first complete touch gesture dismisses the screensaver without activating controls, applies the saved **Wake-up page**, and restarts inactivity. Release before operating controls. Changing **Screensaver time** also dismisses the screensaver and restarts inactivity. Periodic Home Assistant updates and disconnections do not dismiss it.

**Display timeout** remains independent: showing the screensaver does not restart the backlight countdown. For example, set **Screensaver time** to **60** and **Display timeout** to **300** for the clock after one minute and backlight-off after five minutes. A shorter or equal display timeout switches the backlight off first; a display timeout of **0** leaves it on for the screensaver. Both timers pause while touching and restart on release. Existing blueprints without the screensaver toggle keep it disabled.

### Date/time formats and wake sensors

The blueprint's **Date and time** section offers 24-hour or 12-hour AM/PM time, plus long date, weekday with DD/MM, DD/MM/YYYY, MM/DD/YYYY, ISO YYYY-MM-DD or abbreviated-month dates. Defaults are unchanged. Header and screensaver update together; the header uses a smaller font for AM/PM to avoid overlap. Names remain English; custom formats, temperature conversion and timezone selection are not included.

Under **Wake sensors > Wake-up sensors**, select any number of binary sensors. A genuine **OFF → ON** transition wakes the backlight/screensaver and uses the saved wake route; an already-awake panel does not jump pages. While any selected sensor is ON, both automatic backlight-off and screensaver entry are suspended. When the last turns OFF, both inactivity deadlines restart. Unknown/unavailable values are not ON, and attribute-only changes do not count as activations.

Manual **Sleep** or HA backlight-off takes priority over an existing ON sensor. Periodic snapshots cannot undo it; a fresh OFF → ON transition or touch releases that override. A reconnect snapshot can restore an active hold and wake an automatically timed-out panel, but cannot undo manual Sleep. Connection loss or 65 seconds without valid snapshots releases the hold, so the display cannot remain held awake by stale sensor state. Install firmware first, then re-import the blueprint and reload automations.

### Example configuration

| Blueprint input | Office (page 01) | Landing (page 02) |
| --- | --- | --- |
| Page title | Office | Landing |
| Temperature sensor | `sensor.office_nspanel_temperature` | `sensor.landing_nspanel_temperature` |
| Button 1 | `light.office_lamp` | `light.landing_light` |
| Button 2 | `light.office_ceiling` | `automation.lighting_landing` |
| Button 3 | `light.office_cupboard_light` | Leave empty |
| Button 4 | `lock.office_office_door` | Leave empty |

### Activity and diagnostic logging

The dashboard automation runs on watched entity changes, panel-ready/reconnect events and a 30-second heartbeat. Its **Triggered / Scheduled / Via Light / Manual event received** Activity entries are execution history, not errors. The heartbeat is still required by the 65-second stale-configuration check; do not remove it or simply increase its interval without changing that protocol.

To hide only the transport automation from HA Activity, add this to your Home Assistant configuration, merging with any existing `logbook` section, then check configuration and restart HA:

```yaml
logbook:
  exclude:
    entities:
      - automation.office_devpanel_test
```

Replace the example with the automation's actual entity ID from Settings or Developer Tools; its friendly name is not necessarily its entity ID. This is HA's [Activity filter](https://www.home-assistant.io/integrations/logbook/#configure-filter), not a blueprint option. It does not disable the automation, its traces, or genuine light/door activity, and does not change Recorder retention. No HA configuration is modified by this repository.

**Dashboard endpoint** is a constant diagnostic address used to discover the panel's native API action; it is not a heartbeat. Firmware publishes it once at boot and ESPHome retains that state for HA subscribers, including reconnects. Ready events and dashboard heartbeats remain separate.

Unchanged snapshots still refresh the freshness deadline and sensor-hold policy, but no longer reapply all LVGL styles/labels or redraw the clock. Changed configuration/state, reconnection, localization and pending-command confirmation continue updating the UI.

ESPHome **took a long time for an operation** warnings mean a component blocked the cooperative loop, potentially delaying touch/network handling. The reported **max** is the current warning threshold, not a watchdog timeout. Full redraws, glyph rendering and display transfers can be expensive on this RGB/PSRAM panel; these logs alone do not identify their precise cause. Skipping unchanged-heartbeat redraws removes avoidable work, not the warning mechanism. Persistent 600 ms operations during ordinary interaction still warrant physical profiling; watchdogs and warning thresholds are not relaxed.

### Development

The old static configuration is retained privately under `dev/`; it is no longer used by the public package. The firmware stores eight page models with eighteen button slots each, but shares one eighteen-tile LVGL pool between entity pages, Navigation, Settings and light details, plus one nine-dot strip and a single detail editor. Hidden-page updates retain each entity's own last-known state and icon in the model, not in hidden widgets. The tree has 155 actual screen/top-layer objects, below the unchanged 220-object limit (versus 841 with duplicated controls), with 1016 measured startup refresh requests below the unchanged 1500-request limit. Watchdog settings are unchanged. [`scripts/generate_dashboard.py`](./scripts/generate_dashboard.py) generates this fixed widget scaffold, not user configuration:

```sh
python3 scripts/generate_dashboard.py
python3 scripts/generate_dashboard.py --check
python3 -m unittest discover -s scripts -p 'test_*.py'
```

The generator also maintains the deterministic 240 × 240 HS wheel in [`assets/light_colour_wheel.png`](./assets/light_colour_wheel.png) and the MDI lookup header from the pinned [`scripts/mdi-icons.json`](./scripts/mdi-icons.json) catalogue. The wheel uses standard-library PNG encoding, not a dynamic canvas or another runtime dependency. Use `python3 scripts/generate_dashboard.py --update-icons` to refresh the catalogue from the pinned upstream stylesheet. Change `MDI_VERSION` and regenerate together when upgrading the font. MDI licensing is recorded in [`MDI-LICENSE`](./esphome/components/wallpanel/MDI-LICENSE). The one-bit icon font keeps the complete library within flash without loading every icon bitmap into RAM.

Page builders live in [`scripts/dashboard_pages/`](./scripts/dashboard_pages/); runtime models, interaction, shared geometry and page-specific renderers live in [`esphome/components/wallpanel/`](./esphome/components/wallpanel/). The coordinator retains atomic snapshot validation, transport and explicit view routing. Screensaver is an overlay, not a content route; entity slot identity remains independent of visual ordering. Keep package `on_boot` automations as lists so page initialization and local hardware/clock bindings compose rather than override each other.

The headless runtime test uses real ESPHome/LVGL with a no-output display driver. It checks startup widget/refresh-work bounds, independent per-page appearance retention, display idle/wake handling, screensaver timing/rollover/date formatting/wake-only gestures, rendered background/header/footer pixels, layout bounds, menu and swipe navigation, live page changes, dots, state labels, offline controls and rejection of invalid snapshots:

```sh
esphome compile scripts/runtime_tests/runtime.yaml
scripts/runtime_tests/.esphome/build/dynamic-host/.pioenvs/dynamic-host/program
python3 scripts/runtime_tests/check_native_api.py
```

The connected native API check also requires `aioesphomeapi` (included with ESPHome). It starts/stops its own local host process, requires port 16053 to be unused, and tests blueprint-generated 144-control snapshots, Unicode, partial-transfer atomicity, real entity actions through all three layouts, Homepage, local Settings actions, clock format changes, sensor wake/manual precedence and hold expiry. Batch 3 checks also cover hold consumption/cancellation, reusable detail geometry, white/reversed appearance, atomic metadata rejection, OTA gates/recovery, typed numeric/HS-list service envelopes, release-only edits, confirmation timeout, related toggles and primary-entity removal. Host Restart/backlight actions are test callbacks, not real restarts or hardware output. It runs with a 128 KiB host stack and checks bounded retained heap after repeated transfers.

The generator requires PyYAML; blueprint template tests also use Jinja2 (both are included with ESPHome). Version 2 uses eight page slots with per-page layout/icon metadata and exactly 4, 8 or 18 button slots. The optional root `page_order` is a permutation of zero-based slot indices 0-7, separate from the unchanged `pages` array; omission retains legacy numeric ordering. The blueprint expands partial user orders to a complete permutation. The runtime validates the entire snapshot before changing the UI; unsupported entities and malformed payloads are logged and rejected without replacing the previous configuration. Legacy version-1 four-page snapshots remain supported by `esphome.<device_name>_set_dashboard`.

Optional snapshot fields are `homepage: {enabled, position, title}` (insertion position 0-8 among ordered entity slots), `localization: {time, date}` (allowlisted preset IDs), and boolean `wake_hold`. Omission disables Homepage/hold and restores the default formats. The separate `esphome.<device_name>_wake_display` action represents a fresh sensor activation; the blueprint sends it before the snapshot transfer so a later attribute/periodic update cannot cancel it mid-transfer. Local manual-sleep precedence is not stored in HA snapshots.

Batch 3 adds optional `reverse_active` (unique entity IDs), `light_details` (unique primary-light capability/readings/bindings), and `detail_entities` (deduplicated related entity/name/state/icon records). Detail readings include normalized `colour_mode`, brightness 0–255, HS values and Kelvin bounds/value; missing readings use -1 rather than a manufactured setting. Up to two related entity references and optional titles belong to each primary binding. Omitting these fields preserves legacy operation, without reversal or invented capabilities. Metadata is validated with the rest of the snapshot before an atomic replacement. Outgoing brightness/Kelvin values and HS lists use native HA `data_template` expressions; they are not quoted string-valued colour arrays.

The new blueprint appends `_chunk` to the discovered endpoint and sends ordered `set_dashboard_chunk` calls with a transfer ID, zero-based index, total and string payload. Each payload has a `~` text prefix followed by at most 4096 bytes of ASCII JSON; the complete snapshot is capped at 256 KiB/64 chunks. Incomplete transfers expire after 30 seconds of inactivity. Only a complete, validated snapshot replaces the UI. Chunking avoids ESPHome's 32 KiB native API message limit without modifying ESPHome internals.

If the panel remains on **Waiting for Home Assistant** with **Invalid dashboard chunk** followed by **Out-of-order, expired or oversized dashboard transfer**, update/re-import the blueprint, reload automations and run the dashboard automation once. The blueprint explicitly uses `to_json(ensure_ascii=True)`: HA otherwise preserves Unicode, so a 4096-character fragment containing degree signs, accents or emoji can exceed the firmware's 4096-byte JSON limit. Escaping preserves the original displayed text while keeping character and byte counts equal. This blueprint-only fix needs no firmware flash or larger transfer limits.

Automated validation includes a 225,220-byte Unicode snapshot with light metadata across 55 native API chunks, within the unchanged 256 KiB cap. Oversized configurations log an actionable HA error and send no partial model; shorten labels or reduce related controls rather than raising the cap. The Batch 3 production build with the padded hold-target and heartbeat-rendering follow-ups uses 6,375,108 bytes flash (78.4%) and 124,676 bytes static RAM (36.5%). Host heap/stack checks and firmware static RAM reports do not establish ESP32 runtime headroom: after installation, confirm successful boot, PSRAM/free heap, dense-label legibility, light controls and touch responsiveness, particularly with all 144 controls configured. Physical OTA stability and abort/error recovery also need panel confirmation.

For local firmware development, set the `wallpanel_component_source` substitution to your local `esphome/components` directory (relative to the device YAML). Its default is `github://otghcloud/ha-esp32-s3-touch-lcd5@main`; if pinning a remote package to another revision, pin this source to the same revision.

## Notes

- The display uses `mipi_rgb` with the original 800 × 480 timings and data-bit mapping. GPIO46/GPIO3 strapping warnings are explicitly acknowledged for the board's fixed sync connections; do not add external pull resistors to these pins.
- The ESP-IDF configuration selects the `generic` flash driver reported by the target panel. If another hardware revision reports a different flash driver, override `esp32.framework.advanced.flash_chip` accordingly.
- The fallback access point and captive portal are not enabled.
- The public package does not currently implement `boot_sound` or `language` settings.
