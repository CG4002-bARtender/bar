#pragma once

#include <Arduino.h>

namespace config
{
  // Serial
  constexpr int BAUD_RATE = 115200;

  // Blink config
  constexpr int INBUILT_LED_PIN = 2;
  constexpr int BLINK_DELAY_MS = 500;
}
