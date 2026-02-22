#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "rainbird_ble.h"
#include "mqtt_handler.h"

RainBirdBLE ble;
MqttHandler mqtt;

unsigned long lastStatusPoll = 0;
bool initialPollDone = false;
unsigned long followUpPollAt = 0;  // Schedule a poll after station duration expires

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());

        // Enable WiFi light sleep for power saving (~1-2mA vs ~100mA)
        esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
        Serial.println("[WiFi] Power save mode enabled");
    } else {
        Serial.println("\n[WiFi] Connection failed, retrying in 10s");
        delay(10000);
    }
}

void pollStatus() {
    Serial.println("[Poll] Starting status poll...");

    // Connect BLE once for all queries
    if (!ble.connect()) {
        Serial.println("[Poll] BLE connect failed, retrying next interval");
        lastStatusPoll = millis();
        return;
    }

    // Battery status
    {
        SIP::BatteryStatusResponse resp;
        uint8_t cmd = SIP::BATTERY_STATUS;
        uint8_t buf[16];
        size_t len;
        if (ble.sendSimpleCommand(cmd, buf, sizeof(buf), len) &&
            SIP::parseBatteryStatus(buf, len, resp)) {
            mqtt.publishBatteryVoltage(resp.batteryMillivolts);
            mqtt.publishBleRssi(resp.bleRssi);
            // Convert mV to percentage (4x AA alkaline: 4000 mV dead, 6400 mV fresh)
            int pct = ((int)resp.batteryMillivolts - 4000) * 100 / 2400;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            mqtt.publishBatteryPercent((uint8_t)pct);
            Serial.printf("[Poll] Battery: %d mV (%d%%), RSSI: %d dBm\n",
                          resp.batteryMillivolts, pct, resp.bleRssi);
        }
    }

    // Irrigation state
    {
        SIP::IrrigationStateResponse resp;
        uint8_t cmd = SIP::IRRIGATION_STATE;
        uint8_t buf[16];
        size_t len;
        if (ble.sendSimpleCommand(cmd, buf, sizeof(buf), len) &&
            SIP::parseIrrigationState(buf, len, resp)) {
            mqtt.publishIrrigationState(resp.isAuto());
            Serial.printf("[Poll] State: %s\n", resp.isAuto() ? "Auto" : "Off");
        }
    }

    // Active stations
    {
        SIP::StationsActiveResponse resp;
        uint8_t cmd = SIP::STATIONS_ACTIVE;
        uint8_t buf[16];
        size_t len;
        if (ble.sendSimpleCommand(cmd, buf, sizeof(buf), len) &&
            SIP::parseStationsActive(buf, len, resp)) {
            mqtt.publishStationsActive(resp.activeBitmask);
            Serial.printf("[Poll] Active stations bitmask: 0x%02X\n", resp.activeBitmask);
        }
    }

    // Rain sensor
    {
        SIP::RainSensorResponse resp;
        uint8_t cmd = SIP::RAIN_SENSOR_STATE;
        uint8_t buf[16];
        size_t len;
        if (ble.sendSimpleCommand(cmd, buf, sizeof(buf), len) &&
            SIP::parseRainSensor(buf, len, resp)) {
            mqtt.publishRainSensor(resp.isActive);
        }
    }

    // Rain delay
    {
        SIP::RainDelayResponse resp;
        uint8_t cmd = SIP::RAIN_DELAY;
        uint8_t buf[16];
        size_t len;
        if (ble.sendSimpleCommand(cmd, buf, sizeof(buf), len) &&
            SIP::parseRainDelay(buf, len, resp)) {
            mqtt.publishRainDelay(resp.delayDays);
        }
    }

    // Water budget
    {
        SIP::WaterBudgetResponse resp;
        uint8_t cmd = SIP::WATER_BUDGET;
        uint8_t buf[16];
        size_t len;
        if (ble.sendSimpleCommand(cmd, buf, sizeof(buf), len) &&
            SIP::parseWaterBudget(buf, len, resp)) {
            mqtt.publishWaterBudget(resp.budgetPercent);
        }
    }

    // Model & firmware + command support diagnostics (only on first poll)
    if (!initialPollDone) {
        SIP::ModelVersionResponse resp;
        uint8_t cmd = SIP::MODEL_VERSION;
        uint8_t buf[16];
        size_t len;
        if (ble.sendSimpleCommand(cmd, buf, sizeof(buf), len) &&
            SIP::parseModelVersion(buf, len, resp)) {
            mqtt.publishFirmwareVersion(resp.versionMajor, resp.versionMinor);
            Serial.printf("[Poll] Model: 0x%04X, FW: %d.%d\n",
                          resp.modelId, resp.versionMajor, resp.versionMinor);
        }

        // Check which run station commands are supported
        const uint8_t testCmds[] = { 0x39, 0x4B, 0x2B, 0x40, 0x42, 0x38, 0x3A, 0x2C };
        const char* testNames[] = { "ManualRunStation", "StackRunStation", "StackRunStationSec",
                                    "StopIrrigation", "AdvanceStation", "ManualRunProgram",
                                    "TestAllStations", "TestAllStationsSec" };
        Serial.println("[Poll] Command support check:");
        for (int i = 0; i < 8; i++) {
            uint8_t cmdBuf[2] = { SIP::COMMAND_SUPPORT, testCmds[i] };
            if (ble.sendCommand(cmdBuf, 2, buf, sizeof(buf), len) && len >= 3 && buf[0] == 0x84) {
                Serial.printf("[Poll]   0x%02X %-22s = %s\n",
                              testCmds[i], testNames[i],
                              buf[2] ? "SUPPORTED" : "NOT SUPPORTED");
            } else {
                Serial.printf("[Poll]   0x%02X %-22s = query failed\n", testCmds[i], testNames[i]);
            }
        }
    }

    ble.disconnect();
    initialPollDone = true;
    lastStatusPoll = millis();
    Serial.println("[Poll] Status poll complete");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== Rain Bird BLE-to-MQTT Bridge ===");
    Serial.println("Starting...");

    // Init BLE (but don't connect yet)
    ble.init();

    // Connect WiFi
    connectWiFi();

    // Init MQTT
    mqtt.init(&ble);
}

void loop() {
    // Ensure WiFi is connected
    connectWiFi();

    // Handle MQTT
    mqtt.loop();

    // Execute any queued BLE commands from MQTT callbacks
    mqtt.processPendingCommand();

    // If a station was just started, schedule follow-up poll after duration + 30s
    uint16_t startedDur = mqtt.consumeStartedDuration();
    if (startedDur > 0) {
        followUpPollAt = millis() + ((unsigned long)startedDur * 60000UL) + 30000UL;
        Serial.printf("[Main] Follow-up poll scheduled in %d min 30s\n", startedDur);
    }

    // Status poll: first attempt right away, then every STATUS_POLL_INTERVAL_MS
    // Also poll at follow-up time after station run completes
    if (mqtt.isConnected()) {
        bool shouldPoll = !initialPollDone || (millis() - lastStatusPoll >= STATUS_POLL_INTERVAL_MS);
        if (!shouldPoll && followUpPollAt > 0 && millis() >= followUpPollAt) {
            shouldPoll = true;
            followUpPollAt = 0;
            Serial.println("[Main] Follow-up poll triggered (station duration expired)");
        }
        if (shouldPoll) {
            initialPollDone = true;
            pollStatus();
        }
    }
}
