#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "config.h"
#include "sip_protocol.h"

class RainBirdBLE {
public:
    void init();

    // Connect to Rain Bird controller. Returns true on success.
    bool connect();

    // Disconnect from controller.
    void disconnect();

    bool isConnected();

    // Send a SIP command and wait for response.
    // cmdBuf/cmdLen: command bytes to write to FE46
    // respBuf: buffer to receive response from FE47
    // respLen: set to actual response length on return
    // Returns true if command sent and response received.
    bool sendCommand(const uint8_t* cmdBuf, size_t cmdLen,
                     uint8_t* respBuf, size_t respBufSize, size_t& respLen);

    // Convenience: send a simple 1-byte command
    bool sendSimpleCommand(uint8_t opcode, uint8_t* respBuf, size_t respBufSize, size_t& respLen);

    // High-level commands that connect, send, and disconnect
    bool querySecurityStatus(SIP::SecurityStatusResponse& resp);
    bool queryModelVersion(SIP::ModelVersionResponse& resp);
    bool queryBatteryStatus(SIP::BatteryStatusResponse& resp);
    bool queryIrrigationState(SIP::IrrigationStateResponse& resp);
    bool queryAvailableStations(SIP::AvailableStationsResponse& resp);
    bool queryRainSensor(SIP::RainSensorResponse& resp);
    bool queryStationsActive(SIP::StationsActiveResponse& resp);
    bool queryRainDelay(SIP::RainDelayResponse& resp);
    bool queryWaterBudget(SIP::WaterBudgetResponse& resp);

    bool stopIrrigation();
    bool pauseIrrigation();
    bool resumeIrrigation();
    bool advanceStation();
    bool runStation(uint8_t stationId, uint16_t durationMinutes);
    bool runProgram(uint8_t programId);
    bool setControllerState(uint8_t state);
    bool setRainDelay(uint16_t days);
    bool setWaterBudget(uint8_t programId, uint8_t percent);

private:
    NimBLEClient* _client = nullptr;
    NimBLERemoteCharacteristic* _writeChar = nullptr;
    NimBLERemoteCharacteristic* _notifyChar = nullptr;

    // Notification response buffer
    static constexpr size_t RESP_BUF_SIZE = 64;
    volatile bool _responseReceived;
    uint8_t _responseBuf[RESP_BUF_SIZE];
    size_t _responseLen;

    static void notifyCallback(NimBLERemoteCharacteristic* pChar,
                               uint8_t* pData, size_t length, bool isNotify);
    static RainBirdBLE* _instance;

    // Cached address from first successful scan
    NimBLEAddress _cachedAddr;
    bool _addrCached = false;

    bool waitForResponse(unsigned long timeoutMs);

    // Connect + send + get response (manages BLE session)
    bool executeCommand(const uint8_t* cmdBuf, size_t cmdLen,
                        uint8_t* respBuf, size_t respBufSize, size_t& respLen);
};
