#pragma once

#include <Arduino.h>

namespace config
{
  // Serial
  constexpr int BAUD_RATE = 115200;

  // Blink config
  constexpr int INBUILT_LED_PIN = 2;
  constexpr int BLINK_DELAY_MS = 500;

  // Mic config
  constexpr int I2S_WS_PIN = 17;
  constexpr int I2S_SCK_PIN = 14;
  constexpr int I2S_SD_PIN = 27;

  constexpr int MIC_INTERVAL_MS = 20;
  constexpr int MIC_SAMPLE_RATE = 16000;
  constexpr int MIC_SAMPLE_BATCH_SIZE = 64;
  constexpr int MIC_DMA_BUFFER_COUNT = 4;
  constexpr int MIC_DMA_BUFFER_LEN = 1024;

  // Button config
  constexpr int BUTTON_PIN = 26;
  constexpr int BUTTON_INTERVAL_MS = 33;

  // Wifi config

}
