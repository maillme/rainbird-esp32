---
"rainbird-esp32": minor
---

Add MQTT-triggered over-the-air firmware updates

- OTA via MQTT: publish firmware URL to `rainbird/ota/set` to trigger update
- Firmware hosted on GitHub Releases, downloaded over HTTPS
- HA auto-discovery for "OTA Update" button and "Bridge Version" sensor
- Firmware version tracking via `FW_VERSION` in config.h
- Partition table changed to `min_spiffs.csv` for dual OTA slots
- Watchdog safely disabled during OTA download, re-enabled on failure

NOTE: First flash after this change MUST be via USB (partition table changed).
All subsequent updates can be done via OTA.
