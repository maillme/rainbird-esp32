---
"rainbird-esp32": minor
---

Add heartbeat monitoring, power savings, and stability improvements

- Reduce BLE poll interval from 30min to 4hrs to conserve Rain Bird battery
- Add hourly MQTT heartbeat (uptime, WiFi RSSI, free heap) with HA auto-discovery
- Add healthchecks.io ping for external uptime monitoring
- Enable ESP32 hardware watchdog (30s timeout, auto-reboot on hang)
- Remove blocking delay in MQTT reconnect path
- Fix battery percentage calculation to use non-linear alkaline discharge curve
