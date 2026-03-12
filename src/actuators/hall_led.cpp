#include "hall_led.h"

void HallLED::setup() {
  for (size_t i = 0; i < config::hall::SENSOR_PINS_LEN; i++)
  {
    pinMode(config::hall::LED_PINS[i], OUTPUT);
    digitalWrite(config::hall::LED_PINS[i], LOW);
  }
}

void HallLED::offLED(int led) {
  digitalWrite(config::hall::LED_PINS[led], LOW);
}

void HallLED::onLED(int led) {
  digitalWrite(config::hall::LED_PINS[led], HIGH);
}