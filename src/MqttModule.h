#pragma once
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// Forward declaration — full definition in DisplayModule.h
struct DisplayData;

typedef void (*MqttMessageCallback)(const String& topic, const String& payload);

class MqttModule {
public:
    MqttModule(const String& server, int port,
               const String& user,   const String& pass,
               const String& deviceId, const String& deviceName,
               int keepaliveS = 60);

    bool connect();
    void loop();

    bool publishSensor(const DisplayData& data, time_t ts, bool tsAccurate);
    bool publishRaw(const String& topic, const String& payload, int qos = 1);
    bool publishRegister(const String& mac, const String& deviceName);
    bool subscribe(const String& topic, int qos = 1);
    void setMessageCallback(MqttMessageCallback cb);

    bool   isConnected()  { return _mqtt.connected(); }
    int    lastState()    { return _mqtt.state(); }
    String dataTopic()    { return "hivis/" + _deviceId + "/data"; }
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
    static MqttModule* _instance;

    unsigned long _lastConnectAttempt = 0;
    static const unsigned long RECONNECT_COOLDOWN_MS = 5000;
};
