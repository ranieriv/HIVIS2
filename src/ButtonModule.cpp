#include "ButtonModule.h"

ButtonModule::ButtonModule(int pin, int longPressMs, int doublePressMs)
    : _pin(pin), _longPressMs(longPressMs), _doublePressMs(doublePressMs),
      _lastRaw(HIGH), _debounced(HIGH), _debounceTime(0),
      _pressStart(0), _pressed(false), _lastReleaseTime(0), _pendingShorts(0),
      _shortFlag(false), _doubleFlag(false), _longFlag(false), _factoryFlag(false) {}

void ButtonModule::begin() {
    pinMode(_pin, INPUT_PULLUP);
}

void ButtonModule::update() {
    bool raw = (digitalRead(_pin) == LOW);

    if (raw != _lastRaw) {
        _debounceTime = millis();
        _lastRaw = raw;
    }
    if (millis() - _debounceTime < DEBOUNCE_MS) return;

    bool prevDebounced = _debounced;
    _debounced = raw;

    // Falling edge — pressed
    if (_debounced && !prevDebounced) {
        _pressStart = millis();
        _pressed    = true;
    }

    // Rising edge — released
    if (!_debounced && prevDebounced && _pressed) {
        _pressed = false;
        unsigned long duration = millis() - _pressStart;

        if (duration >= 5000) {
            _factoryFlag = true;
        } else if (duration >= (unsigned long)_longPressMs) {
            _longFlag = true;
        } else {
            _pendingShorts++;
            _lastReleaseTime = millis();
        }
    }

    // Resolve pending shorts after double-press window
    if (_pendingShorts > 0 && !_pressed &&
        millis() - _lastReleaseTime >= (unsigned long)_doublePressMs) {
        if (_pendingShorts >= 2) _doubleFlag = true;
        else                     _shortFlag  = true;
        _pendingShorts = 0;
    }
}

bool ButtonModule::shortPressed()  { if (_shortFlag)   { _shortFlag   = false; return true; } return false; }
bool ButtonModule::doublePressed() { if (_doubleFlag)  { _doubleFlag  = false; return true; } return false; }
bool ButtonModule::longPressed()   { if (_longFlag)    { _longFlag    = false; return true; } return false; }
bool ButtonModule::factoryReset()  { if (_factoryFlag) { _factoryFlag = false; return true; } return false; }
