#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <esp_pm.h>
#include <HTTPUpdate.h>
#include "config.h"
#include "rainbird_ble.h"
#include "mqtt_handler.h"

#define WDT_TIMEOUT_SEC 60

RainBirdBLE ble;
MqttHandler mqtt;

unsigned long lastStatusPoll = 0;
unsigned long lastHeartbeat = -HEARTBEAT_INTERVAL_MS;  // Fire immediately on first connect
unsigned long lastHealthcheck = -HEALTHCHECK_INTERVAL_MS;  // Fire immediately on first connect
unsigned long lastReleaseCheck = -RELEASE_CHECK_INTERVAL_MS;  // Fire immediately on first connect
bool initialPollDone = false;
unsigned long followUpPollAt = 0;  // Schedule a poll after station duration expires

// Battery percentage moving average (smooths noisy readings)
#define BATTERY_AVG_SAMPLES 3
int batteryPctBuf[BATTERY_AVG_SAMPLES] = {-1, -1, -1};
int batteryBufIdx = 0;

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
            // Convert mV to percentage using alkaline 4xAA discharge curve
            // Alkaline cells are non-linear: voltage drops fast at first, then plateaus
            int mv = (int)resp.batteryMillivolts;
            int pct;
            if      (mv >= 5800) pct = 100;
            else if (mv >= 5600) pct = 90 + (mv - 5600) * 10 / 200;  // 5600-5800 = 90-100%
            else if (mv >= 5200) pct = 60 + (mv - 5200) * 30 / 400;  // 5200-5600 = 60-90%
            else if (mv >= 4800) pct = 30 + (mv - 4800) * 30 / 400;  // 4800-5200 = 30-60%
            else if (mv >= 4400) pct =  5 + (mv - 4400) * 25 / 400;  // 4400-4800 = 5-30%
            else                 pct = 0;
            // Store in ring buffer and compute moving average
            batteryPctBuf[batteryBufIdx] = pct;
            batteryBufIdx = (batteryBufIdx + 1) % BATTERY_AVG_SAMPLES;
            int sum = 0, count = 0;
            for (int i = 0; i < BATTERY_AVG_SAMPLES; i++) {
                if (batteryPctBuf[i] >= 0) { sum += batteryPctBuf[i]; count++; }
            }
            int avgPct = sum / count;
            mqtt.publishBatteryPercent((uint8_t)avgPct);
            Serial.printf("[Poll] Battery: %d mV (%d%%, avg %d%%), RSSI: %d dBm\n",
                          resp.batteryMillivolts, pct, avgPct, resp.bleRssi);
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

void pingHealthcheck() {
    WiFiClient hcClient;
    if (hcClient.connect(HEALTHCHECK_HOST, 80, 5000)) {
        hcClient.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                        HEALTHCHECK_PATH, HEALTHCHECK_HOST);
        hcClient.stop();
        Serial.println("[Healthcheck] Ping sent");
    } else {
        Serial.println("[Healthcheck] Connection failed");
    }
}

void performOta(const String& url) {
    Serial.printf("[OTA] Starting update from: %s\n", url.c_str());

    // Publish status so HA shows progress
    mqtt.publishAvailability(true);  // keep alive during update

    // Disable watchdog during OTA (download can take >30s)
    esp_task_wdt_delete(NULL);
    Serial.println("[OTA] Watchdog disabled for update");

    WiFiClientSecure otaClient;
    otaClient.setInsecure();  // Skip cert verification (URL is user-provided via MQTT)

    // GitHub release URLs return 302 redirect to CDN — must follow
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    // Feed watchdog during download via progress callback
    httpUpdate.onProgress([](int cur, int total) {
        Serial.printf("[OTA] Progress: %d / %d bytes (%.0f%%)\n", cur, total,
                      total > 0 ? (float)cur / total * 100 : 0);
    });

    t_httpUpdate_return result = httpUpdate.update(otaClient, url);

    // If we get here, the update failed (success would reboot)
    switch (result) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Update failed: %s (err %d)\n",
                          httpUpdate.getLastErrorString().c_str(),
                          httpUpdate.getLastError());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No update available");
            break;
        default:
            Serial.println("[OTA] Unexpected result");
            break;
    }

    // Re-enable watchdog
    esp_task_wdt_add(NULL);
    Serial.println("[OTA] Watchdog re-enabled");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.printf("\n=== Rain Bird BLE-to-MQTT Bridge v%s ===\n", FW_VERSION);
    Serial.println("Starting...");

    // Enable hardware watchdog (auto-reboots if loop hangs)
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    Serial.printf("[WDT] Watchdog enabled (%ds timeout)\n", WDT_TIMEOUT_SEC);

    // Init BLE (but don't connect yet)
    ble.init();

    // Connect WiFi
    connectWiFi();

    // Enable automatic light sleep — CPU sleeps during delay() while WiFi stays associated
    esp_pm_config_esp32c3_t pm_config = {
        .max_freq_mhz = 160,   // Full speed when active
        .min_freq_mhz = 10,    // Minimum when idle
        .light_sleep_enable = true
    };
    esp_err_t pm_err = esp_pm_configure(&pm_config);
    if (pm_err == ESP_OK) {
        Serial.println("[PM] Automatic light sleep enabled (~2-5mA idle)");
    } else {
        Serial.printf("[PM] Light sleep config failed: %d\n", pm_err);
    }

    // Init MQTT
    mqtt.init(&ble);
}

void loop() {
    esp_task_wdt_reset();

    // Ensure WiFi is connected
    connectWiFi();

    // Handle MQTT
    mqtt.loop();

    // Execute any queued BLE commands from MQTT callbacks
    mqtt.processPendingCommand();

    // Check for OTA update request (manual URL via button)
    String otaUrl = mqtt.consumeOtaUrl();
    if (otaUrl.length() > 0) {
        performOta(otaUrl);
    }

    // Check for HA update entity install request (GitHub release)
    String updateUrl = mqtt.consumeUpdateInstallUrl();
    if (updateUrl.length() > 0) {
        performOta(updateUrl);
    }

    // If a station was just started, schedule follow-up poll after duration + 30s
    uint16_t startedDur = mqtt.consumeStartedDuration();
    if (startedDur > 0) {
        followUpPollAt = millis() + ((unsigned long)startedDur * 60000UL) + 30000UL;
        Serial.printf("[Main] Follow-up poll scheduled in %d min 30s\n", startedDur);
    }

    if (mqtt.isConnected()) {
        // Heartbeat: lightweight MQTT publish only (every hour)
        if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
            mqtt.publishHeartbeat();
            mqtt.publishBridgeVersion();
            lastHeartbeat = millis();
        }

        // Healthcheck ping (every 4 hours)
        if (millis() - lastHealthcheck >= HEALTHCHECK_INTERVAL_MS) {
            pingHealthcheck();
            lastHealthcheck = millis();
        }

        // GitHub release check (every 24 hours)
        if (millis() - lastReleaseCheck >= RELEASE_CHECK_INTERVAL_MS) {
            mqtt.checkGitHubRelease();
            lastReleaseCheck = millis();
        }

        // BLE status poll: first attempt right away, then every STATUS_POLL_INTERVAL_MS
        // Also poll at follow-up time after station run completes
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

    delay(200);  // Let CPU idle between loop iterations (reduces heat)
}
