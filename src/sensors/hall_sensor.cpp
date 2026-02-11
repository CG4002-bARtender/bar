#include "hall_sensor.h"

HallSensor::HallSensor() : Sensor(config::HALL_INTERVAL_MS) {};

void HallSensor::setup()
{
  for (size_t i = 0; i < config::HALL_PINS_LEN; ++i)
  {
    pinMode(config::HALL_PINS[i], INPUT);
    analogSetPinAttenuation(config::HALL_PINS[i], ADC_11db);
  }
  calibrate();
}

void HallSensor::read()
{
  for (size_t i = 0; i < config::HALL_PINS_LEN; ++i)
  {
    offsetValues[i] = analogRead(config::HALL_PINS[i]) - baselineValues[i];
  }
}

void HallSensor::print()
{
  Serial.printf("=========== Hall SENSORS ===========\n");
  for (size_t i = 0; i < config::HALL_PINS_LEN; ++i)
  {
    Serial.printf("[Hall A%d]: %+d\n", i, offsetValues[i]);
  }
  Serial.printf("=====================================\n");
}

void HallSensor::calibrate() 
{
  for (size_t i = 0; i < config::HALL_CALIBRATION_LEN; ++i) 
  {
    for (size_t j = 0; j < config::HALL_PINS_LEN; ++j)
    {
      baselineValues[j] += analogRead(config::HALL_PINS[j]);
    }
    delay(config::HALL_CALIBRATION_DELAY);
  }

  for (size_t j = 0; j < config::HALL_PINS_LEN; ++j)
  {
    baselineValues[j] = baselineValues[j] / config::HALL_CALIBRATION_LEN; 
  }
}
