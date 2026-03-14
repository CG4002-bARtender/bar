#include <Arduino.h>
#include "../config.h"
#include "../sensors/hall_sensor.h"

HallSensor hallSensor;
int activeLedIndex = -1;

void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("=== Hall Sensor Test Script ===");

  for (int pin : config::hall::LED_PINS)
  {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  hallSensor.setup();
}

void loop()
{
  unsigned long now = millis();

  if (hallSensor.shouldRead(now))
  {
    hallSensor.read();
    hallSensor.print();

    int closest = hallSensor.closestHall;

    if (closest != activeLedIndex)
    {
      if (activeLedIndex >= 0)
        digitalWrite(config::hall::LED_PINS[activeLedIndex], LOW);

      if (closest >= 0)
        digitalWrite(config::hall::LED_PINS[closest], HIGH);

      activeLedIndex = closest;
    }
  }
}
                     