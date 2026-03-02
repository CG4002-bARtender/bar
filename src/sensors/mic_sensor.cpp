#include "mic_sensor.h"

MicSensor::MicSensor() : raw32(), numSamples(0) {}

MicSensor::~MicSensor()
{
    i2s_driver_uninstall(I2S_NUM_0);
}

void MicSensor::setup()
{
    i2s_config_t i2sConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = config::mic::SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = config::mic::DMA_BUFFER_COUNT,
        .dma_buf_len = config::mic::DMA_BUFFER_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pinConfig = {
        .bck_io_num = config::mic::SCK_PIN,
        .ws_io_num = config::mic::WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = config::mic::SD_PIN
    };

    esp_err_t err;

    err = i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
    DEBUG_PRINTF("I2S driver install: %s\n", err == ESP_OK ? "OK" : "FAILED");

    err = i2s_set_pin(I2S_NUM_0, &pinConfig);
    DEBUG_PRINTF("I2S pin config: %s\n", err == ESP_OK ? "OK" : "FAILED");
}

void MicSensor::read()
{
  size_t bytesRead;
  esp_err_t err = i2s_read(I2S_NUM_0, raw32, sizeof(raw32), &bytesRead, 1000);
  if (err != ESP_OK) {
    numSamples = 0;
    DEBUG_PRINTF("Failed to read!");
    return;
  }

  numSamples = bytesRead / sizeof(int32_t);
  for (size_t i = 0; i < numSamples; i++)
  {
    samples[i] = (int16_t)(raw32[i] >> 16);
  }
}

size_t MicSensor::readInto(uint8_t* dest)
{
  size_t bytesRead;
  esp_err_t err = i2s_read(I2S_NUM_0, raw32, sizeof(raw32), &bytesRead, 1000);
  if (err != ESP_OK || bytesRead == 0) return 0;

  size_t n = bytesRead / sizeof(int32_t);
  int16_t* out = reinterpret_cast<int16_t*>(dest);
  for (size_t i = 0; i < n; i++)
  {
    out[i] = (int16_t)(raw32[i] >> 16);
  }
  return n * sizeof(int16_t);
}

void MicSensor::flush()
{
  size_t discarded;
  while (i2s_read(I2S_NUM_0, raw32, sizeof(raw32), &discarded, 0) == ESP_OK && discarded > 0);
  numSamples = 0;
}

void MicSensor::print()
{
  if (numSamples > 0) {
    DEBUG_PRINTF("Samples: %d | First: %d | Last: %d | ",
                  numSamples, samples[0], samples[numSamples - 1]);

    bool hasData = false;
    for (size_t i = 0; i < numSamples; i++) {
      if (samples[i] != 0) {
        hasData = true;
        break;
      }
    }
    DEBUG_PRINTLN(hasData ? "HAS DATA" : "ALL ZEROS");
  }
}
