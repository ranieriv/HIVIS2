#include "BME688Module.h"

float    BME688Module::_iaq  = 0;
float    BME688Module::_co2  = 0;
float    BME688Module::_bvoc = 0;
float    BME688Module::_temp = 0;
float    BME688Module::_hum  = 0;
uint8_t  BME688Module::_accuracy = 0;
uint32_t BME688Module::_saveIntervalMs = 6UL * 60 * 60 * 1000;
BME688Module* BME688Module::_instance = nullptr;

BME688Module::BME688Module(uint8_t addr, int saveIntervalH)
    : _addr(addr)
{
    _instance       = this;
    _saveIntervalMs = (uint32_t)saveIntervalH * 60UL * 60UL * 1000UL;
}

bool BME688Module::begin() {
    if (!_envSensor.begin(_addr, Wire)) {
        Serial.println("BME688: begin() failed!");
        return false;
    }
    loadState();

    bsec_virtual_sensor_t sensorList[] = {
        BSEC_OUTPUT_IAQ,
        BSEC_OUTPUT_CO2_EQUIVALENT,
        BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
        BSEC_OUTPUT_RAW_GAS
    };
    _envSensor.updateSubscription(sensorList, 6, BSEC_SAMPLE_RATE_LP);
    _envSensor.attachCallback(newDataCallback);
    _ok = true;
    return true;
}

void BME688Module::update() {
    if (!_ok) return;
    if (!_envSensor.run()) {
        if (_envSensor.status < BSEC_OK)
            Serial.printf("BSEC error: %d\n", _envSensor.status);
    }
}

void BME688Module::newDataCallback(const bme68xData data,
                                   const bsecOutputs outputs, Bsec2 bsec) {
    if (!outputs.nOutputs) return;

    for (uint8_t i = 0; i < outputs.nOutputs; i++) {
        const bsecData &o = outputs.output[i];
        switch (o.sensor_id) {
            case BSEC_OUTPUT_IAQ:                                 _iaq = o.signal; _accuracy = o.accuracy; break;
            case BSEC_OUTPUT_CO2_EQUIVALENT:                      _co2 = o.signal; break;
            case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:               _bvoc = o.signal; break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE: _temp = o.signal; break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:    _hum  = o.signal; break;
        }
    }

    static uint8_t  lastSaveAcc  = 0;
    static uint32_t lastSaveTime = 0;
    bool accuracyJustHitMax = (_accuracy == 3 && lastSaveAcc != 3);
    bool intervalElapsed    = (millis() - lastSaveTime >= _saveIntervalMs);

    if (_accuracy > 0 && (accuracyJustHitMax || intervalElapsed)) {
        _instance->saveState();
        lastSaveAcc  = _accuracy;
        lastSaveTime = millis();
    }
}

void BME688Module::loadState() {
    File f = LittleFS.open("/bsec_state.bin", "r");
    if (!f) {
        Serial.println("BSEC: no saved state, starting fresh.");
        return;
    }

    uint8_t  state[BSEC_MAX_STATE_BLOB_SIZE];
    uint32_t storedCrc = 0;
    size_t   n         = f.read(state, BSEC_MAX_STATE_BLOB_SIZE);
    size_t   nc        = f.read((uint8_t*)&storedCrc, sizeof(storedCrc));
    f.close();

    if (n != BSEC_MAX_STATE_BLOB_SIZE || nc != sizeof(storedCrc)) {
        Serial.println("BSEC: state file truncated — starting fresh.");
        LittleFS.remove("/bsec_state.bin");
        return;
    }

    uint32_t calcCrc = crc32_le(0, state, BSEC_MAX_STATE_BLOB_SIZE);
    if (calcCrc != storedCrc) {
        Serial.printf("BSEC: state CRC mismatch (0x%08X vs 0x%08X) — starting fresh.\n",
                      calcCrc, storedCrc);
        LittleFS.remove("/bsec_state.bin");
        return;
    }

    if (_envSensor.setState(state))
        Serial.println("BSEC: state loaded OK.");
    else
        Serial.println("BSEC: setState() failed — starting fresh.");
}

void BME688Module::saveState() {
    uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
    if (!_envSensor.getState(state)) {
        Serial.println("BSEC: getState() failed — not saved.");
        return;
    }
    uint32_t crc = crc32_le(0, state, BSEC_MAX_STATE_BLOB_SIZE);

    File f = LittleFS.open("/bsec_state.bin", "w");
    if (!f) {
        Serial.println("BSEC: could not open state file for write.");
        return;
    }
    f.write(state, BSEC_MAX_STATE_BLOB_SIZE);
    f.write((uint8_t*)&crc, sizeof(crc));
    f.close();
    Serial.println("BSEC: state saved.");
}
