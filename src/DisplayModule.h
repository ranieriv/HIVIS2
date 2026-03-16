#pragma once
#include <U8g2lib.h>
#include <Wire.h>

struct DisplayData {
    float   temp, hum, iaq, co2, bvoc, db;
    uint8_t accuracy;
    int     batteryPrc;
    float   batteryV;
    int16_t rssi;
    bool    serverConnected;
    const char* ssid;
    const char* deviceName;
};

class DisplayModule {
public:
    DisplayModule();

    void begin();
    void showSplash();

    // Call every loop. Renders current page if screen is on; handles auto-off.
    void update(const DisplayData &data, float iaqWarn, float iaqAlert);

    // Wake screen and reset auto-off timer
    void wake();

    // Advance to next page (also wakes screen)
    void nextPage();

    bool isOn() const { return _screenOn; }
    void setTimeoutMs(int ms) { _timeoutMs = ms; }

private:
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2;

    bool          _screenOn;
    uint8_t       _page;
    unsigned long _lastActivity;
    int           _timeoutMs;

    void drawPage0(const DisplayData &d, float iaqWarn, float iaqAlert);
    void drawPage1(const DisplayData &d);
    void drawPage2(const DisplayData &d);

    void drawWifi(int x, int y, int16_t rssi);
    void drawBattery(int x, int y, int percent);
    void drawStatusBar(int x, int y, int16_t rssi, int batPrc);
};
