# rainbird-esp32

## 0.3.1

### Patch Changes

- Fix HA device ID to match original format, prevent ghost device

## 0.3.0

### Minor Changes

- Add automatic light sleep for battery operation, dynamic HA device identity

  - Enable ESP32-C3 automatic light sleep (~2-5mA idle vs ~50mA before)
  - WiFi stays associated during sleep, MQTT commands received in near real-time
  - Derive HA device ID and name from RAINBIRD_DEVICE_NAME in secrets.h
  - Add README with setup guide, entity list, and architecture overview

## 0.2.3

### Patch Changes

- Stability improvements: watchdog timeout, battery smoothing, MQTT backoff

  - Increase watchdog timeout from 30s to 60s for BLE operation headroom
  - Smooth battery percentage with 3-reading moving average
  - Exponential backoff on MQTT reconnect (5s → 300s cap, resets on success)

## 0.2.2

### Patch Changes

- OTA button uses latest GitHub release URL with version check

## 0.2.1

### Patch Changes

- Fix OTA updates from GitHub releases

  - Follow HTTP redirects during OTA download (GitHub 302 to CDN)
  - OTA button uses latest GitHub release URL instead of raw payload
  - Skip OTA if already on latest version

## 0.2.0

### Minor Changes

- Add HA update entity with GitHub release checking, publish bridge version on connect, reduce CPU heat with loop delay
- b416c06: Add heartbeat monitoring, power savings, and stability improvements

  - Reduce BLE poll interval from 30min to 4hrs to conserve Rain Bird battery
  - Add hourly MQTT heartbeat (uptime, WiFi RSSI, free heap) with HA auto-discovery
  - Add healthchecks.io ping for external uptime monitoring
  - Enable ESP32 hardware watchdog (30s timeout, auto-reboot on hang)
  - Remove blocking delay in MQTT reconnect path
  - Fix battery percentage calculation to use non-linear alkaline discharge curve

- 27497f1: Add MQTT-triggered over-the-air firmware updates

  - OTA via MQTT: publish firmware URL to `rainbird/ota/set` to trigger update
  - Firmware hosted on GitHub Releases, downloaded over HTTPS
  - HA auto-discovery for "OTA Update" button and "Bridge Version" sensor
  - Firmware version tracking via `FW_VERSION` in config.h
  - Partition table changed to `min_spiffs.csv` for dual OTA slots
  - Watchdog safely disabled during OTA download, re-enabled on failure

  NOTE: First flash after this change MUST be via USB (partition table changed).
  All subsequent updates can be done via OTA.
