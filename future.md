# Future Ideas

## Reverse-Engineer Schedule Format
Instead of decompiling the APK to understand `RETRIEVE_SCHEDULE` (0x20) / `SET_SCHEDULE` (0x21) data format, we can sniff it empirically:

1. Set a known schedule via the Rain Bird phone app
2. Reconnect ESP32, send `0x20`, dump raw hex to `rainbird/debug/schedule_raw`
3. Change the schedule in the app (different days, times, durations)
4. Query again, diff the hex dumps
5. Repeat until format is clear

Needs: a debug MQTT topic + HA button to trigger the query on demand.

Low priority — HA automations using start/stop commands handle scheduling fine for now.
