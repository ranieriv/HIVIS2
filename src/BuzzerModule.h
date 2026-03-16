#pragma once
#include <Arduino.h>

enum class BuzzerMode { IDLE, ONE_SHOT, WARNING, DANGER };

struct BuzzerStep {
    uint16_t freq;  // 0 = silence
    uint16_t ms;
};

class BuzzerModule {
public:
    explicit BuzzerModule(int pin);

    void begin();
    void update();  // Call every loop — drives non-blocking state machine

    void beepShort();
    void beepReady();
    void beepConnected();
    void startWarning();
    void startDanger();
    void stopAlert();
    void beepError();

    bool isMuted()            { return _muted; }
    void setMuted(bool muted) { _muted = muted; if (muted) silence(); }

private:
    int  _pin;
    bool _muted;

    BuzzerMode _mode;

    BuzzerStep    _seq[8];
    int           _seqLen;
    int           _seqIdx;
    unsigned long _stepStart;

    bool          _alertOn;
    unsigned long _alertStepStart;

    void tone(uint16_t freq);
    void silence();
    void playSeq(const BuzzerStep* steps, int len);
};
