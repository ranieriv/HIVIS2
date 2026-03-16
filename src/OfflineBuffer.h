#pragma once
#include <Arduino.h>

// Forward declaration — full definition in DisplayModule.h
struct DisplayData;
class MqttModule;

class OfflineBuffer {
public:
    static const int   MAX_RECORDS = 500;
    static const char* PATH;  // "/offline_buffer.ndjson"

    static void store(const DisplayData& data);
    static int  count();
    static void flush(MqttModule& mqtt, time_t nowTs, int intervalMs,
                      const String& deviceId, const String& deviceName);

private:
    static void _dropOldest();
};
