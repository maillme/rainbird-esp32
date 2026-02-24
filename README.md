# Rain Bird BLE-to-MQTT Bridge

ESP32-C3 firmware that bridges Rain Bird battery-powered BLE irrigation controllers to Home Assistant via MQTT.

Built for the **Rain Bird ESP-RZXe** (BAT-BT series) — the battery-operated controller that only supports Bluetooth, not WiFi. This bridge gives it full Home Assistant integration with auto-discovery, remote control, and over-the-air firmware updates.

## What it does

- **Controls irrigation** from Home Assistant — start/stop stations, run programs, change modes
- **Monitors status** — battery voltage & percentage, active stations, rain sensor, rain delay, water budget
- **Auto-discovery** — all entities appear automatically in HA via MQTT discovery
- **OTA updates** — checks GitHub releases hourly, update from the HA device page
- **External monitoring** — heartbeat pings to healthchecks.io for uptime tracking

## Hardware

- [Seeed XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) — small, cheap, BLE + WiFi
- Rain Bird ESP-RZXe / BAT-BT-4 controller
- USB-C cable for initial flash

The ESP32-C3 sits near the Rain Bird controller (BLE range ~10m) and connects to your WiFi network to reach the MQTT broker.

## Home Assistant entities

| Entity | Type | Description |
|--------|------|-------------|
| Station 1-4 | Switch | Turn irrigation zones on/off |
| Station 1-4 Duration | Number | Set run time in minutes per zone |
| Controller Mode | Select | Auto (scheduled) / Off |
| Stop Irrigation | Button | Emergency stop all zones |
| Advance Station | Button | Skip to next station in sequence |
| Run Program A-D | Button | Run a saved irrigation program |
| Battery Voltage | Sensor | Raw millivolts (4x AA alkaline) |
| Battery | Sensor | Percentage (alkaline discharge curve) |
| Rain Sensor | Binary sensor | Wet / Dry |
| Rain Delay | Sensor | Days remaining |
| Water Budget | Sensor | Percentage |
| Firmware | Sensor | Rain Bird controller firmware version |
| Bridge Version | Sensor | ESP32 bridge firmware version |
| Bridge Uptime | Sensor | Seconds since last reboot |
| Bridge WiFi RSSI | Sensor | WiFi signal strength |
| Free Heap | Sensor | Available memory |
| BLE RSSI | Sensor | Bluetooth signal strength to controller |
| Firmware Update | Update | Shows available OTA updates from GitHub |
| OTA Update | Button | Force OTA from latest GitHub release |

## Setup

### 1. Clone and configure

```bash
git clone https://github.com/maillme/rainbird-esp32.git
cd rainbird-esp32
```

Copy the secrets template and fill in your credentials:

```bash
cp src/secrets.h.example src/secrets.h
```

Edit `src/secrets.h` with your WiFi, MQTT, and Rain Bird BLE device details.

### 2. Build and flash

Requires [PlatformIO](https://platformio.org/).

```bash
# Build
pio run

# Flash via USB
pio run -t upload

# Monitor serial output
pio device monitor -b 115200
```

The XIAO ESP32-C3 may need the BOOT button held during reset to enter bootloader mode.

### 3. Home Assistant

Ensure you have an MQTT broker (e.g., Mosquitto) configured in HA. The bridge publishes auto-discovery configs on first connect — all entities appear automatically under a "Rain Bird" device.

## OTA updates

After the initial USB flash, all future updates can be done over-the-air:

1. The bridge checks GitHub releases once per hour
2. If a newer version is found, the HA update entity shows "Update available"
3. Click Install from the HA device page — the bridge downloads and flashes the new firmware

## Architecture

```
Home Assistant ←→ MQTT (Mosquitto) ←→ ESP32-C3 ←→ BLE ←→ Rain Bird BAT-BT
```

The ESP32-C3's single 2.4GHz radio is shared between WiFi and BLE. WiFi power save mode is enabled to improve BLE reliability and reduce heat.

## Protocol

Communication with the Rain Bird controller uses the proprietary SIP protocol over BLE, reverse-engineered from the Rain Bird Android app. See `PROTOCOL.md` for the full command reference.

## License

ISC
