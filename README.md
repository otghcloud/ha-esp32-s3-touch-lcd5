# HA ESP32-S3 Touch LCD 5

ESPHome configuration for the Waveshare ESP32-S3-Touch-LCD-5 (800 × 480), with a touch dashboard connected to Home Assistant.

## Requirements

- Home Assistant with ESPHome Device Builder, or a standalone ESPHome installation.
- A Waveshare ESP32-S3-Touch-LCD-5.
- Home Assistant entities matching the IDs listed below.

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

## Dashboard configuration

The static page and entity definitions are in [`config.yaml`](./config.yaml). The ESPHome YAML included by the remote package is generated from that file. After changing the page titles, controls, or entity IDs, regenerate the package files with:

```sh
python3 scripts/generate_dashboard.py
```

The generator requires PyYAML (included when ESPHome is installed). To verify that generated files match the configuration without modifying them, run:

```sh
python3 scripts/generate_dashboard.py --check
```

Pages support up to four controls each in the current two-column layout. Light and automation controls toggle their Home Assistant entities; lock controls lock or unlock based on their current state. Swipe left and right to move between pages.

## Current Home Assistant entities

The initial `config.yaml` uses these fixed entity IDs:

| Purpose | Entity ID |
| --- | --- |
| Temperature | `sensor.office_nspanel_temperature` |
| Lamp | `light.office_lamp` |
| Ceiling light | `light.office_ceiling` |
| Cupboard light | `light.office_cupboard_light` |
| Door lock | `lock.office_office_door` |
| Landing light | `light.landing_light` |
| Landing lighting automation | `automation.lighting_landing` |

These entities must exist in Home Assistant for the controls to work. Entity IDs can be changed in `config.yaml`; automatic configuration through a Home Assistant blueprint is planned for a future iteration.

## Notes

- The dashboard currently uses a solid dark background; the background image is omitted while its rendering issue is investigated.
- The fallback access point and captive portal are not enabled.
- The public package does not currently implement `boot_sound` or `language` settings.
