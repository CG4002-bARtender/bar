#include "mic_sensor.h"

MicSensor::MicSensor() : samples(), numBytesRead(0) {}

MicSensor::~MicSensor()
{
    i2s_driver_uninstall(I2S_NUM_0);
}

void MicSensor::setup()
{
    i2s_config_t i2sConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = config::MIC_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = config::MIC_DMA_BUFFER_COUNT,
        .dma_buf_len = config::MIC_DMA_BUFFER_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pinConfig = {
        .bck_io_num = config::I2S_SCK_PIN,
        .ws_io_num = config::I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = config::I2S_SD_PIN
    };

    esp_err_t err;
    
    err = i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
    Serial.printf("I2S driver install: %s\n", err == ESP_OK ? "OK" : "FAILED");

    err = i2s_set_pin(I2S_NUM_0, &pinConfig);
    Serial.printf("I2S pin config: %s\n", err == ESP_OK ? "OK" : "FAILED");
}

void MicSensor::read()
{
  esp_err_t err = i2s_read(I2S_NUM_0, &samples, sizeof(samples), &numBytesRead, 1000);
  if (err != ESP_OK) {
    numBytesRead = 0;
    Serial.printf("Failed to read!");
  }
}

void MicSensor::print()
{    
  if (numBytesRead > 0) {
    // Print first sample and last sample
    int numSamplesRead = numBytesRead / 4;
    Serial.printf("Bytes: %d | First: %d | Last: %d | ", 
                  numBytesRead, samples[0], samples[numSamplesRead-1]);
    
    // Check if ANY non-zero values
    bool hasData = false;
    for (int i = 0; i < numSamplesRead; i++) {
      if (samples[i] != 0) {
        hasData = true;
        break;
      }
    }
    Serial.println(hasData ? "HAS DATA ✓" : "ALL ZEROS ✗");
  }
}
      