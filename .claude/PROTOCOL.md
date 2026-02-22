# Rain Bird ESP-BAT-BT SIP Protocol Reference

Reverse engineered from Rain Bird 2.0 Android APK v1.4.1 (`com.rainbird.rainbird2`).

## Overview

The Rain Bird ESP-BAT-BT-4 uses **SIP (Standard Irrigation Protocol)** over BLE, built on an STM32WB P2P Server template. The protocol is **unencrypted plain binary** with PIN-based authentication only.

## BLE Service & Characteristics

**Service UUID**: `0000FE40-CC7A-482A-984A-7F2ED5B3E58F`

| UUID | Properties | CCCD | Role | Confirmed |
|------|-----------|------|------|-----------|
| `0000FE41-...-21EDAE82ED19` | Write | No | **Unused** (vestigial P2P template) | nRF + APK |
| `0000FE42-...-21EDAE82ED19` | Notify | Yes | **Unused** (vestigial P2P template) | nRF + APK |
| `0000FE43-...-21EDAE82ED19` | Notify | Yes | Unsolicited status pushes (TBC) | nRF |
| **`0000FE46-...-21EDAE82ED19`** | **Write** | **No** | **SIP command channel** (`writeSIPCommand`) | nRF + APK |
| **`0000FE47-...-21EDAE82ED19`** | **Notify** | **Yes** | **SIP response channel** | nRF + APK |
| `0000FE44-...-21EDAE82ED19` | Write | No | OTA firmware updates (`writeOTA`) | nRF + APK |
| `0000FE45-...-21EDAE82ED19` | Notify | Yes | OTA response | nRF + APK |
| `0000FE48-...-21EDAE82ED19` | Write | - | U2OTA (not on GATT, app-only ref) | APK |
| `0000FE49-...-21EDAE82ED19` | Notify | - | U2OTA flow | APK |
| `0000FE4A-...-21EDAE82ED19` | Notify | - | U2OTA flow end | APK |

Full UUID suffix: `8E22-4541-9D4C-21EDAE82ED19`

**Device name**: `BAT-BT-4 579A`

**Primary command flow**: Write to **FE46**, receive response on **FE47**.

Note: FE41/FE42 are present on the device GATT table (confirmed by nRF Connect scan) but the
Rain Bird app never references them. They are remnants of the STM32WB P2P Server template that
Rain Bird built upon. The APK code explicitly uses FE46/FE47 for SIP commands (log message:
`"start writeSIPCommand"`) and FE44/FE45 for OTA (log message: `"start writeOTA"`).

## Packet Format

```
Request:  [OpCode] [Data...]
Response: [ResponseCode] [Data...]
```

- No length prefix
- No checksum/CRC
- No encryption
- OpCode and ResponseCode are single bytes
- Query response codes = request code | 0x80 (e.g., 0x02 -> 0x82)
- Action response codes = 0x01 (ACK) with command echo byte

### ACK Response Format
```
[0x01] [EchoedCommandByte]
```

### NAK Response
```
[0x00] [ErrorData...]
```

## Connection Flow

1. BLE connect to device
2. Discover services (service `0000FE40-...`)
3. Request MTU negotiation
4. Enable CCCD notifications on FE47 (write `01 00` to descriptor `00002902-0000-1000-8000-00805F9B34FB`)
5. Query SecurityStatus (`0x55`)
6. If no PIN set or PIN already validated -> proceed with commands
7. If PIN required -> send SetControllerPin (`0x59`) with PIN bytes

## Authentication

### SecurityStatus (0x55 -> 0x56)

```
Request:  [55]
Response: [56] [isPinSet] [isPINCorrected]
```

- `isPinSet`: 0x00 = no PIN, 0x01 = PIN is set
- `isPINCorrected`: 0x00 = not authenticated, 0x01 = authenticated
- **Authenticated** when: `isPinSet == 0` OR `isPINCorrected == 1`
- **5 failed PIN attempts = 20 minute lockout**

### SetControllerPin (0x59 -> 0x01 ACK)

```
Request:  [59] [PIN bytes...]
Response: [01] [59]
```

## Complete Command Table

### Query Commands (response code = request | 0x80)

| Command | Req | Resp | Response Format |
|---------|-----|------|----------------|
| ModelAndVersion | `02` | `82` | `[82][model_hi][model_lo][ver_major][ver_minor]` (5 bytes) |
| AvailableStations | `03` | `83` | `[83][pageNum][stationMap x4]` (6 bytes, stationMap is 32-bit bitmask) |
| CommandSupport | `04` | `84` | Query supported commands |
| SerialNumber | `05` | `85` | Serial number bytes |
| ControllerFirmwareVersion | `0B` | `8B` | Firmware version |
| CurrentTime | `10` | `90` | Current time |
| CurrentDate | `12` | `92` | Current date |
| RetrieveSchedule | `20` | `A0` | Schedule data (paginated) |
| WaterBudget | `30` | `B0` | Water budget % |
| MonthlyWaterBudget | `32` | `B2` | Monthly water budget |
| RainDelaySetting | `36` | `B6` | Rain delay days |
| CurrentQueue | `3B` | `BB` | Queued irrigation entries |
| CurrentRainSensorState | `3E` | `BE` | Rain sensor active/inactive |
| CurrentStationsActive | `3F` | `BF` | Which stations are running |
| CurrentIrrigationState | `48` | `C8` | `[C8][state]` (2 bytes, state: 0=Off, 1=Auto) |
| ControllerEventTimestamp | `4A` | `CA` | Event timestamp |
| CombinedControllerState | `4C` | `CC` | Full state snapshot (includes controller state at byte 10) |
| RequestCurrentStationError | `3D` | `BD` | Station error status |
| RequestIrrigationStatistics | `4D` | `CD` | Irrigation statistics |
| SecurityStatus | `55` | `56` | `[56][isPinSet][isPINCorrected]` (3 bytes) |
| BatteryAndCommStatus | `5C` | `DC` | `[DC][battMV_hi][battMV_lo][srcType][bleSNR][bleRSSI][loraSNR][loraRSSI]` (8 bytes) |
| GetStationFlowLearnState | `62` | `E2` | Flow learning state |
| GetFlowMonitorState | `63` | `E3` | Flow monitor state |
| GetFlowMonitorRate | `65` | `E5` | Flow monitor rate |

### Action Commands (response = 0x01 ACK)

| Command | Req | Data | Description |
|---------|-----|------|-------------|
| SetCurrentTime | `11` | time bytes | Set controller clock time |
| SetCurrentDate | `13` | date bytes | Set controller clock date |
| SetSchedule | `21` | schedule data | Write schedule/program config |
| SetWaterBudget | `31` | budget bytes | Set water budget % |
| SetMonthlyWaterBudget | `33` | monthly data | Set monthly water budget |
| SetRainDelaySetting | `37` | delay bytes | Set rain delay |
| **ManuallyRunProgram** | **`38`** | **`[programId]`** | **Run a program (A=0, B=1, etc.)** |
| **ManualRunStation** | **`39`** | **`[stationId]`** | **Run a single station** |
| TestAllStations | `3A` | test data | Test all stations |
| TestAllStationsSecond | `2C` | test data | Test all stations (seconds) |
| **StopIrrigation** | **`40`** | **(none)** | **Stop all irrigation** |
| **AdvanceStation** | **`42`** | data | **Advance to next station** |
| SetCurrentControllerState | `49` | `[state]` | Set Off(0) or Auto(1) |
| **StackManuallyRunStation** | **`4B`** | **station+duration** | **Queue station run (minutes)** |
| StackManuallyRunStationSecond | `2B` | station+duration | Queue station run (seconds) |
| **PauseIrrigation** | **`2D`** | **(none)** | **Pause watering** |
| **ResumeIrrigation** | **`2E`** | **(none)** | **Resume watering** |
| SetControllerPin | `59` | PIN bytes | Set/change PIN |
| SetFactoryDefaultsRequest | `57` | (none) | Factory reset |
| RequestRasterTest | `58` | test data | Raster test |
| StartStationFlowLearn | `60` | station data | Start flow learning |
| CancelStationFlowLearn | `61` | (none) | Cancel flow learning |
| SetFlowMonitorState | `64` | state data | Set flow monitor |

## Key Response Formats (Detailed)

### ModelAndVersion (0x02 -> 0x82)
```
Response: [82] [model_byte1] [model_byte2] [version_major] [version_minor]
Size: 5 bytes
model = hex string of bytes 1-2
version = "major.minor"
```

### AvailableStations (0x03 -> 0x83)
```
Response: [83] [pageNumber] [stationMap byte1-4]
Size: 6 bytes
stationMap = 32-bit bitmask, bit count = number of stations
```

### BatteryAndCommStatus (0x5C -> 0xDC)
```
Response: [DC] [battMV_hi] [battMV_lo] [sourceType] [bleSNR] [bleRSSI] [loraSNR] [loraRSSI]
Size: 8 bytes
batteryVoltageInMillivolt = (battMV_hi << 8) | battMV_lo  (bytes 1-2, reversed/big-endian)
```

### CurrentIrrigationState (0x48 -> 0xC8)
```
Response: [C8] [state]
Size: 2 bytes
state: 0 = Off, 1 = Auto
```

### SecurityStatus (0x55 -> 0x56)
```
Response: [56] [isPinSet] [isPINCorrected]
Size: 3 bytes
Authenticated if: isPinSet==0 OR isPINCorrected==1
```

## Controller States

| Value | State | Description |
|-------|-------|-------------|
| 0 | Off | Controller is off |
| 1 | Auto | Controller is in auto/run mode |

## Queue Entry Format

Each queued irrigation entry uses 3 bytes:
```
Byte 0: (programId+1) << 4 | stationId   (upper nibble = program+1, lower = station)
Byte 1-2: runtime in seconds (big-endian)
```

## Key Source Files (in decompiled APK)

| File | Role |
|------|------|
| `rb2/ble/x.java` | SIP command enum (all 53 commands with opcodes) |
| `rb2/ble/h.java` | BLE communication handler (GATT writes, notifications) |
| `rb2/ble/d.java` | SIP client implementation (high-level command methods) |
| `rb2/ble/BleHelper.java` | GATT callback handling, connection management |
| `rb2/ble/sipResponse/*.java` | Response parsers for each command type |
| `rb2/ble/model/*.java` | Data models (state, queue, schedule, errors) |

## Implementation Notes

- Commands are sent as hex string → byte array conversion (e.g., "40" → `[0x40]`)
- The app uses `lf.t.s(hexString)` to convert hex strings to byte arrays
- FE46 is the primary write characteristic for SIP commands
- FE47 is the primary notify characteristic for SIP responses
- FE44/FE45 are used for OTA firmware updates only
- 100ms delays between writes observed in test/mock mode
- Large data transfers (schedules) use pagination

## Live Test Results (BAT-BT-4 579A, 2026-02-22)

All commands sent via nRF Connect on iPhone. Protocol confirmed working.

| Command | Sent (FE46) | Response (FE47) | Decoded |
|---------|-------------|-----------------|---------|
| SecurityStatus | `55` | `56 00 00` | No PIN set, no auth needed |
| ModelAndVersion | `02` | `82 00 0B 02 0A` | Model 000B, firmware v2.10 |
| BatteryStatus | `5C` | `DC 3D 15 00 00 A9 00 00` | ~5.4V battery, BLE RSSI -87dBm |
| IrrigationState | `48` | `C8 01` | Auto/On mode |
| AvailableStations | `03` | `83 00 0F 00 00 00` | 4 stations (bitmask 0x0F) |
