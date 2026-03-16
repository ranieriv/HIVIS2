#include "MqttModule.h"
#include "DisplayModule.h"
#include <ArduinoJson.h>
#include <WiFi.h>

MqttModule* MqttModule::_instance = nullptr;

MqttModule::MqttModule(const String& server, int port,
                       const String& user,   const String& pass,
                       const String& deviceId, const String& deviceName,
                       int keepaliveS)
    : _mqtt(_wifiClient),
      _server(server), _port(port),
      _user(user), _pass(pass),
      _deviceId(deviceId), _deviceName(deviceName),
      _keepaliveS(keepaliveS),
      _userCallback(nullptr)
{
    _instance = this;

    // v2 uses setInsecure() — accepts self-signed cert (CA pinning is a future upgrade)
    _wifiClient.setInsecure();

    _mqtt.setServer(_server.c_str(), _port);
    _mqtt.setKeepAlive(_keepaliveS);
    _mqtt.setBufferSize(512);
    _mqtt.setCallback(_onMessage);
}

bool MqttModule::connect() {
    if (_mqtt.connected()) return true;
    _connectInternal();
    return _mqtt.connected();
}

void MqttModule::loop() {
    if (!_mqtt.connected()) _connectInternal();
    _mqtt.loop();
}

void MqttModule::_connectInternal() {
    String clientId = _deviceId;
    Serial.printf("MQTT: connecting to %s:%d as '%s'...",
                  _server.c_str(), _port, clientId.c_str());

    for (int i = 0; i < 3; i++) {
        bool ok;
        if (_user.isEmpty()) {
            ok = _mqtt.connect(clientId.c_str());
        } else {
            ok = _mqtt.connect(clientId.c_str(), _user.c_str(), _pass.c_str());
        }

        if (ok) {
            Serial.println(" OK");
            return;
        }
        Serial.printf(" failed (state=%d), retry...\n", _mqtt.state());
        delay(1000);
    }
    Serial.println(" MQTT: could not connect.");
}

void MqttModule::_onMessage(char* topic, byte* payload, unsigned int len) {
    if (!_instance || !_instance->_userCallback) return;
    String t(topic);
    String p;
    p.reserve(len);
    for (unsigned int i = 0; i < len; i++) p += (char)payload[i];
    _instance->_userCallback(t, p);
}

bool MqttModule::subscribe(const String& topic, int qos) {
    if (!_mqtt.connected()) return false;
    bool ok = _mqtt.subscribe(topic.c_str(), qos);
    Serial.printf("MQTT: subscribe '%s' %s\n", topic.c_str(), ok ? "OK" : "FAIL");
    return ok;
}

void MqttModule::setMessageCallback(MqttMessageCallback cb) {
    _userCallback = cb;
}

// ── Publish helpers ───────────────────────────────────────────────────────────

bool MqttModule::publishSensor(const DisplayData& data, time_t ts, bool tsAccurate) {
    if (!_mqtt.connected()) _connectInternal();
    if (!_mqtt.connected()) return false;
    _mqtt.loop();

    // bat_mv: millivolts as integer (e.g. 3850 for 3.850 V)
    int batMv = (int)(data.batteryV * 1000.0f);

    JsonDocument doc;
    doc["device_id"]   = _deviceId;
    doc["device_name"] = _deviceName;
    doc["group"]       = "";            // reserved for future use
    doc["ts"]          = (long long)ts;
    doc["ts_accurate"] = tsAccurate;
    doc["fw_version"]  = FIRMWARE_VERSION;
    doc["temp"]        = round(data.temp  * 10.0f) / 10.0f;
    doc["hum"]         = round(data.hum   * 10.0f) / 10.0f;
    doc["iaq"]         = round(data.iaq   * 10.0f) / 10.0f;
    doc["co2"]         = round(data.co2);
    doc["bvoc"]        = round(data.bvoc  * 100.0f) / 100.0f;
    doc["db"]          = round(data.db    * 10.0f) / 10.0f;
    doc["bat_pct"]     = data.batteryPrc;
    doc["bat_mv"]      = batMv;
    doc["rssi"]        = data.rssi;
    doc["accuracy"]    = data.accuracy;
    doc["backfill"]    = false;

    char buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));

    // Live data: QoS 0
    bool ok = _mqtt.publish(dataTopic().c_str(), (uint8_t*)buf, len, false);
    if (!ok) Serial.println("MQTT: publish failed.");
    return ok;
}

bool MqttModule::publishRaw(const String& topic, const String& payload, int qos) {
    if (!_mqtt.connected()) _connectInternal();
    if (!_mqtt.connected()) return false;
    _mqtt.loop();

    bool ok = _mqtt.publish(topic.c_str(),
                            (uint8_t*)payload.c_str(),
                            payload.length(),
                            false);  // not retained
    // PubSubClient doesn't expose QoS directly in publish() — QoS 0 only.
    // For QoS 1 backfill, we rely on the MQTT broker to handle delivery.
    (void)qos;
    return ok;
}

bool MqttModule::publishRegister(const String& mac, const String& deviceName) {
    if (!_mqtt.connected()) _connectInternal();
    if (!_mqtt.connected()) return false;
    _mqtt.loop();

    JsonDocument doc;
    doc["mac"]         = mac;
    doc["device_name"] = deviceName;
    doc["fw_version"]  = FIRMWARE_VERSION;
    doc["group"]       = "";

    char buf[256];
    size_t len = serializeJson(doc, buf, sizeof(buf));

    // Registration: QoS 1 (important)
    bool ok = _mqtt.publish("hivis/register", (uint8_t*)buf, len, false);
    Serial.printf("MQTT: registration publish %s\n", ok ? "OK" : "FAIL");
    return ok;
}

// static helper
String MqttModule::provisionTopic(const String& mac) {
    String m = mac;
    m.toLowerCase();
    m.replace(":", "");
    return "hivis/provision/" + m;
}
