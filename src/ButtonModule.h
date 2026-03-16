#pragma once
#include <Arduino.h>

class ButtonModule {
public:
    ButtonModule(int pin, int longPressMs, int doublePressMs);

    void begin();
    void update();  // Call every loop

    // Consumed (cleared) on first read
    bool shortPressed();
    bool doublePressed();
    bool longPressed();
    bool factoryReset();

private:
    int  _pin;
    int  _longPressMs;
    int  _doublePressMs;

    bool          _lastRaw;
    bool          _debounced;
    unsigned long _debounceTime;
    static const int DEBOUNCE_MS = 20;

    unsigned long _pressStart;
    bool          _pressed;
    unsigned long _lastReleaseTime;
    int           _pendingShorts;

    bool _shortFlag;
    bool _doubleFlag;
    bool _longFlag;
    bool _factoryFlag;
};
