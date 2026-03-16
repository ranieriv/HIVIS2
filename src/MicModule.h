#pragma once
#include <Arduino.h>
#include "driver/i2s.h"

class MicModule {
public:
    MicModule(int ws, int sck, int sd, int lr, double calibration);

    void   begin();
    double readDB();

private:
    int    _ws, _sck, _sd, _lr;
    double _calibration_offset;

    static const int        SAMPLE_BUFFER_SIZE = 256;
    static const i2s_port_t I2S_PORT = I2S_NUM_0;
};
