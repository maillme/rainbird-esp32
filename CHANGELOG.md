# rainbird-esp32

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
