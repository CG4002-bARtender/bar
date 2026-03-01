#include "hall_sensor.h"

HallSensor::HallSensor() : Sensor(config::hall::INTERVAL_MS), closestHall(0) {};

void HallSensor::setup()
{
  for (size_t i = 0; i < config::hall::SENSOR_PINS_LEN; ++i)
  {
    pinMode(config::hall::SENSOR_PINS[i], INPUT);
    analogSetPinAttenuation(config::hall::SENSOR_PINS[i], ADC_11db);
  }
  calibrate();
}

void HallSensor::read()
{
  closestHall = -1;
  int largestSeen = 0;
  for (size_t i = 0; i < config::hall::SENSOR_PINS_LEN; ++i)
  {
    offsetValues[i] = analogRead(config::hall::SENSOR_PINS[i]) - baselineValues[i];
    if (offsetValues[i] > largestSeen && offsetValues[i] > 30) {
      closestHall = i;
      largestSeen = offsetValues[i];
    }
  }
}

void HallSensor::print()
{
  DEBUG_PRINTF("=========== Hall SENSORS ===========\n");
  for (size_t i = 0; i < config::hall::SENSOR_PINS_LEN; ++i)
  {
    DEBUG_PRINTF("[Hall A%d]: %+d\n", i, offsetValues[i]);
  }
  DEBUG_PRINTF(closestHall == -1 ? "No magnet detected!\n" : "Closest Hall Sensor: A%d\n", closestHall);
  DEBUG_PRINTF("=====================================\n");
}

void HallSensor::calibrate() 
{
  for (size_t i = 0; i < config::hall::CALIBRATION_ROUNDS; ++i) 
  {
    for (size_t j = 0; j < config::hall::SENSOR_PINS_LEN; ++j)
    {
      baselineValues[j] += analogRead(config::hall::SENSOR_PINS[j]);
    }
    delay(config::hall::CALIBRATION_DELAY);
  }

  for (size_t j = 0; j < config::hall::SENSOR_PINS_LEN; ++j)
  {
    baselineValues[j] = baselineValues[j] / config::hall::CALIBRATION_ROUNDS; 
  }
}
