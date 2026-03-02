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
  constexpr int BAUD_RATE = 115200;

  namespace blink {
    constexpr int LED_PIN   = 2;
    constexpr int DELAY_MS  = 500;
  }

  namespace mic {
    constexpr int WS_PIN            = 17;
    constexpr int SCK_PIN           = 14;
    constexpr int SD_PIN            = 16;
    constexpr int SAMPLE_RATE       = 8000;
    constexpr int SAMPLE_BATCH_SIZE = 250;  // 250 int16 = 500 bytes = one BLE notify per read
    constexpr int DMA_BUFFER_COUNT  = 4;
    constexpr int DMA_BUFFER_LEN    = 256;
  }

  namespace button {
    constexpr int PIN         = 22;
    constexpr int GREEN_LED_PIN     = 2;
    constexpr int RED_LED_PIN = 21;
    constexpr int INTERVAL_MS = 33;
    constexpr int DURATION_MS = 3000;
  }

  namespace hall {
    constexpr int    SENSOR_PINS[]  = {36, 39, 34, 35, 15};
    constexpr int    LED_PINS[]     = {25, 26, 27, 13, 5};
    constexpr size_t SENSOR_PINS_LEN = sizeof(SENSOR_PINS) / sizeof(SENSOR_PINS[0]);
    constexpr int    INTERVAL_MS        = 400;
    constexpr int    OVERSAMPLE_COUNT   = 16;   // ADC reads averaged per sample to reject BLE RF glitches
    constexpr int    CALIBRATION_ROUNDS = 30;
    constexpr int    CALIBRATION_DELAY  = 200;
    constexpr int    NOISE_THRESHOLD    = 300;
  }

  namespace audio {
    constexpr size_t        CHUNK_SIZE        = 500;   // bytes per BLE fragment (MTU 517 - 3 ATT - 4 header = 510, using 500 for safety)
    constexpr size_t        MAX_FRAGS         = 100;   // ring buffer depth (~3s at 8kHz)
    constexpr unsigned long TX_INTERVAL_MS    = 32;    // ms between BLE fragment sends
    constexpr uint16_t      FRAGMENT_SENTINEL = 0xFFFF;
  }

  namespace ble {
    constexpr const char* DEVICE_NAME       = "bar";
    constexpr const char* SERVICE_UUID      = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
    constexpr const char* CHAR_UUID_TX      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
    constexpr const char* CHAR_UUID_RX      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
    constexpr int         PING_INTERVAL_MS  = 1000;
    constexpr int         PUBLISH_INTERVAL_MS = 50;
    constexpr uint16_t    CONN_MIN_INTERVAL = 16;
    constexpr uint16_t    CONN_MAX_INTERVAL = 16;
    constexpr uint16_t    CONN_LATENCY      = 0;
    constexpr uint16_t    CONN_TIMEOUT      = 600;
  }

  namespace feedback {
    constexpr unsigned long WAIT_BLINK_MS   = 300;   // green slow blink while awaiting ACK
    constexpr unsigned long ACK_BLINK_MS    = 100;   // green fast blink on ACK
    constexpr unsigned long ACK_DURATION_MS = 1000;  // total ACK flash window
    constexpr unsigned long NACK_BLINK_MS   = 200;   // red half-period on NACK
    constexpr int           NACK_BLINK_COUNT = 3;
    constexpr unsigned long ACK_TIMEOUT_MS  = 1000;  // default to NACK if no response within this window
  }
}
