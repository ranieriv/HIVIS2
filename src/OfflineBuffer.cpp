#include "OfflineBuffer.h"
#include "DisplayModule.h"
#include "MqttModule.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

const char* OfflineBuffer::PATH = "/offline_buffer.ndjson";

// ── store ─────────────────────────────────────────────────────────────────────

void OfflineBuffer::store(const DisplayData& data) {
    int n = count();
    if (n >= MAX_RECORDS) {
        Serial.println("OfflineBuffer: full — dropping oldest record.");
        _dropOldest();
    }

    // Use "w" to create a new file; "a" to append to an existing one.
    // LittleFS on ESP32 Arduino does not create files with "a" mode.
    const char* mode = LittleFS.exists(PATH) ? "a" : "w";
    File f = LittleFS.open(PATH, mode);
    if (!f) {
        Serial.println("OfflineBuffer: could not open for append.");
        return;
    }

    JsonDocument doc;
    doc["ts"]          = 0;
    doc["ts_accurate"] = false;
    doc["temp"]        = round(data.temp  * 10.0f) / 10.0f;
    doc["hum"]         = round(data.hum   * 10.0f) / 10.0f;
    doc["iaq"]         = round(data.iaq   * 10.0f) / 10.0f;
    doc["co2"]         = round(data.co2);
    doc["bvoc"]        = round(data.bvoc  * 100.0f) / 100.0f;
    doc["db"]          = round(data.db    * 10.0f) / 10.0f;
    doc["bat_pct"]     = data.batteryPrc;
    doc["bat_mv"]      = (int)(data.batteryV * 1000.0f);
    doc["accuracy"]    = data.accuracy;
    doc["backfill"]    = true;

    serializeJson(doc, f);
    f.println();   // newline-delimited JSON (NDJSON)
    f.close();
}

// ── count ─────────────────────────────────────────────────────────────────────

int OfflineBuffer::count() {
    File f = LittleFS.open(PATH, "r");
    if (!f) return 0;

    int n = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 2) n++;
    }
    f.close();
    return n;
}

// ── flush ─────────────────────────────────────────────────────────────────────

void OfflineBuffer::flush(MqttModule& mqtt, time_t nowTs, int intervalMs,
                          const String& deviceId, const String& deviceName) {
    if (!LittleFS.exists(PATH)) return;

    File f = LittleFS.open(PATH, "r");
    if (!f) return;

    // Read all lines
    std::vector<String> lines;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 2) lines.push_back(line);
    }
    f.close();

    if (lines.empty()) {
        LittleFS.remove(PATH);
        return;
    }

    int total    = (int)lines.size();
    int intervalS = intervalMs / 1000;
    String topic  = "hivis/" + deviceId + "/data";

    Serial.printf("OfflineBuffer: flushing %d records...\n", total);

    bool allOk = true;
    for (int i = 0; i < total; i++) {
        JsonDocument doc;
        if (deserializeJson(doc, lines[i]) != DeserializationError::Ok) continue;

        // Assign approximate timestamp (oldest record first)
        long long approxTs = (long long)nowTs - (long long)(total - 1 - i) * intervalS;

        doc["ts"]          = approxTs;
        doc["ts_accurate"] = false;
        doc["device_id"]   = deviceId;
        doc["device_name"] = deviceName;
        doc["fw_version"]  = FIRMWARE_VERSION;
        doc["group"]       = "";

        String payload;
        serializeJson(doc, payload);

        // Backfill: QoS 1 for reliability
        if (!mqtt.publishRaw(topic, payload, 1)) {
            allOk = false;
            Serial.printf("OfflineBuffer: publish failed at record %d\n", i);
            break;
        }
        delay(50); // don't flood the broker
    }

    if (allOk) {
        LittleFS.remove(PATH);
        Serial.println("OfflineBuffer: flush complete, buffer cleared.");
    } else {
        Serial.println("OfflineBuffer: partial flush — buffer retained for next reconnect.");
    }
}

// ── _dropOldest ───────────────────────────────────────────────────────────────

void OfflineBuffer::_dropOldest() {
    if (!LittleFS.exists(PATH)) return;

    File src = LittleFS.open(PATH, "r");
    if (!src) return;

    // Skip first non-empty line, write the rest to a temp file
    const char* tmpPath = "/offline_buffer.tmp";
    File tmp = LittleFS.open(tmpPath, "w");
    if (!tmp) { src.close(); return; }

    bool firstDropped = false;
    while (src.available()) {
        String line = src.readStringUntil('\n');
        line.trim();
        if (!firstDropped && line.length() > 2) {
            firstDropped = true;
            continue;  // skip oldest record
        }
        tmp.println(line);
    }
    src.close();
    tmp.close();

    LittleFS.remove(PATH);
    LittleFS.rename(tmpPath, PATH);
}
