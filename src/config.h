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
    constexpr int SCK_PIN           = D7;
    constexpr int SD_PIN            = 16;
    constexpr int SAMPLE_RATE       = 8000;
    constexpr int SAMPLE_BATCH_SIZE = 250;  // 250 int16 = 500 bytes = one BLE notify per read
    constexpr int DMA_BUFFER_COUNT  = 4;
    constexpr int DMA_BUFFER_LEN    = 256;
  }

  namespace button {
    constexpr int PIN               = D4;
    constexpr int GREEN_LED_PIN     = D3;
    constexpr int RED_LED_PIN       = D2;
    constexpr int INTERVAL_MS       = 33;
    constexpr int DURATION_MS       = 2000;
  }

  namespace hall {
    constexpr int    SENSOR_PINS[]  = {A0};
    constexpr int    LED_PINS[]     = {D2};
    constexpr size_t SENSOR_PINS_LEN = sizeof(SENSOR_PINS) / sizeof(SENSOR_PINS[0]);
    constexpr int    INTERVAL_MS        = 400;
    constexpr int    OVERSAMPLE_COUNT   = 16;   // ADC reads averaged per sample to reject BLE RF glitches
    constexpr int    CALIBRATION_ROUNDS = 30;
    constexpr int    CALIBRATION_DELAY  = 200;
    constexpr int    NOISE_THRESHOLD    = 100;
  }

  namespace audio {
    constexpr size_t        CHUNK_SIZE        = 500;   // bytes per BLE fragment (MTU 517 - 3 ATT - 4 header = 510, using 500 for safety)
    constexpr size_t        MAX_FRAGS         = 100;   // ring buffer depth (~3s at 8kHz)
    constexpr unsigned long TX_INTERVAL_MS    = 32;    // ms between BLE fragment sends
    constexpr uint16_t      FRAGMENT_SENTINEL = 0xFFFF;
  }

  namespace ble {
    constexpr const char* DEVICE_NAME       = "bar";
    constexpr const char* GLOVE_DEVICE_NAME = "glove";
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

  namespace wifi {
    constexpr const char* SSID     = "Medea";
    constexpr const char* PASSWORD = "12345678";
  }

  namespace mqtt {
    constexpr const char* BROKER              = "10.187.150.191";
    constexpr int         PORT                = 1883;
    constexpr const char* USERNAME            = "test";
    constexpr const char* PASSWORD            = "test";
    constexpr const char* CLIENT_ID           = "bar";
    constexpr int         PUBLISH_INTERVAL_MS = 2000;

    constexpr const char* TOPIC_TEST          = "test";
    constexpr const char* TOPIC_AUDIO         = "audio";
    constexpr const char* TOPIC_HALL          = "hall";
    constexpr const char* TOPIC_ACK           = "ack";
    constexpr const char* TOPIC_GLOVE         = "glove";

    constexpr size_t      AUDIO_CHUNK_SIZE    = 512;           // bytes per MQTT publish
    constexpr size_t      CHUNK_SIZE          = AUDIO_CHUNK_SIZE + 32;  // PubSubClient rx/tx buffer
    constexpr size_t      JSON_BUFFER_SIZE    = 512;
    constexpr size_t      MAX_AUDIO_POOL_BYTES= 24 * 1024;    // ~1.5s of audio at 8kHz 16-bit
    constexpr size_t      AUDIO_POOL_SIZE     = MAX_AUDIO_POOL_BYTES / (AUDIO_CHUNK_SIZE + 6); // ~46 slots
  }

  namespace feedback {
    constexpr unsigned long WAIT_BLINK_MS   = 300;   // green slow blink while awaiting ACK
    constexpr unsigned long ACK_BLINK_MS    = 100;   // green fast blink on ACK
    constexpr unsigned long ACK_DURATION_MS = 1000;  // total ACK flash window
    constexpr unsigned long NACK_BLINK_MS   = 200;   // red half-period on NACK
    constexpr int           NACK_BLINK_COUNT = 3;
    constexpr unsigned long ACK_TIMEOUT_MS  = 5000;  // default to NACK if no response within this window
  }
}
