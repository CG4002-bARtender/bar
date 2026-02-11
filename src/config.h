#pragma once

#include <Arduino.h>

namespace config
{
  // Serial
  constexpr int         BAUD_RATE = 115200;

  // Blink config
  constexpr int         INBUILT_LED_PIN = 2;
  constexpr int         BLINK_DELAY_MS = 500;

  // Mic config
  constexpr int         I2S_WS_PIN = 17;
  constexpr int         I2S_SCK_PIN = 14;
  constexpr int         I2S_SD_PIN = 27;

  constexpr int         MIC_SAMPLE_RATE = 16000;
  constexpr int         MIC_SAMPLE_BATCH_SIZE = 512;
  constexpr int         MIC_DMA_BUFFER_COUNT = 4;
  constexpr int         MIC_DMA_BUFFER_LEN = 1024;

  // Record config
  constexpr int         RECORDING_BUTTON_PIN = 26;
  constexpr int         RECORDING_LED_PIN = D5;
  constexpr int         RECORDING_BUTTON_INTERVAL_MS = 33;
  constexpr int         RECORDING_DURATION_MS = 3000;

  // Hall config
  constexpr int         HALL_SENSOR_PINS[] = {A0, A1, A2, A3, A4 };
  constexpr int         HALL_LED_PINS[] = {D0, D1, D2, D3, D4};
  constexpr size_t      HALL_SENSOR_PINS_LEN = sizeof(HALL_SENSOR_PINS) / sizeof(HALL_SENSOR_PINS[0]);

  constexpr int         HALL_INTERVAL_MS = 33;
  constexpr int         HALL_CALIBRATION_LEN = 30;
  constexpr int         HALL_CALIBRATION_DELAY = 200;

  // WiFi config
  constexpr const char* WIFI_SSID = "Medea";
  constexpr const char* WIFI_PASSWORD = "12345678";

  // MQTT config
  constexpr const char* MQTT_BROKER = "k12141b9.ala.eu-central-1.emqxsl.com";
  constexpr int         MQTT_PORT = 8883;
  constexpr const char* MQTT_USERNAME = "test";  
  constexpr const char* MQTT_PASSWORD = "test";  
  constexpr const char* MQTT_CLIENT_ID = "esp32_glove";
  constexpr const char* MQTT_TOPIC = "glove";
  constexpr int         MQTT_PUBLISH_INTERVAL_MS = 2000;
  constexpr size_t      MQTT_CHUNK_SIZE = 1100;
  constexpr size_t      MQTT_JSON_BUFFER_SIZE = 512;
  constexpr size_t      MQTT_QUEUE_SIZE = 40;

  // Audio MQTT protocol
  constexpr const char* MQTT_AUDIO_TOPIC = "glove/audio";
  constexpr size_t      MQTT_AUDIO_CHUNK_SIZE = 1024;
  constexpr uint16_t    FRAGMENT_SENTINEL = 0xFFFF;
}
