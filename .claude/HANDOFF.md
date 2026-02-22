# Rain Bird BLE-to-MQTT Bridge

## Project Purpose
Remote irrigation control from Amsterdam for a garden in Valencia. ESP32-C3 (Seeed XIAO) bridges a Rain Bird ESP-BAT-BT-4 controller over BLE to MQTT, consumed by Home Assistant.

**Full chain**: HA (Amsterdam) → MQTT (Mosquitto on HA) → Tailscale → ESP32-C3 (Valencia) → BLE → Rain Bird BAT-BT-4

## Workflow Rules
- **Do NOT flash the ESP32-C3 or open serial monitor.** The user handles this themselves via PlatformIO in a separate VSCode window.
- Build check only: `~/.platformio/penv/bin/pio run` from `rainbird-esp32/`
- User runs `pio run -t upload` and `pio device monitor -b 115200` manually
- The XIAO ESP32-C3 sometimes needs BOOT button held during reset to enter bootloader mode for flashing

## Project Structure
```
rainbird/
├── CLAUDE.md              ← This file
├── PROTOCOL.md            ← Full SIP protocol reference (reverse engineered from APK)
├── rainbird-esp32/        ← ESP32-C3 firmware (PlatformIO project)
│   ├── platformio.ini     ← Build config, libs: NimBLE-Arduino 2.x, PubSubClient, ArduinoJson
│   └── src/
│       ├── config.h       ← WiFi, MQTT, BLE credentials and timing constants
│       ├── main.cpp       ← Setup, loop, status polling, follow-up poll timer
│       ├── rainbird_ble.h/cpp  ← BLE connection, scan, command send/receive, runStation
│       ├── mqtt_handler.h/cpp  ← MQTT connection, HA discovery, command queue, state publishing
│       └── sip_protocol.h/cpp  ← SIP command builders and response parsers
└── rainbird2-decompiled/  ← Decompiled Rain Bird Android APK (reference only)
    ├── resources/res/raw/sipcommands.json  ← Command definitions with lengths
    └── sources/com/rainbird/rainbirdlib/rb2/ble/
        ├── d.java         ← BLE SIP client implementation (command builders)
        ├── x.java         ← Command opcode enum
        └── h.java         ← BLE communication handler
```

## Critical Protocol Details

### SIP Command Format
- **Station IDs are 2-byte big-endian** in all run/station commands. Station 1 = `00 01`, NOT `01`.
- This was the root cause of all station run command NAKs — station encoded as 1 byte caused the controller to read station 256+ (invalid).

### Working Commands on BAT-BT-4 (confirmed)
| Command | Opcode | Format | Status |
|---------|--------|--------|--------|
| StackRunStation | `0x4B` | `4B 00 <station> <duration_minutes>` (4 bytes) | **PRIMARY - USE THIS** |
| StopIrrigation | `0x40` | `40` (1 byte) | Working |
| AdvanceStation | `0x42` | `42` (1 byte) | Working |
| SetControllerState | `0x49` | `49 <state>` (2 bytes, 0=Off/1=Auto) | Working |
| All query commands | various | See PROTOCOL.md | Working |

### Commands NOT supported on BAT-BT-4
| Command | Opcode | Error |
|---------|--------|-------|
| ManualRunStation | `0x39` | Always NAK error 01 |
| StackRunStationSec | `0x2B` | Not in sipcommands.json, may not be supported |

### NAK Response Format
`00 <echoed_opcode> <error_code>` — Error codes: 01 = unsupported/invalid format, 04 = invalid parameter

## Key Implementation Details

### BLE (NimBLE-Arduino 2.x)
- **NimBLE 2.x API**: `getResults()` duration is in **milliseconds**, not seconds (was causing 0-device scans)
- BLE address caching: After first successful scan, cached address enables instant reconnect (skip 5s scan)
- ESP32-C3 has single 2.4GHz radio shared between WiFi and BLE; WiFi power save mode (`WIFI_PS_MAX_MODEM`) helps BLE
- Service UUID: `0000FE40-...`, Write: `FE46`, Notify: `FE47`
- The Rain Bird device name is `BAT-BT-4 579A`

### MQTT / Home Assistant
- HA MQTT Discovery auto-creates: 4 station switches, duration numbers, mode select, stop/advance buttons, sensors
- **Optimistic mode** on switches (`"opt": true`) — HA shows ON immediately, doesn't wait for state confirmation
- **Command queue pattern**: MQTT callback only queues commands (sets `_pendingCmd`), main loop executes BLE operations via `processPendingCommand()`. This is critical because BLE operations take 10+ seconds and PubSubClient callbacks can't block that long.
- **Follow-up poll**: After starting a station, a status poll is scheduled for `duration + 30 seconds` to update HA with the real state (station turned off)
- If station start fails, the optimistic ON is reverted to OFF immediately
- `discoveryPause()` (100ms delay + mqtt.loop()) between discovery publishes prevents MQTT buffer overflow

### Status Polling
- Polls every 30 minutes: battery, irrigation state, active stations, rain sensor, rain delay, water budget
- First poll also queries model/firmware and runs COMMAND_SUPPORT diagnostic (checks which of 8 action commands are supported)
- Follow-up poll triggers after station duration expires to sync HA state

### Run Station Fallback Chain
`runStation()` tries commands in order on a single BLE connection:
1. `0x4B` StackRunStation (minutes) — **always works on BAT-BT-4**
2. `0x39` ManualRunStation (fallback)
3. `0x2B` StackRunStationSec (seconds, last resort)

## Config (config.h)
- WiFi: `Smith` network
- MQTT: `valencia.local:1883`, user `Rainbird-user`
- BLE device: `BAT-BT-4 579A`
- Poll interval: 30 min, BLE timeout: 10s, response timeout: 5s
- Default station duration: 10 min, 4 stations

## Common Issues & Solutions
1. **BLE scan finds 0 devices**: NimBLE 2.x uses milliseconds for scan duration, not seconds
2. **MQTT callback blocks / switch auto-toggles OFF**: Must queue BLE commands, not execute in callback
3. **Station run commands NAK**: Station ID must be 2-byte big-endian (`00 01` not `01`)
4. **Flash fails with serial noise**: Close serial monitor first, or hold BOOT button during reset
5. **C++ brace-list assignment fails**: ESP32 toolchain doesn't support `struct = {val, val}` — set fields individually
