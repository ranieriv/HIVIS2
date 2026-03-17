#include "DisplayModule.h"

DisplayModule::DisplayModule()
    : _u8g2(U8G2_R0, U8X8_PIN_NONE),
      _screenOn(false), _page(0), _lastActivity(0), _timeoutMs(10000) {}

void DisplayModule::begin() {
    _u8g2.begin();
    _u8g2.setBusClock(400000);
}

void DisplayModule::showSplash() {
    _screenOn     = true;
    _lastActivity = millis();
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_helvB12_tr);
    _u8g2.drawStr(2, 28, "HIVIS Monitor");
    _u8g2.setFont(u8g2_font_6x12_tr);
    _u8g2.drawStr(2, 44, "Version 2.0.0");
    _u8g2.drawStr(2, 57, "Initializing...");
    _u8g2.sendBuffer();
}

void DisplayModule::showPortal(const char* ssid) {
    _screenOn = true;
    _lastActivity = millis();
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_helvB08_tr);
    _u8g2.drawStr(2, 10, "-- Setup Portal --");
    _u8g2.drawHLine(0, 13, 128);
    _u8g2.setFont(u8g2_font_6x12_tr);
    _u8g2.drawStr(2, 28, "Connect to WiFi:");
    _u8g2.drawStr(2, 42, ssid);
    _u8g2.drawHLine(0, 50, 128);
    _u8g2.setFont(u8g2_font_5x8_tr);
    _u8g2.drawStr(2, 58, "Open 192.168.4.1");
    _u8g2.sendBuffer();
}

void DisplayModule::wake() {
    _screenOn     = true;
    _lastActivity = millis();
}

void DisplayModule::nextPage() {
    _page = (_page + 1) % 3;
    wake();
}

void DisplayModule::update(const DisplayData &data, float iaqWarn, float iaqAlert) {
    if (!_screenOn) return;

    if (_timeoutMs > 0 && millis() - _lastActivity >= (unsigned long)_timeoutMs) {
        _screenOn = false;
        _u8g2.clearBuffer();
        _u8g2.sendBuffer();
        return;
    }

    _u8g2.clearBuffer();
    switch (_page) {
        case 0: drawPage0(data, iaqWarn, iaqAlert); break;
        case 1: drawPage1(data);                    break;
        case 2: drawPage2(data);                    break;
    }
    _u8g2.sendBuffer();
}

// ── Page 0 — Air Quality ──────────────────────────────────────────────────────
void DisplayModule::drawPage0(const DisplayData &d, float iaqWarn, float iaqAlert) {
    _u8g2.setFont(u8g2_font_6x12_tr);
    _u8g2.setCursor(0, 10);
    _u8g2.print(d.temp, 1); _u8g2.print("C  ");
    _u8g2.print(d.hum,  0); _u8g2.print("%");
    drawStatusBar(90, 2, d.rssi, d.batteryPrc);
    _u8g2.drawHLine(0, 13, 128);

    if (d.accuracy == 0) {
        _u8g2.setFont(u8g2_font_6x12_tr);
        _u8g2.drawStr(8, 40, "Sensor warming up...");
    } else {
        _u8g2.setFont(u8g2_font_logisoso20_tn);
        _u8g2.setCursor(0, 44);
        _u8g2.print((int)d.iaq);
        _u8g2.setFont(u8g2_font_6x12_tr);
        _u8g2.drawStr(55, 32, "IAQ");
        _u8g2.setCursor(55, 44);
        _u8g2.print("CO2: "); _u8g2.print((int)d.co2);
    }

    _u8g2.setFont(u8g2_font_6x12_tr);
    _u8g2.setCursor(0, 56);
    _u8g2.print("Noise: "); _u8g2.print(d.db, 1); _u8g2.print(" dB");

    _u8g2.drawHLine(0, 58, 128);
    _u8g2.setFont(u8g2_font_5x8_tr);
    if (d.accuracy == 0) {
        _u8g2.drawStr(0, 64, "Calibrating BSEC...");
    } else if (d.iaq >= iaqAlert) {
        _u8g2.drawStr(0, 64, "!! POOR AIR QUALITY !!");
    } else if (d.iaq >= iaqWarn) {
        _u8g2.drawStr(0, 64, "ALERT: VENTILATE REC");
    } else {
        _u8g2.setCursor(0, 64);
        _u8g2.print("Status: OK | Acc: "); _u8g2.print(d.accuracy);
    }
}

// ── Page 1 — Environment ──────────────────────────────────────────────────────
void DisplayModule::drawPage1(const DisplayData &d) {
    _u8g2.setFont(u8g2_font_6x12_tr);
    _u8g2.setCursor(0, 10);
    if (d.deviceName) _u8g2.print(d.deviceName);
    drawStatusBar(90, 2, d.rssi, d.batteryPrc);
    _u8g2.drawHLine(0, 13, 128);

    _u8g2.setCursor(4, 26);
    _u8g2.print("Temp:  "); _u8g2.print(d.temp, 1); _u8g2.print(" C");
    _u8g2.setCursor(4, 38);
    _u8g2.print("Hum:   "); _u8g2.print(d.hum, 1); _u8g2.print(" %");
    _u8g2.setCursor(4, 50);
    _u8g2.print("BVOC:  "); _u8g2.print(d.bvoc, 2); _u8g2.print(" ppm");

    _u8g2.drawHLine(0, 58, 128);
    _u8g2.setFont(u8g2_font_5x8_tr);
    _u8g2.setCursor(0, 64);
    _u8g2.print("Status: OK | Acc: "); _u8g2.print(d.accuracy);
}

// ── Page 2 — System ───────────────────────────────────────────────────────────
void DisplayModule::drawPage2(const DisplayData &d) {
    _u8g2.setFont(u8g2_font_6x12_tr);
    _u8g2.setCursor(0, 10);
    if (d.deviceName) _u8g2.print(d.deviceName);
    drawStatusBar(90, 2, d.rssi, d.batteryPrc);
    _u8g2.drawHLine(0, 13, 128);

    _u8g2.setCursor(4, 26);
    _u8g2.print("WiFi: ");
    if (d.rssi != 0 && d.ssid) { _u8g2.print(d.ssid); _u8g2.print(" "); _u8g2.print(d.rssi); _u8g2.print("dB"); }
    else _u8g2.print("offline");

    _u8g2.setCursor(4, 38);
    _u8g2.print("Bat:  "); _u8g2.print(d.batteryPrc); _u8g2.print("% (");
    _u8g2.print(d.batteryV, 2); _u8g2.print("V)");

    _u8g2.setCursor(4, 50);
    _u8g2.print("Srv:  "); _u8g2.print(d.serverConnected ? "connected" : "offline");

    _u8g2.drawHLine(0, 58, 128);
    _u8g2.setFont(u8g2_font_5x8_tr);
    _u8g2.drawStr(0, 64, "FW: " FIRMWARE_VERSION);
}

// ── Helpers ───────────────────────────────────────────────────────────────────
void DisplayModule::drawStatusBar(int x, int y, int16_t rssi, int batPrc) {
    drawWifi(x, y, rssi);
    drawBattery(x + 20, y, batPrc);
}

void DisplayModule::drawWifi(int x, int y, int16_t rssi) {
    int bars = 0;
    if      (rssi == 0 || rssi <= -90) bars = 0;
    else if (rssi > -60)               bars = 4;
    else if (rssi > -70)               bars = 3;
    else if (rssi > -80)               bars = 2;
    else                               bars = 1;

    if (bars == 0) {
        _u8g2.setFont(u8g2_font_5x8_tr);
        _u8g2.drawStr(x + 2, y + 8, "X");
        return;
    }
    for (int i = 1; i <= 4; i++) {
        if (i > bars) continue;
        int h = i * 2;
        _u8g2.drawBox(x + (i * 3), y + (8 - h), 2, h);
    }
}

void DisplayModule::drawBattery(int x, int y, int percent) {
    _u8g2.drawFrame(x, y, 16, 8);
    _u8g2.drawBox(x + 16, y + 2, 2, 4);
    int fill = map(constrain(percent, 0, 100), 0, 100, 0, 14);
    if (fill > 0) _u8g2.drawBox(x + 1, y + 1, fill, 6);
}
