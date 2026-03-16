#include "MicModule.h"
#include <math.h>

MicModule::MicModule(int ws, int sck, int sd, int lr, double calibration)
    : _ws(ws), _sck(sck), _sd(sd), _lr(lr), _calibration_offset(calibration) {}

void MicModule::begin() {
    pinMode(_lr, OUTPUT);
    digitalWrite(_lr, LOW); // RIGHT channel

    i2s_config_t i2s_config = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = 44100,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = 128,
        .use_apll             = false
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num   = _sck,
        .ws_io_num    = _ws,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = _sd
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

double MicModule::readDB() {
    int32_t samples[SAMPLE_BUFFER_SIZE];
    size_t  bytes_read = 0;

    esp_err_t result = i2s_read(I2S_PORT, (void*)samples,
                                sizeof(samples), &bytes_read, portMAX_DELAY);
    if (result != ESP_OK || bytes_read == 0) return 0.0;

    int    count      = bytes_read / 4;
    double sumSquares = 0.0;
    for (int i = 0; i < count; i++) {
        float norm = (float)samples[i] / 2147483648.0f;
        sumSquares += (double)(norm * norm);
    }

    double rms = sqrt(sumSquares / count);
    if (rms < 1e-7) return 0.0;
    return 20.0 * log10(rms) + _calibration_offset;
}
