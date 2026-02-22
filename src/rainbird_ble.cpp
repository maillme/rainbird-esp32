#include "rainbird_ble.h"

static const char* TAG = "RainBirdBLE";

RainBirdBLE* RainBirdBLE::_instance = nullptr;

void RainBirdBLE::init() {
    _instance = this;
    NimBLEDevice::init("RainBird-Bridge");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    Serial.printf("[BLE] Initialized, BLE address: %s\n",
                  NimBLEDevice::getAddress().toString().c_str());
}

void RainBirdBLE::notifyCallback(NimBLERemoteCharacteristic* pChar,
                                  uint8_t* pData, size_t length, bool isNotify) {
    if (!_instance || length == 0) return;

    size_t copyLen = (length > RESP_BUF_SIZE) ? RESP_BUF_SIZE : length;
    memcpy(_instance->_responseBuf, pData, copyLen);
    _instance->_responseLen = copyLen;
    _instance->_responseReceived = true;

    Serial.printf("[BLE] Notify received (%d bytes): ", length);
    for (size_t i = 0; i < copyLen; i++) {
        Serial.printf("%02X ", pData[i]);
    }
    Serial.println();
}

bool RainBirdBLE::connect() {
    if (_client && _client->isConnected()) return true;

    if (!_client) {
        _client = NimBLEDevice::createClient();
    }

    // Try cached address first (instant connect, no scan needed)
    if (_addrCached) {
        Serial.printf("[BLE] Connecting to cached addr %s...\n", _cachedAddr.toString().c_str());
        if (_client->connect(_cachedAddr)) {
            goto connected;
        }
        Serial.println("[BLE] Cached addr failed, falling back to scan");
        _addrCached = false;
    }

    // Full scan
    {
        Serial.println("[BLE] Scanning for Rain Bird (5s)...");
        NimBLEScan* pScan = NimBLEDevice::getScan();
        pScan->setActiveScan(true);
        pScan->setInterval(100);
        pScan->setWindow(99);
        NimBLEScanResults results = pScan->getResults(5000, false);

        const NimBLEAdvertisedDevice* targetDevice = nullptr;
        for (int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice* device = results.getDevice(i);
            if (device->getName() == RAINBIRD_DEVICE_NAME) {
                targetDevice = device;
                break;
            }
        }

        if (!targetDevice) {
            Serial.printf("[BLE] Rain Bird not found (%d devices scanned)\n", results.getCount());
            pScan->clearResults();
            return false;
        }

        Serial.printf("[BLE] Found %s (rssi %d), connecting...\n",
                      targetDevice->getName().c_str(), targetDevice->getRSSI());

        if (!_client->connect(targetDevice)) {
            Serial.println("[BLE] Connection failed");
            pScan->clearResults();
            return false;
        }

        // Cache the address for future fast connects
        _cachedAddr = targetDevice->getAddress();
        _addrCached = true;
        pScan->clearResults();
    }

connected:

    Serial.println("[BLE] Connected, discovering services...");

    NimBLERemoteService* pService = _client->getService(RAINBIRD_SERVICE_UUID);
    if (!pService) {
        Serial.println("[BLE] Service FE40 not found");
        _client->disconnect();
        return false;
    }

    _writeChar = pService->getCharacteristic(RAINBIRD_WRITE_UUID);
    _notifyChar = pService->getCharacteristic(RAINBIRD_NOTIFY_UUID);

    if (!_writeChar || !_notifyChar) {
        Serial.println("[BLE] Characteristics FE46/FE47 not found");
        _client->disconnect();
        return false;
    }

    // Subscribe to notifications on FE47
    if (!_notifyChar->subscribe(true, notifyCallback)) {
        Serial.println("[BLE] Failed to subscribe to FE47 notifications");
        _client->disconnect();
        return false;
    }

    Serial.println("[BLE] Ready - FE46 write, FE47 notify subscribed");
    return true;
}

void RainBirdBLE::disconnect() {
    if (_client && _client->isConnected()) {
        _client->disconnect();
        Serial.println("[BLE] Disconnected");
    }
}

bool RainBirdBLE::isConnected() {
    return _client && _client->isConnected();
}

bool RainBirdBLE::waitForResponse(unsigned long timeoutMs) {
    unsigned long start = millis();
    while (!_responseReceived) {
        if (millis() - start > timeoutMs) {
            Serial.println("[BLE] Response timeout");
            return false;
        }
        delay(10);
    }
    return true;
}

bool RainBirdBLE::sendCommand(const uint8_t* cmdBuf, size_t cmdLen,
                               uint8_t* respBuf, size_t respBufSize, size_t& respLen) {
    if (!_writeChar || !_notifyChar) return false;

    _responseReceived = false;
    _responseLen = 0;

    Serial.printf("[BLE] Sending command: ");
    for (size_t i = 0; i < cmdLen; i++) {
        Serial.printf("%02X ", cmdBuf[i]);
    }
    Serial.println();

    if (!_writeChar->writeValue(cmdBuf, cmdLen, true)) {
        Serial.println("[BLE] Write failed");
        return false;
    }

    if (!waitForResponse(BLE_RESPONSE_TIMEOUT_MS)) {
        return false;
    }

    respLen = (_responseLen > respBufSize) ? respBufSize : _responseLen;
    memcpy(respBuf, _responseBuf, respLen);
    return true;
}

bool RainBirdBLE::sendSimpleCommand(uint8_t opcode,
                                     uint8_t* respBuf, size_t respBufSize, size_t& respLen) {
    return sendCommand(&opcode, 1, respBuf, respBufSize, respLen);
}

// --- Session-managed commands (connect, send, disconnect) ---

bool RainBirdBLE::executeCommand(const uint8_t* cmdBuf, size_t cmdLen,
                                  uint8_t* respBuf, size_t respBufSize, size_t& respLen) {
    bool wasConnected = isConnected();

    if (!wasConnected && !connect()) {
        return false;
    }

    bool ok = sendCommand(cmdBuf, cmdLen, respBuf, respBufSize, respLen);

    if (!wasConnected) {
        disconnect();
    }

    return ok;
}

// --- High-level Query Commands ---

bool RainBirdBLE::querySecurityStatus(SIP::SecurityStatusResponse& resp) {
    uint8_t buf[16];
    size_t len;
    uint8_t cmd = SIP::SECURITY_STATUS;
    if (!executeCommand(&cmd, 1, buf, sizeof(buf), len)) return false;
    return SIP::parseSecurityStatus(buf, len, resp);
}

bool RainBirdBLE::queryModelVersion(SIP::ModelVersionResponse& resp) {
    uint8_t buf[16];
    size_t len;
    uint8_t cmd = SIP::MODEL_VERSION;
    if (!executeCommand(&cmd, 1, buf, sizeof(buf), len)) return false;
    return SIP::parseModelVersion(buf, len, resp);
}

bool RainBirdBLE::queryBatteryStatus(SIP::BatteryStatusResponse& resp) {
    uint8_t buf[16];
    size_t len;
    uint8_t cmd = SIP::BATTERY_STATUS;
    if (!executeCommand(&cmd, 1, buf, sizeof(buf), len)) return false;
    return SIP::parseBatteryStatus(buf, len, resp);
}

bool RainBirdBLE::queryIrrigationState(SIP::IrrigationStateResponse& resp) {
    uint8_t buf[16];
    size_t len;
    uint8_t cmd = SIP::IRRIGATION_STATE;
    if (!executeCommand(&cmd, 1, buf, sizeof(buf), len)) return false;
    return SIP::parseIrrigationState(buf, len, resp);
}

bool RainBirdBLE::queryAvailableStations(SIP::AvailableStationsResponse& resp) {
    uint8_t buf[16];
    size_t len;
    uint8_t cmd = SIP::AVAILABLE_STATIONS;
    if (!executeCommand(&cmd, 1, buf, sizeof(buf), len)) return false;
    return SIP::parseAvailableStations(buf, len, resp);
}

bool RainBirdBLE::queryRainSensor(SIP::RainSensorResponse& resp) {
    uint8_t buf[16];
    size_t len;
    uint8_t cmd = SIP::RAIN_SENSOR_STATE;
    if (!executeCommand(&cmd, 1, buf, sizeof(buf), len)) return false;
    return SIP::parseRainSensor(buf, len, resp);
}

bool RainBirdBLE::queryStationsActive(SIP::StationsActiveResponse& resp) {
    uint8_t buf[16];
    size_t len;
    uint8_t cmd = SIP::STATIONS_ACTIVE;
    if (!executeCommand(&cmd, 1, buf, sizeof(buf), len)) return false;
    return SIP::parseStationsActive(buf, len, resp);
}

bool RainBirdBLE::queryRainDelay(SIP::RainDelayResponse& resp) {
    uint8_t buf[16];
    size_t len;
    uint8_t cmd = SIP::RAIN_DELAY;
    if (!executeCommand(&cmd, 1, buf, sizeof(buf), len)) return false;
    return SIP::parseRainDelay(buf, len, resp);
}

bool RainBirdBLE::queryWaterBudget(SIP::WaterBudgetResponse& resp) {
    uint8_t buf[16];
    size_t len;
    uint8_t cmd = SIP::WATER_BUDGET;
    if (!executeCommand(&cmd, 1, buf, sizeof(buf), len)) return false;
    return SIP::parseWaterBudget(buf, len, resp);
}

// --- High-level Action Commands ---

bool RainBirdBLE::stopIrrigation() {
    uint8_t cmdBuf[4], respBuf[16];
    size_t cmdLen = SIP::buildStopIrrigation(cmdBuf);
    size_t respLen;
    if (!executeCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen)) return false;
    uint8_t echo;
    return SIP::parseAck(respBuf, respLen, &echo);
}

bool RainBirdBLE::pauseIrrigation() {
    uint8_t cmdBuf[4], respBuf[16];
    size_t cmdLen = SIP::buildPauseIrrigation(cmdBuf);
    size_t respLen;
    if (!executeCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen)) return false;
    uint8_t echo;
    return SIP::parseAck(respBuf, respLen, &echo);
}

bool RainBirdBLE::resumeIrrigation() {
    uint8_t cmdBuf[4], respBuf[16];
    size_t cmdLen = SIP::buildResumeIrrigation(cmdBuf);
    size_t respLen;
    if (!executeCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen)) return false;
    uint8_t echo;
    return SIP::parseAck(respBuf, respLen, &echo);
}

bool RainBirdBLE::advanceStation() {
    uint8_t cmdBuf[4], respBuf[16];
    size_t cmdLen = SIP::buildAdvanceStation(cmdBuf);
    size_t respLen;
    if (!executeCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen)) return false;
    uint8_t echo;
    return SIP::parseAck(respBuf, respLen, &echo);
}

bool RainBirdBLE::runStation(uint8_t stationId, uint16_t durationMinutes) {
    // Manage BLE session ourselves so we can try multiple commands on one connection
    bool wasConnected = isConnected();
    if (!wasConnected && !connect()) return false;

    uint8_t cmdBuf[8], respBuf[16];
    size_t cmdLen, respLen;
    uint8_t echo;
    bool ok = false;
    uint8_t durMin = (durationMinutes > 255) ? 255 : (uint8_t)durationMinutes;

    // Try StackRunStation (0x4B) first — confirmed working on BAT-BT-4
    // Format: 4B 00 <station> <durationMinutes> (4 bytes, station as 2-byte BE)
    cmdLen = SIP::buildStackRunStation(stationId, durMin, cmdBuf);
    Serial.printf("[BLE] RunStation (0x4B) station=%d duration=%dmin\n", stationId, durMin);
    if (sendCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen) &&
        SIP::parseAck(respBuf, respLen, &echo)) {
        Serial.printf("[BLE] Station %d started (%d min)\n", stationId, durMin);
        ok = true;
    }

    // Fallback: ManualRunStation (0x39)
    if (!ok) {
        Serial.printf("[BLE] 0x4B NAK (resp: %02X %02X %02X), trying 0x39...\n",
                      respLen > 0 ? respBuf[0] : 0xFF,
                      respLen > 1 ? respBuf[1] : 0xFF,
                      respLen > 2 ? respBuf[2] : 0xFF);
        cmdLen = SIP::buildManualRunStation(stationId, durMin, cmdBuf);
        if (sendCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen) &&
            SIP::parseAck(respBuf, respLen, &echo)) {
            Serial.printf("[BLE] Station %d started via 0x39 (%d min)\n", stationId, durMin);
            ok = true;
        }
    }

    // Fallback: StackRunStationSec (0x2B) with seconds
    if (!ok) {
        Serial.printf("[BLE] 0x39 NAK, trying 0x2B (seconds)...\n");
        uint16_t durationSec = durationMinutes * 60;
        cmdLen = SIP::buildStackRunStationSec(stationId, durationSec, cmdBuf);
        if (sendCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen) &&
            SIP::parseAck(respBuf, respLen, &echo)) {
            Serial.printf("[BLE] Station %d started via 0x2B (%ds)\n", stationId, durationSec);
            ok = true;
        }
    }

    if (!ok) {
        Serial.printf("[BLE] ALL run commands failed for station %d (last resp: %02X %02X %02X)\n",
                      stationId,
                      respLen > 0 ? respBuf[0] : 0xFF,
                      respLen > 1 ? respBuf[1] : 0xFF,
                      respLen > 2 ? respBuf[2] : 0xFF);
    }

    if (!wasConnected) disconnect();
    return ok;
}

bool RainBirdBLE::runProgram(uint8_t programId) {
    uint8_t cmdBuf[4], respBuf[16];
    size_t cmdLen = SIP::buildManualRunProgram(programId, cmdBuf);
    size_t respLen;
    if (!executeCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen)) return false;
    uint8_t echo;
    return SIP::parseAck(respBuf, respLen, &echo);
}

bool RainBirdBLE::setControllerState(uint8_t state) {
    uint8_t cmdBuf[4], respBuf[16];
    size_t cmdLen = SIP::buildSetControllerState(state, cmdBuf);
    size_t respLen;
    if (!executeCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen)) return false;
    uint8_t echo;
    return SIP::parseAck(respBuf, respLen, &echo);
}

bool RainBirdBLE::setRainDelay(uint16_t days) {
    uint8_t cmdBuf[4], respBuf[16];
    size_t cmdLen = SIP::buildSetRainDelay(days, cmdBuf);
    size_t respLen;
    if (!executeCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen)) return false;
    uint8_t echo;
    return SIP::parseAck(respBuf, respLen, &echo);
}

bool RainBirdBLE::setWaterBudget(uint8_t programId, uint8_t percent) {
    uint8_t cmdBuf[4], respBuf[16];
    size_t cmdLen = SIP::buildSetWaterBudget(programId, percent, cmdBuf);
    size_t respLen;
    if (!executeCommand(cmdBuf, cmdLen, respBuf, sizeof(respBuf), respLen)) return false;
    uint8_t echo;
    return SIP::parseAck(respBuf, respLen, &echo);
}
