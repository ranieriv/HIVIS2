#pragma once
#include <Arduino.h>
#include <bsec2.h>
#include <Wire.h>
#include <LittleFS.h>

class BME688Module {
public:
    BME688Module(uint8_t addr, int saveIntervalH = 6);

    bool begin();   // returns true on success
    void update();  // no-op if begin() failed
    bool isOk() { return _ok; }

    float   getIAQ()      { return _iaq; }
    float   getCO2()      { return _co2; }
    float   getVOC()      { return _bvoc; }
    float   getTemp()     { return _temp; }
    float   getHum()      { return _hum; }
    uint8_t getAccuracy() { return _accuracy; }

private:
    uint8_t _addr;
    bool    _ok = false;
    Bsec2   _envSensor;

    static float    _iaq, _co2, _bvoc, _temp, _hum;
    static uint8_t  _accuracy;
    static uint32_t _saveIntervalMs;
    static BME688Module* _instance;

    void loadState();
    void saveState();
    static void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec);
};
