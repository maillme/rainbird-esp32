#include "sip_protocol.h"

namespace SIP {

// --- Command Builders ---

size_t buildSimpleCommand(uint8_t opcode, uint8_t* buf) {
    buf[0] = opcode;
    return 1;
}

size_t buildStopIrrigation(uint8_t* buf) {
    return buildSimpleCommand(STOP_IRRIGATION, buf);
}

size_t buildPauseIrrigation(uint8_t* buf) {
    return buildSimpleCommand(PAUSE_IRRIGATION, buf);
}

size_t buildResumeIrrigation(uint8_t* buf) {
    return buildSimpleCommand(RESUME_IRRIGATION, buf);
}

size_t buildAdvanceStation(uint8_t* buf) {
    return buildSimpleCommand(ADVANCE_STATION, buf);
}

size_t buildManualRunStation(uint8_t stationId, uint8_t durationMinutes, uint8_t* buf) {
    buf[0] = MANUAL_RUN_STATION;        // 0x39
    buf[1] = 0x00;                       // Station ID high byte
    buf[2] = stationId;                  // Station ID low byte
    buf[3] = durationMinutes;            // Duration (1 byte, minutes)
    return 4;
}

size_t buildStackRunStation(uint8_t stationId, uint8_t durationMinutes, uint8_t* buf) {
    buf[0] = STACK_RUN_STATION;          // 0x4B
    buf[1] = 0x00;                       // Station ID high byte
    buf[2] = stationId;                  // Station ID low byte
    buf[3] = durationMinutes;            // Duration (1 byte, minutes)
    return 4;
}

size_t buildStackRunStationSec(uint8_t stationId, uint16_t durationSeconds, uint8_t* buf) {
    buf[0] = STACK_RUN_STATION_SEC;      // 0x2B
    buf[1] = 0x00;                       // Station ID high byte
    buf[2] = stationId;                  // Station ID low byte
    buf[3] = (uint8_t)(durationSeconds >> 8);   // Duration high byte
    buf[4] = (uint8_t)(durationSeconds & 0xFF); // Duration low byte
    return 5;
}

size_t buildManualRunProgram(uint8_t programId, uint8_t* buf) {
    buf[0] = MANUAL_RUN_PROGRAM;
    buf[1] = programId;
    return 2;
}

size_t buildSetControllerState(uint8_t state, uint8_t* buf) {
    buf[0] = SET_CONTROLLER_STATE;
    buf[1] = state;
    return 2;
}

size_t buildSetRainDelay(uint16_t days, uint8_t* buf) {
    buf[0] = SET_RAIN_DELAY;
    buf[1] = (uint8_t)(days >> 8);
    buf[2] = (uint8_t)(days & 0xFF);
    return 3;
}

size_t buildSetWaterBudget(uint8_t programId, uint8_t percent, uint8_t* buf) {
    buf[0] = SET_WATER_BUDGET;
    buf[1] = programId;
    buf[2] = percent;
    return 3;
}

// --- Response Parsers ---

bool parseAck(const uint8_t* data, size_t len, uint8_t* echoedCommand) {
    if (len < 1 || data[0] != ACK) return false;
    if (echoedCommand && len >= 2) *echoedCommand = data[1];
    return true;
}

bool parseSecurityStatus(const uint8_t* data, size_t len, SecurityStatusResponse& resp) {
    if (len < 3 || data[0] != 0x56) return false;
    resp.isPinSet = data[1] != 0;
    resp.isPinCorrect = data[2] != 0;
    return true;
}

bool parseModelVersion(const uint8_t* data, size_t len, ModelVersionResponse& resp) {
    if (len < 5 || data[0] != 0x82) return false;
    resp.modelId = ((uint16_t)data[1] << 8) | data[2];
    resp.versionMajor = data[3];
    resp.versionMinor = data[4];
    return true;
}

bool parseBatteryStatus(const uint8_t* data, size_t len, BatteryStatusResponse& resp) {
    if (len < 8 || data[0] != 0xDC) return false;
    // Bytes 1-2: battery voltage in millivolts (little-endian based on live test)
    resp.batteryMillivolts = ((uint16_t)data[2] << 8) | data[1];
    resp.sourceType = data[3];
    resp.bleRssi = (int8_t)data[5];
    return true;
}

bool parseIrrigationState(const uint8_t* data, size_t len, IrrigationStateResponse& resp) {
    if (len < 2 || data[0] != 0xC8) return false;
    resp.state = data[1];
    return true;
}

bool parseAvailableStations(const uint8_t* data, size_t len, AvailableStationsResponse& resp) {
    if (len < 6 || data[0] != 0x83) return false;
    resp.pageNumber = data[1];
    resp.stationBitmask = ((uint32_t)data[2] << 24) | ((uint32_t)data[3] << 16) |
                          ((uint32_t)data[4] << 8) | data[5];
    // Count set bits
    uint32_t n = resp.stationBitmask;
    resp.stationCount = 0;
    while (n) { resp.stationCount += n & 1; n >>= 1; }
    return true;
}

bool parseRainDelay(const uint8_t* data, size_t len, RainDelayResponse& resp) {
    if (len < 3 || data[0] != 0xB6) return false;
    resp.delayDays = ((uint16_t)data[1] << 8) | data[2];
    return true;
}

bool parseWaterBudget(const uint8_t* data, size_t len, WaterBudgetResponse& resp) {
    if (len < 3 || data[0] != 0xB0) return false;
    resp.programId = data[1];
    resp.budgetPercent = data[2];
    return true;
}

bool parseRainSensor(const uint8_t* data, size_t len, RainSensorResponse& resp) {
    if (len < 2 || data[0] != 0xBE) return false;
    resp.isActive = data[1] != 0;
    return true;
}

bool parseStationsActive(const uint8_t* data, size_t len, StationsActiveResponse& resp) {
    if (len < 2 || data[0] != 0xBF) return false;
    resp.activeBitmask = 0;
    for (size_t i = 1; i < len; i++) {
        resp.activeBitmask |= ((uint32_t)data[i] << ((i - 1) * 8));
    }
    return true;
}

} // namespace SIP
