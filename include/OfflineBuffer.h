#pragma once
#include <Arduino.h>

// Forward declaration — full definition in DisplayModule.h
struct DisplayData;

class MqttModule; // forward declaration

class OfflineBuffer {
public:
    static const int   MAX_RECORDS = 500;
    static const char* PATH;  // "/offline_buffer.ndjson"

    // Append one sensor reading. If buffer is full, drops oldest record.
    static void store(const DisplayData& data);

    // Count records currently in buffer.
    static int count();

    // Assign approximate timestamps and publish all records via MQTT (QoS 1).
    // nowTs      — current Unix time from NTP
    // intervalMs — publish interval from cfg.mqttIntervalMs
    // deviceId   — used in the topic string
    // Clears the file on successful full flush.
    static void flush(MqttModule& mqtt, time_t nowTs, int intervalMs,
                      const String& deviceId, const String& deviceName);

private:
    // Remove the first (oldest) line from the buffer file.
    static void _dropOldest();
};
