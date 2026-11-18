# Advanced Configuration and Development

For the short installation guide and everyday controls, start with [README.md](./README.md). This guide covers detailed configuration, diagnostics, architecture, protocol and validation.

## Firmware and deployment

The [installation guide](./README.md#1-install-the-firmware) uses matching published release assets. The release's example pins the package and dashboard component to the same immutable tag. If upgrading ESPHome from 2026.10 to 2026.11, use a Wallpanel revision with the light-action compatibility fix in both references; refreshing a pre-fix tag such as `v2.0.0` will not pick it up. Initial builds need internet access for dependencies, Roboto and Material Design icons. Keep Wi-Fi/API credentials in private ESPHome secrets.

The firmware and native tests use the dedicated visibility APIs in LVGL 9.6 (ESPHome 2026.11), with a flag-based fallback for LVGL 9.5 (ESPHome 2026.10).

Home Assistant's **Device info** identifies the panel as **HA Panel LCD5** by **OTGH Cloud**, using `esphome.project` in [`esphome/core.yaml`](./esphome/core.yaml). The initial release baseline is **0.0.1**. Keep the firmware project version aligned with its release tag (for example, `0.0.1` for `v0.0.1`). Firmware and blueprint belong to the same release; the dashboard protocol's independent version number is **4**.

### Release process and CI

The [release workflow](./.github/workflows/release.yml) owns version generation. **Do not edit `VERSION`, bump generated version fields, or create release tags manually.** Stable tags are the version history; [`VERSION`](./VERSION), the ESPHome project-version include, runtime version header, blueprint release variable and tag-pinned `source_url` are generated together for each release.

Use **Conventional Commits** for PR titles and every new non-merge commit:

```text
fix(touch): Preserve the first contact
feat(ui): Add another control
refactor(api)!: Remove the old endpoint
```

Allowed types are `feat`, `fix`, `perf`, `revert`, `docs`, `style`, `refactor`, `test`, `build`, `ci` and `chore`, with an optional lowercase scope. CI checks the PR title and all introduced commits, and checks new commits again on pushes to `main` and merge queues. Existing legacy history is not retroactively rejected. Dependabot uses valid `ci`/`build` prefixes; it is not exempt. Git-generated merge commits are excluded.

Configure **squash merging only**, using the **PR title and description** as the default squash message, and require **Required checks** in `main` branch protection. This preserves the reviewed title and any `BREAKING CHANGE:` footer in the actual main commit. Mark breaking changes with `!` after the type/scope or a `BREAKING CHANGE:` / `BREAKING-CHANGE:` body footer.

To publish, merge the changes normally, then open **Actions > Release > Run workflow**, selecting **main**:

- Leave **bump_override** on `auto` and **version** empty for automatic calculation.
- Since the previous release, any breaking change selects **major**, otherwise any `feat` selects **minor**, otherwise **patch** (including docs/dependency-only changes).
- The first automatic release establishes **0.0.1**, including the pre-pipeline legacy history.
- Optionally select `patch`, `minor` or `major` to force a bump, **or** enter an exact `X.Y.Z` with the bump left on `auto`. Exact versions must exceed every existing stable tag.
- With no new commits, `auto` stops without publishing; an explicit bump/version can intentionally publish a version-only release, identified as such in its notes.
- Stable versions only: no `v` prefix in the input, leading zeroes, prerelease/build suffixes, or segments larger than 32 bits. Breaking changes select major even during `0.x`; an intentional override is the maintainer's responsibility.

The workflow pins the dispatch's main commit and calculates the version from all non-merge commits since the preceding release, not just the latest commit/PR. It re-runs the complete CI suite with the generated version, then creates a **release-only metadata commit** whose parent is that tested main commit. Only generated version files may differ. The tag points to this commit, so tagged source archives, firmware and blueprint metadata all agree. **No branch is pushed or changed**, and protected `main` needs no bypass. The files on `main` remain the development/bootstrap metadata, not a record of the latest published version; use release tags for that. This is intentionally different from committing version bumps back to `main`.

The workflow generates grouped release notes directly from commit messages (including breaking-change bodies), prepares assets and SHA-256 checksums, uploads a complete draft, then publishes it as latest. It does not publish on merge or on a manually pushed tag. Release runs are serialized; a newer tag appearing during validation fails the run rather than reusing a stale version.

Tags/releases must never be moved or overwritten. A failed push/upload/publish may leave an unpublished tag or draft. Inspect the failed run and remove only that unpublished draft/tag before restarting; never delete or overwrite a published release. Fix published errors with another version.

Release assets are the blueprint, a tag-pinned device example, `version.json` and `SHA256SUMS`, alongside GitHub's tagged source archives. Build firmware locally with your own Wi-Fi/API secrets. The CI firmware uses dummy credentials and is retained for seven days as **validation-only** artifacts with its MDI licence; it is not a distributable device image or an automatic OTA update.

The [CI workflow](./.github/workflows/checks.yml) runs on PRs to `main`, pushes to `main`, merge queues and manual dispatch, and is reused for releases. It validates metadata, generated assets, blueprint/release tests, Python/YAML/workflow lint, real LVGL/native API behaviour and a full ESP32 build. Protect `main` with PR-only updates (including admins), the GitHub Actions **Required checks** status with an up-to-date branch, resolved review conversations, linear history and no force pushes/deletion. Zero mandatory approvals allows solo maintenance without allowing direct pushes. No path filters omit checks on dependency PRs. [Dependabot](./.github/dependabot.yml) proposes weekly GitHub Actions and pinned Python/ESPHome updates; these receive the same complete checks, without automatic merging.

Every executing job uses `runs-on: [self-hosted, ubuntu-latest]`. These are **runner labels**, not a GitHub-hosted VM request or `uses` parameters. Grant this repository access to runners with both labels. Use current GitHub Actions runners supporting Node 24; provide Git, GitHub CLI, curl, tar, sha256sum, Python 3/venv support, a C/C++ toolchain, CMake, Ninja, pkg-config and FreeType/OpenSSL development libraries. The metadata job downloads actionlint 1.7.12 for Linux x64/ARM64, verifies the archive against a pinned SHA-256 checksum and installs it under `RUNNER_TEMP`; it does not depend on a preinstalled linter or alter the runner image. The setup action installs Python 3.14 and the pinned tools from [requirements.txt](./.github/requirements.txt) into an isolated job environment. Builds need outbound access to GitHub, PyPI, ESPHome/ESP-IDF dependencies and font hosts, plus sufficient RAM/disk for two concurrent compiler processes. Native tests need an isolated/free localhost port 16053.

Use ephemeral, isolated self-hosted runners without production secrets or LAN access. Fork PR build jobs are skipped and **Required checks** fails until the work is reviewed and moved to a trusted in-repository branch. Also configure GitHub to require approval for **all** external-fork workflow runs: workflow guards alone are not a security boundary because contributors can edit workflow files. Restrict changes to workflows/release assets through review. If tag rulesets restrict `v*` creation, allow the release workflow's identity to create tags; do not permit moving published tags. Only the publish job receives a write token; checkouts do not persist credentials, and push credentials are supplied only for the tag operation. Publishing is not exercised by local tests.

Conventional Commit/manual-release patterns follow [otghcloud/laravel-edgelink](https://github.com/otghcloud/laravel-edgelink), [otghcloud/aurora-gha-manager](https://github.com/otghcloud/aurora-gha-manager) and [otghcloud/aurora-manage](https://github.com/otghcloud/aurora-manage). Firmware validation patterns also reference [otghcloud/nspanel-ha](https://github.com/otghcloud/nspanel-ha) and [edwardtfn/NSPanel-Easy](https://github.com/edwardtfn/NSPanel-Easy).

### Release notifications

Firmware advertises a diagnostic **Firmware release** state such as `wallpanel/0.0.1`; its prefix lets the blueprint discover it even if the entity is renamed. **Release status** distinguishes waiting for configuration, mismatched firmware/blueprint versions, unavailable latest-release data, newer stable releases, matching releases and development builds ahead of stable. A protocol-compatible mismatch warns without blocking controls; unsupported protocol versions still reject the complete snapshot.

For newer-release checks, install the shared [Home Assistant package](./home_assistant/wallpanel_releases.yaml) once, for example:

```yaml
homeassistant:
  packages:
    wallpanel_releases: !include packages/wallpanel_releases.yaml
```

Copy that file into `/config/packages/wallpanel_releases.yaml`, merge with your existing packages configuration, check HA configuration and restart. In each dashboard blueprint, select the resulting **Wallpanel latest release** sensor under **Release checks**. HA fetches the latest published stable manifest over verified HTTPS every six hours; panels do not make internet requests. Before the first release, or if the request fails, HA reports unavailable data rather than asserting that firmware is current. Malformed/unknown sensor versions are not used.

The blueprint sends the latest known version with each snapshot and creates one persistent notification per panel for a mismatch or newer release. It updates/dismisses that notification on relevant version/device events or manual execution, not every heartbeat. No automatic firmware downloads or installation, watchdog changes or display-layout changes are introduced.

## Dashboard configuration

Pages and entities are configured at runtime by the [Home Assistant blueprint](./wallpanel_blueprint.yaml), not baked into firmware. Install the updated firmware once, then:

1. Add the device to the ESPHome integration in Home Assistant.
2. Open the device's ESPHome integration configuration and enable **Allow the device to perform Home Assistant actions**.
3. Under **Settings > Automations & scenes > Blueprints > Import blueprint**, import:

   Use the `source_url` from the blueprint in your chosen release. Before publication, copy the local `wallpanel_blueprint.yaml` to `/config/blueprints/automation/otghcloud/wallpanel_blueprint.yaml` and reload automations.

4. Create an automation from **Waveshare Touch LCD HA-Panel** and select your panel device.
5. Expand **Button page 01** through **Button page 08**. Each provides Enabled, layout, Compact and center buttons, page icon, optional temperature sensor and eighteen entity selectors with optional **Title** and **Tap action** settings. Auto is the default; manual grids remain available. Disable a page to hide it without clearing its inputs, or leave all active slots empty.
6. Under **Navigation > Page order and titles**, edit each row to set its page title, then drag the rows into the desired display order. Page slots still identify the corresponding numbered configuration sections.
7. Save and enable the automation. Run its actions once for an immediate update; future entity changes and reconnects update automatically. Keep only one configuration automation enabled per panel.

The **Dashboard endpoint** diagnostic sensor must remain enabled. It lets the blueprint discover the correct ESPHome action even if you rename the device's friendly name or HA entities.

Buttons use HA friendly names unless their **Title** field is filled in. Blank or whitespace-only titles retain the default name; titles without an entity do not create a button. Labels use at most 80 characters. Lights and switches toggle, automation buttons enable/disable (not trigger), and locks unlock when locked or lock when unlocked, without confirmation. Scripts always execute `script.turn_on`, even while already running; HA owns single/restart/queued/parallel behavior. Script state off/on displays IDLE/RUNNING. There is no script stop action or parameter editor.

The maximum is **eight button pages / 144 controls**, plus an optional Homepage, using the shared-widget renderer. Use firmware and blueprint from the same release. Install firmware first, then import the matching blueprint and reload automations.

Protocol 4 requires a coordinated firmware/blueprint update: earlier protocols are rejected, and older firmware rejects protocol 4 rather than ignoring indicator/lock metadata. Replace the blueprint at the existing automation's path, retain its inputs, reload automations and run it once. New indicator inputs default to unset and lock bindings default to empty; existing behavior and saved `details` tap choices remain. Enabled/Compact still default on and omitted layouts use Auto. Check Navigation capacity when upgrading from a pre-Auto release.

Each numbered button can optionally use another **State indicator entity** and a list of **Active states** (default `on`). Entries are exact, case-sensitive raw HA states, for example `playing` or `unlocked`, not translated UI labels. Indicator states over 1024 UTF-8 bytes produce a configuration error and block transfer rather than being silently truncated. The source supplies displayed state and standard active/inactive presentation; the icon domain and command target remain the controlled entity. Reverse active still changes only the white-fill rule. Unknown/unavailable sources explicitly show UNAVAILABLE and never fall back silently to the actor's state. They do not disable an otherwise available actor; an active source likewise cannot enable an unavailable actor. Detail readings and lock command decisions always use actual actor data. Related controls have no independent overrides.

Indicator-only sources may be sensors or other non-actor entities. Only active numbered slots introduce their subscriptions. Controlled targets are tracked separately from all watched sources, so an indicator-only light cannot activate unrelated light detail bindings.

### Home Assistant icons

The firmware includes the complete **Material Design Icons 7.4.47** font at 72 pixels for 2x2 tiles and 36 pixels for denser tiles. The blueprint sends each entity's `icon` attribute, so custom `mdi:` icons (including light, door, lock and automation variants) display on the panel. Without an explicit icon, buttons use Home Assistant's state-aware defaults: lightbulb/lightbulb-off, robot/robot-off, toggle-switch-variant/toggle-switch-variant-off, and locked/unlocked/jammed/transition lock icons. Outlet switches use power-plug/power-plug-off. Explicit glyphs take precedence, but their colours still follow entity state.

Active entity tiles are white with contrasting dark text; inactive tiles stay dark. Normally ON/enabled is active, while locks are active when **unlocked**. **Appearance > Reverse active state** selects entities that invert only this white-fill rule, consistently on pages and related controls. Actions and reported state labels are never reversed. Unknown/unavailable, transitional and disconnected controls never become white through reversal.

Without an independent indicator, icons retain amber for on/enabled and grey for off/disabled, with darker shades on white. Locks retain green for locked, red for unlocked/open/jammed and amber for transitions. Unknown/unavailable entities and disconnected controls are grey and dimmed. Lock transitions, jammed and open states remain non-actionable.

The display uses named RGB pin groups matching [Waveshare's LCD interface table](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-5#Pinouts) and big-endian LVGL pixels. This corrects the former red/blue swap at the output boundary, affecting icons, images and the wheel consistently. MIPI RGB's RGB565 fast path writes the pixel buffer directly, so changing only `color_order` or counter-swapping individual palette values is not a fix. The routing test checks red and blue independently; green/white alone cannot detect this fault. Physical panel colours still need installation acceptance.

Each page's **Page icon** selector controls its icon beside the header title and navigation name. Blank or unsupported icons are omitted from the header; Navigation uses `mdi:view-dashboard` instead. Unsupported icons also log a warning.

This matches entity icons, not icon overrides defined only in individual Lovelace cards. Non-MDI icon packs and icons added after 7.4.47 are not supported; the panel logs a warning and uses a default instead. Door icons are available for existing controls; this does not add door sensors or cover controls.

The full icon library requires the board's 16 MB flash layout. Use a **USB/manual factory flash** for initial installation; subsequent updates can use OTA. Icons are included during compilation; the panel does not download them at runtime.

### Layout and page navigation

Each button page has a 56-pixel black header with its optional icon/title on the left and configured temperature on the right. Temperature uses a thermometer icon instead of a text prefix, with 20-pixel text/icon smaller than the 24-pixel title. A 48-pixel grey (`#999999`) footer contains the 16-pixel bold clock at bottom-left, centred page dots, a Settings cog and bottom-right menu icon. The footer clock is shared by all normal views; screensaver/OTA presentation is unchanged. Long titles and temperatures are truncated into separate regions. Blank or whitespace-only page titles display as **Page N**, using the blueprint slot number.

Each button page independently selects its grid:

**Auto** defaults to 2x2 for 1-4 configured controls, 4x2 for 5-8, and 6x3 for 9-18. Empty pages remain hidden. Count configured slots, including duplicate entities, rather than current on/off/availability states. Navigation's layout remains an independent manual selection.

| Manual layout (columns x rows) | Active input slots | Tile content |
| --- | --- | --- |
| 2x2 | 1-4 | Icon, title and state |
| 4x2 | 1-8 | Icon, title and state |
| 6x3 | 1-18 | Icon and title; unavailable controls are dimmed/disabled |

**Compact and center buttons** defaults on: populated slots compact in configured order, with incomplete rows centred. Auto can display a light from slot 18 in a centered 2x2 grid without changing its numbered identity, title or tap choice. Turn compaction off to retain exact numbered positions, including non-interactive gaps. Auto then uses the highest occupied slot: slot 5 requires 4x2 and slot 18 requires 6x3, even if it is the only control.

A manually selected smaller layout does **not** delete higher-numbered settings: they are ignored with a Home Assistant warning until a larger layout is selected again. Ignored slots do not enable a page or generate watched-entity updates. With manual 4x2 and compaction off, slots 1/2/4 appear as `[x x - x]` on the first row.

Each page's **Enabled** switch defaults on. Disabled pages retain all saved inputs but disappear from Navigation, swipes and dots. Their inactive-only entities/temperatures/related controls are not watched or transmitted. Shared entities still used elsewhere remain watched. Disabling the current/detail-source page safely selects another available content page and cancels pending gestures; a disabled wake target uses the same fallback.

Use **Navigation > Page order and titles** to reorder pages with Home Assistant's native drag handles. Edit a row with its pencil button to set **Page title**; the row displays that title with its fixed page slot underneath. Keep the **Page slot** unchanged when moving a row: it links to the original page's entities, layout, icon and temperature. Menu positions, swipe order and page dots follow the row order, while wake selections retain their original slot identities. Reordering does not jump away from the currently displayed page. Unlisted pages follow in numeric slot order; removing a row does not hide or delete its page. Empty pages are always skipped. Duplicate or invalid slots log an error and retain the previous dashboard.

**Navigation > Page order and titles** is the sole source of page titles. An omitted or blank row title uses **Page N** on the panel.

Tap the menu icon to open **Navigation**, then tap a page button to select it. Only populated pages appear, in the configured order (numeric slot order by default), using the independent **Navigation > Menu layout** setting. Page buttons show their configured icons and titles; duplicate titles still select separate slots. More than four populated pages require a **4x2** or **6x3** menu; all eight button pages plus Homepage require **6x3**. An invalid menu configuration logs an error and retains the previous dashboard (or waiting screen), rather than hiding pages or adding scrolling/pagination. Tap the menu icon again to return without changing the current page. Navigation hides page-specific temperatures and dots, and ignores swipes.

Swipe left/right anywhere on a button page, including over buttons. Navigation wraps at both ends and skips empty pages. A horizontal movement of at least 100 pixels changes page when you lift your finger. Buttons activate only on release after a tap with no movement beyond 20 pixels; drags and gestures that lose their original contact cannot activate controls. Additional contacts never take ownership of an existing gesture. Bottom-centred dots highlight the current page; they are hidden for a single page. Temperature values use the sensor's unit of measurement.

After reboot the panel shows **Waiting for Home Assistant** until it receives configuration. During a disconnection it retains its layout and displays unavailable indicators with disabled controls. No configuration is saved to flash. The blueprint sends current configuration/states on selected entity changes, reconnect events and every 30 seconds; the panel also disables controls if updates stop for 65 seconds.

Page navigation remains available offline using the last known configuration. Renamed, added or removed pages update the menu automatically. With no populated pages, the menu is disabled and the panel shows its waiting/no-pages message. The header and footer remain visible on these screens.

The static 800 × 480 background in [`assets/background_dark.png`](./assets/background_dark.png) is embedded in the firmware and visible behind the content area of every view, including waiting and navigation.

The darker artwork was verified byte-for-byte against generated RGB565 images in local and independently resolved published-package builds. Compare exposed areas between tiles or the screensaver: inactive tiles are 90% opaque, and active white tiles are opaque. If artwork appears stale, allow the package/component 300-second refresh windows to expire, use **Clean Build Files**, rebuild and check the installed compilation time. These checks do not establish which firmware is currently on your device.


Changing layouts, page icons or controls does not require recompilation.

### Light and lock details and related controls

Each numbered button has **Tap action**: **Default action** preserves its existing toggle/lock/script operation; the details option opens the selected light or lock without operating it. The choice is specific to that button and page, even when the same entity appears elsewhere. Selecting details for another active domain is an explicit configuration error.

**Hold anywhere on a light or lock tile for 600 ms**, including its name/state/body, to open details independently of the tap option. Finger drift up to 40 pixels per axis is accepted; exceeding that allowance at any point cancels the hold. Short-tap tolerance remains 20 pixels and horizontal swipes require 100 pixels. Opening details consumes the entire gesture, so release before using another control; it never also operates the entity. DEBUG logs retain movement/owner/layout cancellation, early release and **Opening details** diagnostics. A first gesture after sleep/screensaver is wake-only and logs **Touch reserved for wake**. Bulb capability metadata controls which editors appear, not whether an available light can open details.

Input is deliberately **single-touch**: the first reported contact owns the entire gesture, and additional contacts are ignored while it remains down. Broad finger presses reporting extra contacts do not cancel a hold, tap or slider drag. Only ESPHome's PRESSED/UPDATED states count as active; nonzero releasing states are not live fingers. If the owner disappears while another contact remains, the gesture is cancelled until all contacts release, rather than handed to another finger. This prevents a different contact from completing a hold or committing a command. Owner-loss diagnostics include the original/remaining IDs, state and coordinates.

The detail header combines the source page and configured item title, for example **Office - Lamp**, and follows live renames using the exact source page/slot. Removing that source closes details even if the same entity remains elsewhere. The upper-left **State** tile always toggles the primary light, regardless of its main-page tap choice; its appearance/icon conveys state without a second ON/OFF label. Back is a footer arrow immediately left of the cog/menu, matching their font and touch area. The reusable tile's border/radius are reset when it returns to another view.

The left three quarters contain State and supported brightness, temperature and colour controls. Select a mode at the right of that area. The HS wheel changes hue/saturation; colour-capable RGB/XY lights use Home Assistant's conversion. Its label is an RGB hex code for the selected hue/saturation at full value, independent of brightness; it is not a measured physical colour. The conversion retains fractional hue instead of LVGL's intermediate 8-bit hue approximation. Missing readings are hidden while the light is off; while it is on, **Choose brightness/temperature/colour** invites a selection. Editing an off light immediately shows the preview. No missing reading is invented in the model.

Temperature appears only when the light advertises colour-temperature support and a valid Kelvin range. Missing bounds retain **Temperature range unavailable**. On/off-only lights show State without invented sliders. Unreported capabilities show an explicit diagnostic. Disconnection, unavailability, pending commands and confirmation errors remain visible, even while the light is off. Normal ON/OFF status text is hidden.

The value label is horizontally centred over the active slider or wheel using that widget's bounds. The vertical slider has a 24-pixel track and bounded thumb, clear of the value label and footer at minimum/maximum and while pressed. A matching 24-pixel click extension on the LVGL and raw-touch routes retains a finger-friendly 72-pixel-wide target, including the protruding thumb. Raw gesture mapping derives its range from the actual widget bounds, not duplicated coordinates. Colour-wheel hit testing is unchanged.

Drag to preview locally; **one command is sent on release**, not continuously. Brightness zero calls `light.turn_off`; other brightness, temperature and colour edits call `light.turn_on`, including when the light is off. Incoming state updates do not overwrite an active drag. The view waits for reported confirmation, then returns to authoritative state; an unconfirmed change shows a warning after five seconds. Offline/unavailable controls cannot send commands. Capability changes, entity removal, manual backlight-off, wake/screensaver transitions and OTA cancel edits safely.

Under **Light details > Related controls**, add a structured row for a primary light and select up to two related **light, switch, automation or script** entities, with optional titles. They occupy the right-hand quarter and perform their ordinary actions. Related entities need no main-page tile and are watched for updates automatically. Shared entities are transmitted once across light and lock bindings; title overrides remain specific to each binding. No row is required merely to enable normal details. Existing automation tiles are not removed automatically: clear them yourself if replaced by related controls. Rows for primaries outside the active layouts are retained but inactive.

Lock details reuse the same container and tile pool, hiding all light editors/modes. A large left-hand control offers **Unlock** when actually locked or **Lock** when actually unlocked, with actual status and green/red colours independent of any main-page indicator. Other states are shown truthfully but cannot issue guessed commands. Offline details remain readable with commands disabled. There is no PIN, extra confirmation, latch-open action, optimistic state or automatic retry. **Lock details > Related controls** supplies up to two controls on the right, such as an auto-lock switch and a script. Related locks and nested details are not supported. Changing actual lock state cancels an in-flight gesture so it cannot complete a now-different operation.

Back returns to the source content page. Details never enter page ordering, dots, wake choices or last-used content identity. The footer menu/cog remain available. Related tiles retain normal actions. Continuous/throttled drag commands, related sensors/energy readings, HA `button` entities, four related controls and other detail domains remain deferred.

### Firmware updates

OTA uploads **blank the backlight by default** and pause LVGL without animation; touches, commands, clock redraws, wake/idle transitions and incoming dashboard transfers are blocked. A previously dark display stays dark. Successful uploads remain blanked and paused until the normal reboot. On abort/error, the prior view/backlight/manual-sleep policy is restored, inactivity deadlines restart and the panel requests fresh configuration rather than replaying partial transfers.

Pausing LVGL does **not** stop the RGB controller scanning its PSRAM framebuffer, and the static **Updating firmware** overlay alone showed graphical corruption in physical testing. Blanking masks that corruption without changing display timings or watchdog settings.

To opt back into the static overlay instead of blanking, set this device-YAML substitution and rebuild:

```yaml
substitutions:
  ota_blank_display: "false"
```

The default is `ota_blank_display: "true"`. With either setting, the backlight's previous state is restored on abort/error.

### Homepage and Settings

Enable **Homepage > Enable Homepage** in the blueprint to add a placeholder view with centred **Homepage** text and the shared background/header/footer. It is off by default, is additional to the eight button pages, and works even when no button pages are configured.

Homepage participates in the menu, swipes and page dots. It comes first unless a **Homepage** row in **Navigation > Page order and titles** places it elsewhere. Its optional title changes the header/menu label, not the placeholder text. Disabling Homepage retains its ordering input and safely returns to another available page.

Tap the footer cog for **Settings**. **Restart** immediately reboots the panel, without confirmation; **Sleep** immediately turns the backlight off. These are local actions and work without Home Assistant, including before any pages arrive. Settings has no page dots, is excluded from swipes/order/wake choices, and never replaces the last content page. The menu icon opens Navigation from Settings; touching a sleeping panel wakes only, as below.

Restart/Sleep are stacked in the left third. The right-hand **Overview** shows the actual ESPHome device name/hostname, CI-generated firmware project version and current local IPv4 address. A disconnected/unset network shows **Unavailable**, not a stale or zero address. IP changes refresh while Settings is visible, without HA sensors or internet polling; unchanged values do not redraw labels.

### Display backlight

The device's **Configuration** section in Home Assistant includes a **Display backlight** switch. It controls the board's active-high CH422G EXIO2 backlight enable and turns on after every reboot.

The **Display timeout** setting in the same section turns the backlight off after inactivity. Enter seconds from **0 to 86400** (24 hours); **0** disables automatic switch-off. The default is 0, and Home Assistant's numeric field does not store a blank value. The setting is saved across reboots. Changing it or turning the backlight on restarts the countdown; timeouts are checked once per second.

Touching a dark screen turns the backlight on. That entire first gesture is wake-only: it cannot activate a button or change pages, even with multiple fingers. Release, then touch again to operate the dashboard. The timer pauses while touching and restarts on release. Manually switching the backlight off also cancels any in-progress control gesture.

Turning the backlight off does not stop the dashboard or Home Assistant connection. A timeout of 0 still permits manual switch-off, with touch-to-wake available.

The saved **Wake-up page** dropdown defaults to **Last used page**, or can select **Page 1** through **Page 8** (the blueprint's fixed page slots), **Navigation / menu**, or **Homepage**. It applies whenever the backlight turns on, whether by touch, Home Assistant or reboot, and when dismissing the screensaver. **Navigation / menu** opens the page-selection menu; other choices close it. **Last used page** means the last button page or Homepage, never Navigation or Settings. An empty/disabled selected page falls back to the first available content page in display order. If no pages have arrived yet, the panel retains its waiting screen and applies the preference when Home Assistant sends populated pages. Changing the dropdown does not immediately navigate, and normal touches while the backlight is on do not change pages. Enable Homepage through the blueprint to use it.

The **Restart** button in **Configuration** reboots the panel. The timeout and wake-up page settings are retained; the dashboard waits for Home Assistant to resend its configuration as usual.

Brightness adjustment is not supported by this connection: [ESPHome's CH422G driver](https://esphome.io/components/ch422g/) does not support PWM. A true brightness slider would require a hardware-supported dimming connection; no simulated slider is exposed.

### Screensaver

Expand **Screensaver** in the blueprint and turn on **Enable screensaver** (off by default), then save/run the automation.

The device's Home Assistant **Configuration** section now includes **Screensaver time**, saved across reboots. Enter **0 to 86400 seconds**; the default is **60**, and **0** disables the screensaver. After inactivity, a full-screen view covers all page/header/footer/menu content with the shared background, a large centred clock and the smaller date below it, by default **18:35** / **Saturday, 14 November 2026**. It uses the same Home Assistant-synchronized time as the header; before synchronization it shows **--:--** and **Waiting for time**.

The first complete touch gesture dismisses the screensaver without activating controls, applies the saved **Wake-up page**, and restarts inactivity. Release before operating controls. Changing **Screensaver time** also dismisses the screensaver and restarts inactivity. Periodic Home Assistant updates and disconnections do not dismiss it.

**Display timeout** remains independent: showing the screensaver does not restart the backlight countdown. For example, set **Screensaver time** to **60** and **Display timeout** to **300** for the clock after one minute and backlight-off after five minutes. A shorter or equal display timeout switches the backlight off first; a display timeout of **0** leaves it on for the screensaver. Both timers pause while touching and restart on release.

### Date/time formats and wake sensors

The blueprint's **Date and time** section offers 24-hour or 12-hour AM/PM time, plus long date, weekday with DD/MM, DD/MM/YYYY, MM/DD/YYYY, ISO YYYY-MM-DD or abbreviated-month dates. Defaults are unchanged. Header and screensaver update together; the header uses a smaller font for AM/PM to avoid overlap. Names remain English; custom formats, temperature conversion and timezone selection are not included.

Under **Wake sensors > Wake-up sensors**, select any number of binary sensors. A genuine **OFF → ON** transition wakes the backlight/screensaver and uses the saved wake route; an already-awake panel does not jump pages. While any selected sensor is ON, both automatic backlight-off and screensaver entry are suspended. When the last turns OFF, both inactivity deadlines restart. Unknown/unavailable values are not ON, and attribute-only changes do not count as activations.

Manual **Sleep** or HA backlight-off takes priority over an existing ON sensor. Periodic snapshots cannot undo it; a fresh OFF → ON transition or touch releases that override. A reconnect snapshot can restore an active hold and wake an automatically timed-out panel, but cannot undo manual Sleep. Connection loss or 65 seconds without valid snapshots releases the hold, so the display cannot remain held awake by stale sensor state.

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

For an unpublished local build, keep a private device YAML with your own secrets and absolute checkout paths:

```yaml
substitutions:
  device_name: office-panel
  friendly_name: Office Panel
  wifi_ssid: !secret panel_wifi_ssid
  wifi_password: !secret panel_wifi_password
  wallpanel_component_source: /absolute/path/to/checkout/esphome/components
api:
  encryption:
    key: !secret panel_api_key
packages:
  wallpanel: !include /absolute/path/to/checkout/ha-esp32-s3-touch-lcd5.yaml
```

Replace both checkout paths. Compile that private configuration and copy the matching local blueprint into HA; do not use an unpublished tag URL.

The firmware stores eight page models with eighteen button slots each, but shares one eighteen-tile LVGL pool between entity pages, Navigation, Settings and light/lock details, plus one nine-dot strip and a single detail editor. Hidden-page updates retain each entity's own last-known state and icon in the model, not in hidden widgets. The tree has 178 actual screen/top-layer objects, below the 220-object limit, with 1158 measured startup refresh requests below the 1500-request limit. Watchdog settings are unchanged. [`scripts/generate_dashboard.py`](./scripts/generate_dashboard.py) generates this fixed widget scaffold, not user configuration:

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

The connected native API check also requires `aioesphomeapi` (included with ESPHome). It starts/stops its own local host process, requires port 16053 to be unused, and tests blueprint-generated 144-control snapshots, Unicode, partial-transfer atomicity, real entity actions through all three layouts, Homepage, local Settings actions, clock format changes, sensor wake/manual precedence and hold expiry. Detail checks cover hold consumption/cancellation, reusable light/lock geometry, independent indicators, repeated script execution, actual-state lock decisions, white/reversed appearance, atomic metadata rejection, OTA gates/recovery, typed numeric/HS-list service envelopes, release-only edits, confirmation timeout, related actions and exact-source removal. Host Restart/backlight actions are test callbacks, not real restarts or hardware output. It runs with a 128 KiB host stack and checks bounded retained heap after repeated transfers.

The native child process disables glibc's tcache with `GLIBC_TUNABLES=glibc.malloc.tcache_count=0`: `mallinfo2().uordblks` otherwise includes freed blocks held in that cache, obscuring live-allocation growth. This affects only the test process, not firmware or the parent environment. Each repeated maximum-size transfer changes a label to acknowledge atomic application before measuring; the initial baseline is taken after redraw settling. The 16 KiB growth threshold is unchanged.

The generator requires PyYAML; blueprint template tests also use Jinja2 (both are included with ESPHome). Protocol version 4 uses exactly eight page slots, each with layout/icon/compaction metadata and eighteen fixed numbered button slots regardless of grid size. Each page requires boolean `compact`; each button requires `tap_action` (`default` or `details`), an `icon` string (empty for its state-aware default), and `indicator`. The latter is explicitly null when unconfigured, otherwise `{state, active, available}` with raw state text and typed booleans. Actor state remains separate. Unavailable indicator states cannot claim availability or activity. Populated counts must fit the resolved grid; noncompact positions must also fit its capacity. Empty slots use the default tap action and null indicator. This preserves high-slot identity in small Auto grids. The required root `page_order` is a permutation of zero-based slot indices 0-7, separate from the `pages` array. The blueprint expands partial user orders to a complete permutation. The runtime validates the entire snapshot before changing the UI; unsupported protocols/entities and malformed payloads are logged and rejected without replacing the previous configuration.

All root fields are required: `version`, `blueprint_release`, `latest_release`, `pages`, `navigation_layout`, `page_order`, `screensaver_enabled`, `homepage`, `localization`, `wake_hold`, `reverse_active`, `light_details`, `lock_details` and `detail_entities`. `blueprint_release` is a stable SemVer string; `latest_release` is another stable version or an empty string when unavailable/unconfigured. `homepage: {enabled, position, title}` uses insertion position 0-8 among ordered entity slots; `localization: {time, date}` uses allowlisted presets. Disabled features are represented explicitly with false flags and empty arrays, not missing fields. The separate `esphome.<device_name>_wake_display` action represents a fresh sensor activation; the blueprint sends it before the snapshot transfer so a later attribute/periodic update cannot cancel it mid-transfer. Local manual-sleep precedence is not stored in HA snapshots.

`reverse_active` contains unique entity IDs; `light_details` contains unique primary-light capability/readings/bindings; `lock_details` contains unique `{entity, related, related_titles}` lock bindings; `detail_entities` contains deduplicated related entity/name/state/icon records. Detail readings include normalized `colour_mode`, brightness 0–255, HS values and Kelvin bounds/value; unavailable readings use -1 rather than a manufactured setting. Each light record includes `temperature_issue` and `related_titles`, even when false or empty. Up to two related references and titles belong to each primary binding. Unknown bulb capabilities remain explicit diagnostics rather than invented capabilities. Metadata is validated with the rest of the snapshot before an atomic replacement. Outgoing brightness/Kelvin values and HS lists use native HA `data_template` expressions; they are not quoted string-valued colour arrays.

The Dashboard endpoint sensor and ready events advertise `esphome.<device_name>_set_dashboard_chunk` directly. The blueprint sends ordered calls to that action with a transfer ID, zero-based index, total and string payload. Each payload has a `~` text prefix followed by at most 4096 bytes of ASCII JSON; the complete snapshot is capped at 256 KiB/64 chunks. Incomplete transfers expire after 30 seconds of inactivity. Only a complete, validated snapshot replaces the UI. Chunking avoids ESPHome's 32 KiB native API message limit without modifying ESPHome internals. No full-payload dashboard action is exposed.

The blueprint explicitly uses `to_json(ensure_ascii=True)`: HA otherwise preserves Unicode, so a 4096-character fragment containing degree signs, accents or emoji can exceed the firmware's 4096-byte JSON limit. Escaping preserves the original displayed text while keeping character and byte counts equal. Do not remove it or raise the transfer limits to mask invalid chunks.

Automated validation includes a 251,924-byte Unicode snapshot with light metadata and 144 independent indicators across 62 native API chunks, within the 256 KiB cap. This case uses 80-emoji control titles and eight-emoji indicator states. The verified run's retained host heap decreased by 496 bytes after repeated maximum-size transfers, within the 16,384-byte growth limit. The production ESP32 build uses 6,389,252 bytes of flash and 129,924 bytes of static RAM. Oversized configurations log an actionable HA error and send no partial model; shorten labels or reduce related controls rather than raising the cap. Host heap/stack checks and firmware static RAM reports do not establish ESP32 runtime headroom: after installation, confirm successful boot, PSRAM/free heap, dense-label legibility, light/lock controls, correct RGB primaries and touch responsiveness, particularly with all 144 controls configured. Physical OTA stability and abort/error recovery also need panel confirmation.

For local firmware development, set the `wallpanel_component_source` substitution to your local `esphome/components` directory (relative to the device YAML). Its default is `github://otghcloud/ha-esp32-s3-touch-lcd5@main`; if pinning a remote package to another revision, pin this source to the same revision.

## Notes

- The display uses `mipi_rgb` with the original 800 × 480 timings and data-bit mapping. GPIO46/GPIO3 strapping warnings are explicitly acknowledged for the board's fixed sync connections; do not add external pull resistors to these pins.
- The ESP-IDF configuration selects the `generic` flash driver reported by the target panel. If another hardware revision reports a different flash driver, override `esp32.framework.advanced.flash_chip` accordingly.
- The fallback access point and captive portal are not enabled.
- The public package does not currently implement `boot_sound` or `language` settings.
