#pragma once

#include <Arduino.h>

// SIP (Standard Irrigation Protocol) command opcodes
// Write these bytes to BLE characteristic FE46
// Responses arrive on FE47

namespace SIP {

// Query commands (response code = opcode | 0x80)
constexpr uint8_t MODEL_VERSION        = 0x02;
constexpr uint8_t AVAILABLE_STATIONS   = 0x03;
constexpr uint8_t COMMAND_SUPPORT      = 0x04;
constexpr uint8_t SERIAL_NUMBER        = 0x05;
constexpr uint8_t FIRMWARE_VERSION     = 0x0B;
constexpr uint8_t CURRENT_TIME         = 0x10;
constexpr uint8_t CURRENT_DATE         = 0x12;
constexpr uint8_t RETRIEVE_SCHEDULE    = 0x20;
constexpr uint8_t WATER_BUDGET         = 0x30;
constexpr uint8_t MONTHLY_WATER_BUDGET = 0x32;
constexpr uint8_t RAIN_DELAY           = 0x36;
constexpr uint8_t CURRENT_QUEUE        = 0x3B;
constexpr uint8_t STATION_ERROR        = 0x3D;
constexpr uint8_t RAIN_SENSOR_STATE    = 0x3E;
constexpr uint8_t STATIONS_ACTIVE      = 0x3F;
constexpr uint8_t IRRIGATION_STATE     = 0x48;
constexpr uint8_t EVENT_TIMESTAMP      = 0x4A;
constexpr uint8_t COMBINED_STATE       = 0x4C;
constexpr uint8_t IRRIGATION_STATS     = 0x4D;
constexpr uint8_t SECURITY_STATUS      = 0x55;
constexpr uint8_t BATTERY_STATUS       = 0x5C;

// Action commands (response = 0x01 ACK)
constexpr uint8_t SET_CURRENT_TIME     = 0x11;
constexpr uint8_t SET_CURRENT_DATE     = 0x13;
constexpr uint8_t SET_SCHEDULE         = 0x21;
constexpr uint8_t STACK_RUN_STATION_SEC = 0x2B;
constexpr uint8_t TEST_ALL_STATIONS_SEC = 0x2C;
constexpr uint8_t PAUSE_IRRIGATION     = 0x2D;
constexpr uint8_t RESUME_IRRIGATION    = 0x2E;
constexpr uint8_t SET_WATER_BUDGET     = 0x31;
constexpr uint8_t SET_MONTHLY_BUDGET   = 0x33;
constexpr uint8_t SET_RAIN_DELAY       = 0x37;
constexpr uint8_t MANUAL_RUN_PROGRAM   = 0x38;
constexpr uint8_t MANUAL_RUN_STATION   = 0x39;
constexpr uint8_t TEST_ALL_STATIONS    = 0x3A;
constexpr uint8_t STOP_IRRIGATION      = 0x40;
constexpr uint8_t ADVANCE_STATION      = 0x42;
constexpr uint8_t SET_CONTROLLER_STATE = 0x49;
constexpr uint8_t STACK_RUN_STATION    = 0x4B;
constexpr uint8_t SET_FACTORY_DEFAULTS = 0x57;
constexpr uint8_t SET_CONTROLLER_PIN   = 0x59;

// Response codes
constexpr uint8_t ACK = 0x01;
constexpr uint8_t NAK = 0x00;

// Parsed response structs

struct SecurityStatusResponse {
    bool isPinSet;
    bool isPinCorrect;
    bool isAuthenticated() const { return !isPinSet || isPinCorrect; }
};

struct ModelVersionResponse {
    uint16_t modelId;
    uint8_t versionMajor;
    uint8_t versionMinor;
};

struct BatteryStatusResponse {
    uint16_t batteryMillivolts;
    uint8_t sourceType;
    int8_t bleRssi;
};

struct IrrigationStateResponse {
    uint8_t state;  // 0 = Off, 1 = Auto
    bool isAuto() const { return state == 1; }
};

struct AvailableStationsResponse {
    uint8_t pageNumber;
    uint32_t stationBitmask;
    uint8_t stationCount;
};

struct RainDelayResponse {
    uint16_t delayDays;
};

struct WaterBudgetResponse {
    uint8_t programId;
    uint8_t budgetPercent;
};

struct RainSensorResponse {
    bool isActive;
};

struct StationsActiveResponse {
    uint32_t activeBitmask;
};

// Command builders — return the byte array to write to FE46

size_t buildSimpleCommand(uint8_t opcode, uint8_t* buf);
size_t buildStopIrrigation(uint8_t* buf);
size_t buildPauseIrrigation(uint8_t* buf);
size_t buildResumeIrrigation(uint8_t* buf);
size_t buildAdvanceStation(uint8_t* buf);
size_t buildManualRunStation(uint8_t stationId, uint8_t durationMinutes, uint8_t* buf);
size_t buildStackRunStation(uint8_t stationId, uint8_t durationMinutes, uint8_t* buf);
size_t buildStackRunStationSec(uint8_t stationId, uint16_t durationSeconds, uint8_t* buf);
size_t buildManualRunProgram(uint8_t programId, uint8_t* buf);
size_t buildSetControllerState(uint8_t state, uint8_t* buf);
size_t buildSetRainDelay(uint16_t days, uint8_t* buf);
size_t buildSetWaterBudget(uint8_t programId, uint8_t percent, uint8_t* buf);

// Response parsers — parse the byte array received from FE47

bool parseAck(const uint8_t* data, size_t len, uint8_t* echoedCommand);
bool parseSecurityStatus(const uint8_t* data, size_t len, SecurityStatusResponse& resp);
bool parseModelVersion(const uint8_t* data, size_t len, ModelVersionResponse& resp);
bool parseBatteryStatus(const uint8_t* data, size_t len, BatteryStatusResponse& resp);
bool parseIrrigationState(const uint8_t* data, size_t len, IrrigationStateResponse& resp);
bool parseAvailableStations(const uint8_t* data, size_t len, AvailableStationsResponse& resp);
bool parseRainDelay(const uint8_t* data, size_t len, RainDelayResponse& resp);
bool parseWaterBudget(const uint8_t* data, size_t len, WaterBudgetResponse& resp);
bool parseRainSensor(const uint8_t* data, size_t len, RainSensorResponse& resp);
bool parseStationsActive(const uint8_t* data, size_t len, StationsActiveResponse& resp);

} // namespace SIP
