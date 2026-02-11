#include "button_sensor.h"

ButtonSensor::ButtonSensor()
    : Sensor(config::BUTTON_INTERVAL_MS),
      lastReading(HIGH),
      pressed(false) {}

void ButtonSensor::setup()
{
  pinMode(config::BUTTON_PIN, INPUT_PULLUP);
}

void ButtonSensor::read()
{
  bool reading = digitalRead(config::BUTTON_PIN);

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
  Serial.printf("=========== Button ===========\n");
  Serial.printf("Pressed flag: %s\n", pressed ? "YES" : "NO");
  Serial.printf("==============================\n");
}
