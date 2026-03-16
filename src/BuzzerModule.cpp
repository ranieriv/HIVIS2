#include "BuzzerModule.h"

static const int LEDC_CHANNEL = 0;
static const int LEDC_RES     = 8;

BuzzerModule::BuzzerModule(int pin)
    : _pin(pin), _muted(false), _mode(BuzzerMode::IDLE),
      _seqLen(0), _seqIdx(0), _stepStart(0),
      _alertOn(false), _alertStepStart(0) {}

void BuzzerModule::begin() {
    ledcSetup(LEDC_CHANNEL, 1000, LEDC_RES);
    ledcAttachPin(_pin, LEDC_CHANNEL);
    silence();
}

void BuzzerModule::tone(uint16_t freq) {
    if (_muted || freq == 0) { silence(); return; }
    ledcSetup(LEDC_CHANNEL, freq, LEDC_RES);
    ledcWrite(LEDC_CHANNEL, 128);
}

void BuzzerModule::silence() {
    ledcWrite(LEDC_CHANNEL, 0);
}

void BuzzerModule::playSeq(const BuzzerStep* steps, int len) {
    if (len == 0 || len > 8) return;
    for (int i = 0; i < len; i++) _seq[i] = steps[i];
    _seqLen    = len;
    _seqIdx    = 0;
    _stepStart = millis();
    _mode      = BuzzerMode::ONE_SHOT;
    tone(_seq[0].freq);
}

void BuzzerModule::update() {
    switch (_mode) {
        case BuzzerMode::IDLE: break;

        case BuzzerMode::ONE_SHOT:
            if (millis() - _stepStart >= _seq[_seqIdx].ms) {
                _seqIdx++;
                if (_seqIdx >= _seqLen) { silence(); _mode = BuzzerMode::IDLE; }
                else { _stepStart = millis(); tone(_seq[_seqIdx].freq); }
            }
            break;

        case BuzzerMode::WARNING: {
            unsigned long period = _alertOn ? 500UL : 1000UL;
            if (millis() - _alertStepStart >= period) {
                _alertOn = !_alertOn;
                _alertStepStart = millis();
                _alertOn ? tone(800) : silence();
            }
            break;
        }
        case BuzzerMode::DANGER: {
            if (millis() - _alertStepStart >= 200UL) {
                _alertOn = !_alertOn;
                _alertStepStart = millis();
                _alertOn ? tone(1200) : silence();
            }
            break;
        }
    }
}

void BuzzerModule::beepShort()     { if (_muted) return; static const BuzzerStep s[] = {{1000,50},{0,0}};           playSeq(s,2); }
void BuzzerModule::beepReady()     { if (_muted) return; static const BuzzerStep s[] = {{1200,100},{0,100},{1200,100},{0,0}}; playSeq(s,4); }
void BuzzerModule::beepConnected() { if (_muted) return; static const BuzzerStep s[] = {{800,100},{0,50},{1200,150},{0,0}};  playSeq(s,4); }
void BuzzerModule::beepError()     { if (_muted) return; static const BuzzerStep s[] = {{400,80},{0,40},{400,80},{0,40},{400,80},{0,0}}; playSeq(s,6); }

void BuzzerModule::startWarning() {
    if (_muted || _mode == BuzzerMode::WARNING) return;
    _mode = BuzzerMode::WARNING; _alertOn = true; _alertStepStart = millis(); tone(800);
}

void BuzzerModule::startDanger() {
    if (_muted || _mode == BuzzerMode::DANGER) return;
    _mode = BuzzerMode::DANGER; _alertOn = true; _alertStepStart = millis(); tone(1200);
}

void BuzzerModule::stopAlert() {
    silence();
    _mode    = BuzzerMode::IDLE;
    _alertOn = false;
    if (!_muted) {
        static const BuzzerStep s[] = {{1000,150},{0,40},{600,150},{0,0}};
        playSeq(s, 4);
    }
}
