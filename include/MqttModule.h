#pragma once
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// Forward declaration — full definition in DisplayModule.h
struct DisplayData;

// Callback type for incoming MQTT messages
typedef void (*MqttMessageCallback)(const String& topic, const String& payload);

class MqttModule {
public:
    // server      — MQTT broker hostname (e.g. "mqtt.hvht.net")
    // port        — 8883 for TLS
    // user/pass   — credentials (empty string = anonymous / bootstrap)
    // deviceId    — e.g. "hivis-aabbccddeeff"
    // deviceName  — human-readable label
    // keepaliveS  — MQTT keepalive in seconds
    MqttModule(const String& server, int port,
               const String& user,   const String& pass,
               const String& deviceId, const String& deviceName,
               int keepaliveS = 60);

    // Connect to broker. Returns true if connected.
    bool connect();

    // Must be called every loop iteration to maintain keepalive.
    void loop();

    // Publish a sensor reading to hivis/[deviceId]/data (QoS 0, not retained).
    bool publishSensor(const DisplayData& data, time_t ts, bool tsAccurate);

    // Publish raw payload to any topic (used by OfflineBuffer for backfill).
    // qos: 0 or 1.
    bool publishRaw(const String& topic, const String& payload, int qos = 1);

    // Publish device registration request to hivis/register (QoS 1).
    bool publishRegister(const String& mac, const String& deviceName);

    // Subscribe to a topic. Call after connect().
    bool subscribe(const String& topic, int qos = 1);

    // Register callback for incoming messages (provision response etc.).
    void setMessageCallback(MqttMessageCallback cb);

    bool isConnected() { return _mqtt.connected(); }

    // Data topic for this device: hivis/[deviceId]/data
    String dataTopic() { return "hivis/" + _deviceId + "/data"; }

    // Provision response topic: hivis/provision/[mac_no_colons_lowercase]
    static String provisionTopic(const String& mac);

private:
    WiFiClientSecure    _wifiClient;
    PubSubClient        _mqtt;

    String  _server;
    int     _port;
    String  _user;
    String  _pass;
    String  _deviceId;
    String  _deviceName;
    int     _keepaliveS;

    MqttMessageCallback _userCallback;

    void _connectInternal();
    static void _onMessage(char* topic, byte* payload, unsigned int len);
    static MqttModule* _instance; // for static callback
};
