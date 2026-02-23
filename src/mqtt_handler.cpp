#include "mqtt_handler.h"
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

static const char* TAG = "MQTT";

MqttHandler* MqttHandler::_instance = nullptr;

void MqttHandler::init(RainBirdBLE* ble) {
    _instance = this;
    _ble = ble;
    _mqtt.setClient(_wifiClient);
    _mqtt.setServer(MQTT_HOST, MQTT_PORT);
    _mqtt.setCallback(mqttCallback);
    _mqtt.setBufferSize(2048);
    _mqtt.setKeepAlive(60);

    for (int i = 0; i < NUM_STATIONS; i++) {
        _stationDurations[i] = DEFAULT_STATION_DURATION;
    }
}

void MqttHandler::loop() {
    if (!_mqtt.connected()) {
        connectMqtt();
    }
    _mqtt.loop();
}

bool MqttHandler::isConnected() {
    return _mqtt.connected();
}

void MqttHandler::connectMqtt() {
    if (_mqtt.connected()) return;

    // Cooldown between reconnect attempts
    static unsigned long lastAttempt = 0;
    if (millis() - lastAttempt < 5000) return;
    lastAttempt = millis();

    Serial.printf("[MQTT] Connecting to %s:%d...\n", MQTT_HOST, MQTT_PORT);

    String willTopic = String(MQTT_BASE_TOPIC) + "/availability";

    bool connected;
    if (strlen(MQTT_USER) > 0) {
        connected = _mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                                  willTopic.c_str(), 1, true, "offline");
    } else {
        connected = _mqtt.connect(MQTT_CLIENT_ID,
                                  willTopic.c_str(), 1, true, "offline");
    }

    if (connected) {
        Serial.println("[MQTT] Connected");

        // Publish availability
        publishAvailability(true);

        // Publish HA discovery configs (only once)
        if (!_discoveryPublished) {
            publishDiscovery();
            _discoveryPublished = true;
        }

        // Publish bridge version and update state immediately on connect
        publishBridgeVersion();
        publishUpdateState();

        // Subscribe to command topics
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/station/+/set").c_str());
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/station/+/duration/set").c_str());
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/mode/set").c_str());
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/stop/set").c_str());
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/advance/set").c_str());
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/rain_delay/set").c_str());
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/water_budget/set").c_str());
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/program/+/set").c_str());
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/ota/set").c_str());
        _mqtt.subscribe((String(MQTT_BASE_TOPIC) + "/update/install").c_str());

        Serial.println("[MQTT] Subscribed to command topics");
    } else {
        Serial.printf("[MQTT] Failed, rc=%d. Retry in 5s\n", _mqtt.state());
    }
}

// --- HA MQTT Discovery ---

String MqttHandler::deviceJson() {
    return "\"dev\":{\"ids\":[\"rainbird_bat_bt_579a\"],"
           "\"name\":\"Rain Bird BAT-BT-4\","
           "\"mdl\":\"ESP-BAT-BT-4\","
           "\"mf\":\"Rain Bird\"}";
}

void MqttHandler::discoveryPause() {
    _mqtt.loop();
    yield();
    delay(100);
}

void MqttHandler::publishDiscovery() {
    Serial.println("[MQTT] Publishing HA discovery configs...");

    // Station switches (1-4)
    for (uint8_t i = 1; i <= NUM_STATIONS; i++) {
        publishSwitchDiscovery(i);
        discoveryPause();
        publishNumberDiscovery(i);
        discoveryPause();
    }

    // Controller mode select
    publishSelectDiscovery();
    discoveryPause();

    // Buttons
    publishButtonDiscovery("Stop All Irrigation", "stop", (String(MQTT_BASE_TOPIC) + "/stop/set").c_str(), "mdi:stop-circle");
    discoveryPause();
    publishButtonDiscovery("Advance Station", "advance", (String(MQTT_BASE_TOPIC) + "/advance/set").c_str(), "mdi:skip-next");
    discoveryPause();

    // Sensors
    publishSensorDiscovery("Battery Voltage", "battery_voltage",
                           (String(MQTT_BASE_TOPIC) + "/battery/state").c_str(),
                           "mV", "voltage", "mdi:battery");
    discoveryPause();
    publishSensorDiscovery("BLE RSSI", "ble_rssi",
                           (String(MQTT_BASE_TOPIC) + "/rssi/state").c_str(),
                           "dBm", "signal_strength", "mdi:bluetooth");
    discoveryPause();
    publishSensorDiscovery("Firmware", "firmware",
                           (String(MQTT_BASE_TOPIC) + "/firmware/state").c_str(),
                           nullptr, nullptr, "mdi:chip");
    discoveryPause();
    publishSensorDiscovery("Active Station", "active_station",
                           (String(MQTT_BASE_TOPIC) + "/active_station/state").c_str(),
                           nullptr, nullptr, "mdi:sprinkler-variant");
    discoveryPause();

    // Battery percentage
    publishSensorDiscovery("Battery Level", "battery_level",
                           (String(MQTT_BASE_TOPIC) + "/battery_percent/state").c_str(),
                           "%", "battery", "mdi:battery");
    discoveryPause();

    // Binary sensors
    publishBinarySensorDiscovery("Rain Sensor", "rain_sensor",
                                 (String(MQTT_BASE_TOPIC) + "/rain_sensor/state").c_str(),
                                 "moisture");
    discoveryPause();

    // Rain delay number
    {
        JsonDocument doc;
        doc["name"] = "Rain Delay";
        doc["uniq_id"] = "rainbird_rain_delay";
        doc["cmd_t"] = String(MQTT_BASE_TOPIC) + "/rain_delay/set";
        doc["stat_t"] = String(MQTT_BASE_TOPIC) + "/rain_delay/state";
        doc["min"] = 0;
        doc["max"] = 14;
        doc["step"] = 1;
        doc["unit_of_meas"] = "days";
        doc["ic"] = "mdi:weather-rainy";
        doc["avty_t"] = String(MQTT_BASE_TOPIC) + "/availability";
        JsonObject dev = doc["dev"].to<JsonObject>();
        dev["ids"][0] = "rainbird_bat_bt_579a";

        char buf[512];
        serializeJson(doc, buf);
        _mqtt.publish((String(MQTT_DISCOVERY_PREFIX) + "/number/rainbird/rain_delay/config").c_str(), buf, true);
    }
    discoveryPause();

    // Water budget number
    {
        JsonDocument doc;
        doc["name"] = "Water Budget";
        doc["uniq_id"] = "rainbird_water_budget";
        doc["cmd_t"] = String(MQTT_BASE_TOPIC) + "/water_budget/set";
        doc["stat_t"] = String(MQTT_BASE_TOPIC) + "/water_budget/state";
        doc["min"] = 0;
        doc["max"] = 200;
        doc["step"] = 10;
        doc["unit_of_meas"] = "%";
        doc["ic"] = "mdi:water-percent";
        doc["avty_t"] = String(MQTT_BASE_TOPIC) + "/availability";
        JsonObject dev = doc["dev"].to<JsonObject>();
        dev["ids"][0] = "rainbird_bat_bt_579a";

        char buf[512];
        serializeJson(doc, buf);
        _mqtt.publish((String(MQTT_DISCOVERY_PREFIX) + "/number/rainbird/water_budget/config").c_str(), buf, true);
    }

    // Bridge heartbeat sensor (for HA "last seen" tracking)
    publishSensorDiscovery("Bridge Heartbeat", "bridge_heartbeat",
                           (String(MQTT_BASE_TOPIC) + "/heartbeat/state").c_str(),
                           nullptr, nullptr, "mdi:heart-pulse");
    discoveryPause();

    // Bridge uptime sensor
    publishSensorDiscovery("Bridge Uptime", "bridge_uptime",
                           (String(MQTT_BASE_TOPIC) + "/uptime/state").c_str(),
                           "s", "duration", "mdi:timer-outline");
    discoveryPause();

    // Bridge WiFi RSSI sensor
    publishSensorDiscovery("Bridge WiFi RSSI", "bridge_wifi_rssi",
                           (String(MQTT_BASE_TOPIC) + "/wifi_rssi/state").c_str(),
                           "dBm", "signal_strength", "mdi:wifi");
    discoveryPause();

    // Bridge free heap sensor
    publishSensorDiscovery("Bridge Free Heap", "bridge_free_heap",
                           (String(MQTT_BASE_TOPIC) + "/free_heap/state").c_str(),
                           "B", nullptr, "mdi:memory");
    discoveryPause();

    // Bridge firmware version sensor
    publishSensorDiscovery("Bridge Version", "bridge_version",
                           (String(MQTT_BASE_TOPIC) + "/bridge_version/state").c_str(),
                           nullptr, nullptr, "mdi:tag");
    discoveryPause();

    // OTA update button (payload = firmware URL)
    publishButtonDiscovery("OTA Update", "ota_update",
                           (String(MQTT_BASE_TOPIC) + "/ota/set").c_str(), "mdi:download");
    discoveryPause();

    // Run Program buttons
    publishButtonDiscovery("Run Program A", "run_program_a",
                           (String(MQTT_BASE_TOPIC) + "/program/1/set").c_str(), "mdi:play-circle");
    discoveryPause();
    publishButtonDiscovery("Run Program B", "run_program_b",
                           (String(MQTT_BASE_TOPIC) + "/program/2/set").c_str(), "mdi:play-circle");
    discoveryPause();
    publishButtonDiscovery("Run Program C", "run_program_c",
                           (String(MQTT_BASE_TOPIC) + "/program/3/set").c_str(), "mdi:play-circle");
    discoveryPause();

    // Firmware update entity (checks GitHub releases)
    publishUpdateDiscovery();
    discoveryPause();

    Serial.println("[MQTT] Discovery published");
}

void MqttHandler::publishSwitchDiscovery(uint8_t station) {
    JsonDocument doc;
    doc["name"] = String("Station ") + station;
    doc["uniq_id"] = String("rainbird_station_") + station;
    doc["cmd_t"] = String(MQTT_BASE_TOPIC) + "/station/" + station + "/set";
    doc["stat_t"] = String(MQTT_BASE_TOPIC) + "/station/" + station + "/state";
    doc["ic"] = "mdi:sprinkler";
    doc["opt"] = true;  // Optimistic: show ON immediately while BLE command executes
    doc["avty_t"] = String(MQTT_BASE_TOPIC) + "/availability";
    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"][0] = "rainbird_bat_bt_579a";
    if (station == 1) {
        dev["name"] = "Rain Bird BAT-BT-4";
        dev["mdl"] = "ESP-BAT-BT-4";
        dev["mf"] = "Rain Bird";
    }

    char buf[512];
    serializeJson(doc, buf);
    String topic = String(MQTT_DISCOVERY_PREFIX) + "/switch/rainbird/station_" + station + "/config";
    _mqtt.publish(topic.c_str(), buf, true);
}

void MqttHandler::publishNumberDiscovery(uint8_t station) {
    JsonDocument doc;
    doc["name"] = String("Station ") + station + " Duration";
    doc["uniq_id"] = String("rainbird_station_") + station + "_duration";
    doc["cmd_t"] = String(MQTT_BASE_TOPIC) + "/station/" + station + "/duration/set";
    doc["stat_t"] = String(MQTT_BASE_TOPIC) + "/station/" + station + "/duration/state";
    doc["min"] = 1;
    doc["max"] = 120;
    doc["step"] = 1;
    doc["unit_of_meas"] = "min";
    doc["ic"] = "mdi:timer";
    doc["avty_t"] = String(MQTT_BASE_TOPIC) + "/availability";
    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"][0] = "rainbird_bat_bt_579a";

    char buf[512];
    serializeJson(doc, buf);
    String topic = String(MQTT_DISCOVERY_PREFIX) + "/number/rainbird/station_" + station + "_duration/config";
    _mqtt.publish(topic.c_str(), buf, true);

    // Publish initial duration state
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/station/" + station + "/duration/state").c_str(),
                  String(DEFAULT_STATION_DURATION).c_str(), true);
}

void MqttHandler::publishSelectDiscovery() {
    JsonDocument doc;
    doc["name"] = "Controller Mode";
    doc["uniq_id"] = "rainbird_mode";
    doc["cmd_t"] = String(MQTT_BASE_TOPIC) + "/mode/set";
    doc["stat_t"] = String(MQTT_BASE_TOPIC) + "/mode/state";
    doc["ic"] = "mdi:power";
    doc["avty_t"] = String(MQTT_BASE_TOPIC) + "/availability";
    doc["ops"][0] = "Off";
    doc["ops"][1] = "Auto";
    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"][0] = "rainbird_bat_bt_579a";

    char buf[512];
    serializeJson(doc, buf);
    _mqtt.publish((String(MQTT_DISCOVERY_PREFIX) + "/select/rainbird/mode/config").c_str(), buf, true);
}

void MqttHandler::publishButtonDiscovery(const char* name, const char* id,
                                          const char* commandTopic, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["uniq_id"] = String("rainbird_") + id;
    doc["cmd_t"] = commandTopic;
    doc["ic"] = icon;
    doc["avty_t"] = String(MQTT_BASE_TOPIC) + "/availability";
    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"][0] = "rainbird_bat_bt_579a";

    char buf[512];
    serializeJson(doc, buf);
    String topic = String(MQTT_DISCOVERY_PREFIX) + "/button/rainbird/" + id + "/config";
    _mqtt.publish(topic.c_str(), buf, true);
}

void MqttHandler::publishUpdateDiscovery() {
    JsonDocument doc;
    doc["name"] = "Firmware Update";
    doc["uniq_id"] = "rainbird_firmware_update";
    doc["dev_cla"] = "firmware";
    doc["stat_t"] = String(MQTT_BASE_TOPIC) + "/update/state";
    doc["cmd_t"] = String(MQTT_BASE_TOPIC) + "/update/install";
    doc["pl_inst"] = "INSTALL";
    doc["avty_t"] = String(MQTT_BASE_TOPIC) + "/availability";
    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"][0] = "rainbird_bat_bt_579a";

    char buf[512];
    serializeJson(doc, buf);
    _mqtt.publish((String(MQTT_DISCOVERY_PREFIX) + "/update/rainbird/firmware_update/config").c_str(),
                  buf, true);
}

void MqttHandler::publishSensorDiscovery(const char* name, const char* id,
                                          const char* stateTopic, const char* unit,
                                          const char* deviceClass, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["uniq_id"] = String("rainbird_") + id;
    doc["stat_t"] = stateTopic;
    if (icon) doc["ic"] = icon;
    if (unit) doc["unit_of_meas"] = unit;
    if (deviceClass) doc["dev_cla"] = deviceClass;
    doc["avty_t"] = String(MQTT_BASE_TOPIC) + "/availability";
    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"][0] = "rainbird_bat_bt_579a";

    char buf[512];
    serializeJson(doc, buf);
    String topic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/rainbird/" + id + "/config";
    _mqtt.publish(topic.c_str(), buf, true);
}

void MqttHandler::publishBinarySensorDiscovery(const char* name, const char* id,
                                                const char* stateTopic, const char* deviceClass) {
    JsonDocument doc;
    doc["name"] = name;
    doc["uniq_id"] = String("rainbird_") + id;
    doc["stat_t"] = stateTopic;
    if (deviceClass) doc["dev_cla"] = deviceClass;
    doc["avty_t"] = String(MQTT_BASE_TOPIC) + "/availability";
    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"][0] = "rainbird_bat_bt_579a";

    char buf[512];
    serializeJson(doc, buf);
    String topic = String(MQTT_DISCOVERY_PREFIX) + "/binary_sensor/rainbird/" + id + "/config";
    _mqtt.publish(topic.c_str(), buf, true);
}

// --- State Publishing ---

void MqttHandler::publishBatteryVoltage(uint16_t millivolts) {
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/battery/state").c_str(),
                  String(millivolts).c_str(), true);
}

void MqttHandler::publishBatteryPercent(uint8_t percent) {
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/battery_percent/state").c_str(),
                  String(percent).c_str(), true);
}

void MqttHandler::publishBleRssi(int8_t rssi) {
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/rssi/state").c_str(),
                  String(rssi).c_str(), true);
}

void MqttHandler::publishFirmwareVersion(uint8_t major, uint8_t minor) {
    String ver = String(major) + "." + String(minor);
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/firmware/state").c_str(),
                  ver.c_str(), true);
}

void MqttHandler::publishIrrigationState(bool isAuto) {
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/mode/state").c_str(),
                  isAuto ? "Auto" : "Off", true);
}

void MqttHandler::publishRainSensor(bool active) {
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/rain_sensor/state").c_str(),
                  active ? "ON" : "OFF", true);
}

void MqttHandler::publishStationsActive(uint32_t bitmask) {
    // Update individual station states
    for (uint8_t i = 0; i < NUM_STATIONS; i++) {
        bool active = (bitmask >> i) & 1;
        publishStationState(i + 1, active);
    }

    // Publish active station name
    String activeStr = "Idle";
    for (uint8_t i = 0; i < NUM_STATIONS; i++) {
        if ((bitmask >> i) & 1) {
            activeStr = String("Station ") + (i + 1);
            break;
        }
    }
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/active_station/state").c_str(),
                  activeStr.c_str(), true);
}

void MqttHandler::publishRainDelay(uint16_t days) {
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/rain_delay/state").c_str(),
                  String(days).c_str(), true);
}

void MqttHandler::publishWaterBudget(uint8_t percent) {
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/water_budget/state").c_str(),
                  String(percent).c_str(), true);
}

void MqttHandler::publishStationState(uint8_t station, bool on) {
    String topic = String(MQTT_BASE_TOPIC) + "/station/" + station + "/state";
    _mqtt.publish(topic.c_str(), on ? "ON" : "OFF", true);
}

void MqttHandler::publishAvailability(bool online) {
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/availability").c_str(),
                  online ? "online" : "offline", true);
}

void MqttHandler::publishBridgeVersion() {
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/bridge_version/state").c_str(),
                  FW_VERSION, true);
}

String MqttHandler::consumeOtaUrl() {
    String url = _pendingOtaUrl;
    _pendingOtaUrl = "";
    return url;
}

void MqttHandler::publishUpdateState() {
    JsonDocument doc;
    doc["installed_version"] = FW_VERSION;
    doc["latest_version"] = _latestVersion.length() > 0 ? _latestVersion : FW_VERSION;
    doc["title"] = "Rain Bird Bridge";
    if (_releaseUrl.length() > 0) {
        doc["release_url"] = _releaseUrl;
    }

    char buf[512];
    serializeJson(doc, buf);
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/update/state").c_str(), buf, true);
}

void MqttHandler::checkGitHubRelease() {
    Serial.println("[Update] Checking GitHub for latest release...");

    WiFiClientSecure client;
    client.setInsecure();

    if (!client.connect(GITHUB_API_HOST, 443, 5000)) {
        Serial.println("[Update] GitHub API connection failed");
        return;
    }

    client.printf(
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: rainbird-esp32/%s\r\n"
        "Accept: application/vnd.github+json\r\n"
        "Connection: close\r\n\r\n",
        GITHUB_RELEASES_PATH, GITHUB_API_HOST, FW_VERSION);

    // Wait for response
    unsigned long timeout = millis() + 10000;
    while (!client.available() && millis() < timeout) {
        delay(10);
    }
    if (!client.available()) {
        Serial.println("[Update] GitHub API timeout");
        client.stop();
        return;
    }

    // Check HTTP status
    String statusLine = client.readStringUntil('\n');
    if (statusLine.indexOf("200") < 0) {
        Serial.printf("[Update] GitHub API: %s\n", statusLine.c_str());
        client.stop();
        return;
    }

    // Skip headers
    while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() == 0) break;
    }

    // Parse JSON with filter (only extract what we need from the large response)
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["html_url"] = true;
    filter["assets"][0]["browser_download_url"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, client,
        DeserializationOption::Filter(filter));
    client.stop();

    if (err) {
        Serial.printf("[Update] JSON parse failed: %s\n", err.c_str());
        return;
    }

    String tagName = doc["tag_name"].as<String>();
    if (tagName.startsWith("v") || tagName.startsWith("V")) {
        tagName = tagName.substring(1);
    }

    if (tagName.length() == 0) {
        Serial.println("[Update] No tag_name in response");
        return;
    }

    _latestVersion = tagName;
    _releaseUrl = doc["html_url"].as<String>();
    String assetUrl = doc["assets"][0]["browser_download_url"].as<String>();
    if (assetUrl.length() > 0) {
        _firmwareAssetUrl = assetUrl;
    }

    Serial.printf("[Update] Latest: %s, Installed: %s\n",
                  _latestVersion.c_str(), FW_VERSION);

    publishUpdateState();
}

String MqttHandler::consumeUpdateInstallUrl() {
    if (_updateInstallPending && _firmwareAssetUrl.length() > 0) {
        _updateInstallPending = false;
        return _firmwareAssetUrl;
    }
    _updateInstallPending = false;
    return "";
}

void MqttHandler::publishHeartbeat() {
    unsigned long uptimeSec = millis() / 1000;
    int32_t wifiRssi = WiFi.RSSI();
    uint32_t freeHeap = ESP.getFreeHeap();

    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/heartbeat/state").c_str(), "online", true);
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/uptime/state").c_str(),
                  String(uptimeSec).c_str(), true);
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/wifi_rssi/state").c_str(),
                  String(wifiRssi).c_str(), true);
    _mqtt.publish((String(MQTT_BASE_TOPIC) + "/free_heap/state").c_str(),
                  String(freeHeap).c_str(), true);

    Serial.printf("[Heartbeat] uptime=%lus, WiFi RSSI=%d dBm, heap=%u B\n",
                  uptimeSec, wifiRssi, freeHeap);
}

// --- MQTT Message Handling ---

void MqttHandler::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (!_instance) return;
    String topicStr(topic);
    String payloadStr;
    payloadStr.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }
    _instance->handleMessage(topicStr, payloadStr);
}

void MqttHandler::handleMessage(const String& topic, const String& payload) {
    Serial.printf("[MQTT] Received: %s = %s\n", topic.c_str(), payload.c_str());

    // Station ON/OFF: rainbird/station/{n}/set — queue for main loop
    for (uint8_t i = 1; i <= NUM_STATIONS; i++) {
        String stationSetTopic = String(MQTT_BASE_TOPIC) + "/station/" + i + "/set";
        if (topic == stationSetTopic) {
            if (payload == "ON") {
                _pendingCmd.type = CMD_RUN_STATION;
                _pendingCmd.station = i;
                _pendingCmd.duration = _stationDurations[i - 1];
                Serial.printf("[CMD] Queued: run station %d for %d min\n", i, _stationDurations[i - 1]);
            } else if (payload == "OFF") {
                _pendingCmd.type = CMD_STOP;
                _pendingCmd.station = i;
                Serial.printf("[CMD] Queued: stop (station %d OFF)\n", i);
            }
            return;
        }

        // Station duration: rainbird/station/{n}/duration/set — no BLE needed, handle immediately
        String durationSetTopic = String(MQTT_BASE_TOPIC) + "/station/" + i + "/duration/set";
        if (topic == durationSetTopic) {
            _stationDurations[i - 1] = payload.toInt();
            Serial.printf("[CMD] Station %d duration set to %d min\n", i, _stationDurations[i - 1]);
            String stateTopic = String(MQTT_BASE_TOPIC) + "/station/" + i + "/duration/state";
            _mqtt.publish(stateTopic.c_str(), String(_stationDurations[i - 1]).c_str(), true);
            return;
        }
    }

    // Controller mode: rainbird/mode/set
    if (topic == String(MQTT_BASE_TOPIC) + "/mode/set") {
        _pendingCmd.type = CMD_SET_MODE;
        _pendingCmd.value = (payload == "Auto") ? 1 : 0;
        Serial.printf("[CMD] Queued: set mode %s\n", payload.c_str());
        return;
    }

    // Stop all: rainbird/stop/set
    if (topic == String(MQTT_BASE_TOPIC) + "/stop/set") {
        _pendingCmd.type = CMD_STOP;
        _pendingCmd.station = 0;
        Serial.println("[CMD] Queued: stop all");
        return;
    }

    // Advance station: rainbird/advance/set
    if (topic == String(MQTT_BASE_TOPIC) + "/advance/set") {
        _pendingCmd.type = CMD_ADVANCE;
        Serial.println("[CMD] Queued: advance station");
        return;
    }

    // Rain delay: rainbird/rain_delay/set
    if (topic == String(MQTT_BASE_TOPIC) + "/rain_delay/set") {
        _pendingCmd.type = CMD_SET_RAIN_DELAY;
        _pendingCmd.duration = payload.toInt();
        Serial.printf("[CMD] Queued: rain delay %d days\n", payload.toInt());
        return;
    }

    // Water budget: rainbird/water_budget/set
    if (topic == String(MQTT_BASE_TOPIC) + "/water_budget/set") {
        _pendingCmd.type = CMD_SET_WATER_BUDGET;
        _pendingCmd.value = payload.toInt();
        Serial.printf("[CMD] Queued: water budget %d%%\n", payload.toInt());
        return;
    }

    // Run program: rainbird/program/{n}/set
    for (uint8_t i = 1; i <= 3; i++) {
        if (topic == String(MQTT_BASE_TOPIC) + "/program/" + i + "/set") {
            _pendingCmd.type = CMD_RUN_PROGRAM;
            _pendingCmd.value = i - 1;  // SIP uses 0-based program IDs (A=0, B=1, C=2)
            Serial.printf("[CMD] Queued: run program %c\n", 'A' + i - 1);
            return;
        }
    }

    // OTA update: rainbird/ota/set — payload is the firmware URL
    if (topic == String(MQTT_BASE_TOPIC) + "/ota/set") {
        if (payload.length() > 0) {
            _pendingOtaUrl = payload;
            Serial.printf("[CMD] OTA update queued: %s\n", payload.c_str());
        }
        return;
    }

    // HA update entity install: rainbird/update/install
    if (topic == String(MQTT_BASE_TOPIC) + "/update/install") {
        if (payload == "INSTALL" && _firmwareAssetUrl.length() > 0) {
            _updateInstallPending = true;
            Serial.printf("[CMD] Update install requested: %s\n", _firmwareAssetUrl.c_str());
        } else if (_firmwareAssetUrl.length() == 0) {
            Serial.println("[CMD] Update install requested but no firmware URL available");
        }
        return;
    }
}

uint16_t MqttHandler::consumeStartedDuration() {
    uint16_t d = _lastStartedDuration;
    _lastStartedDuration = 0;
    return d;
}

void MqttHandler::processPendingCommand() {
    if (_pendingCmd.type == CMD_NONE) return;

    PendingCommand cmd = _pendingCmd;
    _pendingCmd.type = CMD_NONE;  // Clear before executing (BLE takes time)

    Serial.printf("[CMD] Executing queued command type=%d\n", cmd.type);

    switch (cmd.type) {
        case CMD_RUN_STATION:
            // SIP uses 1-based station IDs (same as HA)
            if (_ble->runStation(cmd.station, cmd.duration)) {
                publishStationState(cmd.station, true);
                _lastStartedDuration = cmd.duration;
                Serial.printf("[CMD] Station %d started for %d min\n", cmd.station, cmd.duration);
            } else {
                publishStationState(cmd.station, false);  // Revert optimistic ON
                Serial.printf("[CMD] Station %d start FAILED\n", cmd.station);
            }
            break;

        case CMD_STOP:
            if (_ble->stopIrrigation()) {
                for (uint8_t i = 1; i <= NUM_STATIONS; i++) {
                    publishStationState(i, false);
                }
                Serial.println("[CMD] Irrigation stopped");
            }
            break;

        case CMD_ADVANCE:
            _ble->advanceStation();
            Serial.println("[CMD] Station advanced");
            break;

        case CMD_SET_MODE:
            if (_ble->setControllerState(cmd.value)) {
                publishIrrigationState(cmd.value == 1);
                Serial.printf("[CMD] Mode set to %s\n", cmd.value ? "Auto" : "Off");
            }
            break;

        case CMD_SET_RAIN_DELAY:
            if (_ble->setRainDelay(cmd.duration)) {
                publishRainDelay(cmd.duration);
            }
            break;

        case CMD_SET_WATER_BUDGET:
            if (_ble->setWaterBudget(0, cmd.value)) {
                publishWaterBudget(cmd.value);
            }
            break;

        case CMD_RUN_PROGRAM:
            if (_ble->runProgram(cmd.value)) {
                Serial.printf("[CMD] Program %c started\n", 'A' + cmd.value);
            } else {
                Serial.printf("[CMD] Program %c start FAILED\n", 'A' + cmd.value);
            }
            break;

        default:
            break;
    }
}
