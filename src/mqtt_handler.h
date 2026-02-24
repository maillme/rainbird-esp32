#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"
#include "rainbird_ble.h"

// Pending command types for deferred BLE execution
enum CmdType { CMD_NONE, CMD_RUN_STATION, CMD_STOP, CMD_ADVANCE,
               CMD_SET_MODE, CMD_SET_RAIN_DELAY, CMD_SET_WATER_BUDGET,
               CMD_RUN_PROGRAM, CMD_OTA };

struct PendingCommand {
    CmdType type = CMD_NONE;
    uint8_t station;
    uint16_t duration;
    uint8_t value;
};

class MqttHandler {
public:
    void init(RainBirdBLE* ble);
    void loop();
    bool isConnected();

    // Process queued BLE commands (call from main loop)
    void processPendingCommand();

    // Returns >0 if a station was just started (duration in minutes), resets after read
    uint16_t consumeStartedDuration();

    // Publish state updates to HA
    void publishBatteryVoltage(uint16_t millivolts);
    void publishBatteryPercent(uint8_t percent);
    void publishBleRssi(int8_t rssi);
    void publishFirmwareVersion(uint8_t major, uint8_t minor);
    void publishIrrigationState(bool isAuto);
    void publishRainSensor(bool active);
    void publishStationsActive(uint32_t bitmask);
    void publishRainDelay(uint16_t days);
    void publishWaterBudget(uint8_t percent);
    void publishStationState(uint8_t station, bool on);
    void publishAvailability(bool online);
    void publishHeartbeat();
    void publishBridgeVersion();

    // OTA: returns pending URL and clears it (empty string = no OTA pending)
    String consumeOtaUrl();

    // GitHub release checking & HA update entity
    void checkGitHubRelease();
    void publishUpdateState();
    String consumeUpdateInstallUrl();

private:
    WiFiClient _wifiClient;
    PubSubClient _mqtt;
    RainBirdBLE* _ble = nullptr;

    String _deviceId;    // e.g. "rainbird_bat_bt_579a" (derived from RAINBIRD_DEVICE_NAME)
    String _deviceName;  // e.g. "Rain Bird BAT-BT-4 579A" (derived from RAINBIRD_DEVICE_NAME)
    uint16_t _stationDurations[NUM_STATIONS]; // per-station duration in minutes
    bool _discoveryPublished = false;
    PendingCommand _pendingCmd;
    uint16_t _lastStartedDuration = 0;  // Duration of last successfully started station
    String _pendingOtaUrl;
    String _latestVersion;
    String _releaseUrl;
    String _firmwareAssetUrl;
    bool _updateInstallPending = false;

    void connectMqtt();
    void publishDiscovery();
    void discoveryPause();
    void publishSwitchDiscovery(uint8_t station);
    void publishNumberDiscovery(uint8_t station);
    void publishSensorDiscovery(const char* name, const char* id,
                                const char* stateTopic, const char* unit,
                                const char* deviceClass, const char* icon);
    void publishBinarySensorDiscovery(const char* name, const char* id,
                                      const char* stateTopic, const char* deviceClass);
    void publishSelectDiscovery();
    void publishButtonDiscovery(const char* name, const char* id,
                                const char* commandTopic, const char* icon);
    void publishUpdateDiscovery();

    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static MqttHandler* _instance;

    void handleMessage(const String& topic, const String& payload);

    // Helper to build device JSON fragment for discovery
    String deviceJson();
};
