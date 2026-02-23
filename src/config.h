#pragma once

#include "secrets.h"

// Firmware version (bump this before each release)
#define FW_VERSION "0.2.3"

// MQTT Client ID
#define MQTT_CLIENT_ID "rainbird-bridge"

// Rain Bird BLE UUIDs
#define RAINBIRD_SERVICE_UUID "0000fe40-cc7a-482a-984a-7f2ed5b3e58f"
#define RAINBIRD_WRITE_UUID   "0000fe46-8e22-4541-9d4c-21edae82ed19"
#define RAINBIRD_NOTIFY_UUID  "0000fe47-8e22-4541-9d4c-21edae82ed19"

// Timing
#define STATUS_POLL_INTERVAL_MS  14400000  // 4 hours (BLE poll — saves Rain Bird battery)
#define HEARTBEAT_INTERVAL_MS    3600000   // 1 hour (MQTT heartbeat + healthcheck, no BLE)
#define BLE_CONNECT_TIMEOUT_MS   10000     // 10 seconds
#define BLE_RESPONSE_TIMEOUT_MS  5000      // 5 seconds
#define DEFAULT_STATION_DURATION 10        // minutes

// Healthchecks.io
#define HEALTHCHECK_HOST "hc-ping.com"
#define HEALTHCHECK_PATH "/4b28c66e-9f1a-45e4-8e55-94f7913f42ea"

// GitHub release checking (for HA update entity)
#define GITHUB_API_HOST "api.github.com"
#define GITHUB_RELEASES_PATH "/repos/maillme/rainbird-esp32/releases/latest"

// MQTT Topics
#define MQTT_BASE_TOPIC "rainbird"
#define MQTT_DISCOVERY_PREFIX "homeassistant"

// Station count (BAT-BT-4 has 4 stations)
#define NUM_STATIONS 4
