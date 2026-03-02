#include "button_sensor.h"

ButtonSensor::ButtonSensor()
    : Sensor(config::button::INTERVAL_MS),
      lastReading(HIGH),
      pressed(false) {}

void ButtonSensor::setup()
{
  pinMode(config::button::PIN, INPUT_PULLUP);
}

void ButtonSensor::read()
{
  bool reading = digitalRead(config::button::PIN);

  if (lastReading == HIGH && reading == LOW)
  {
    pressed = true;
  }

  lastReading = reading;
}

bool ButtonSensor::wasPressed()
{
  if (pressed)
  {
    pressed = false;
    return true;
  }
  return false;
}

void ButtonSensor::print()
{
  DEBUG_PRINTF("=========== Button ===========\n");
  DEBUG_PRINTF("Pressed: %s\n", wasPressed() ? "YES" : "NO");
  DEBUG_PRINTF("==============================\n");
}
