#pragma once

#include <Arduino.h>

// Set to 0 to disable all debug serial prints for production (speeds up program significantly)
#define DEBUG_MODE 1

#if DEBUG_MODE
  #define DEBUG_INIT()        Serial.begin(config::BAUD_RATE)
  #define DEBUG_PRINT(...)    Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...)  Serial.println(__VA_ARGS__)
  #define DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_INIT()        ((void)0)
  #define DEBUG_PRINT(...)    ((void)0)
  #define DEBUG_PRINTLN(...)  ((void)0)
  #define DEBUG_PRINTF(...)   ((void)0)
#endif

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
  constexpr int         RECORDING_LED_PIN = D2;
  constexpr int         RECORDING_BUTTON_INTERVAL_MS = 33;
  constexpr int         RECORDING_DURATION_MS = 3000;

  namespace hall {
    constexpr int         SENSOR_PINS[] = {36, 39, 34, 35, 15};
    constexpr int         LED_PINS[] = {25, 26, 27, 13, 5 };
    constexpr size_t      SENSOR_PINS_LEN = sizeof(SENSOR_PINS) / sizeof(SENSOR_PINS[0]);
    constexpr int         INTERVAL_MS = 400;
    constexpr int         CALIBRATION_ROUNDS = 30;
    constexpr int         CALIBRATION_DELAY = 200;
    constexpr int         NOISE_THRESHOLD = 30;
  }

  namespace ble {
    constexpr const char* DEVICE_NAME       = "bar";
    constexpr const char* SERVICE_UUID      = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
    constexpr const char* CHAR_UUID_TX      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
    constexpr const int   PING_INTERVAL_MS  = 1000;
    constexpr const int   PUBLISH_INTERVAL_MS = 50; 
    constexpr uint16_t    CONN_MIN_INTERVAL = 16;  
    constexpr uint16_t    CONN_MAX_INTERVAL = 32;   
    constexpr uint16_t    CONN_LATENCY      = 0;
    constexpr uint16_t    CONN_TIMEOUT      = 600; 
  }
}
