#pragma once

#include "sensor.h"
#include "../config.h"

class HallSensor : public Sensor
{
public:
  HallSensor();

  void setup() override;
  void read() override;
  void print() override;

private:
  void calibrate();

  int offsetValues[config::HALL_SENSOR_PINS_LEN];
  int baselineValues[config::HALL_SENSOR_PINS_LEN];
};
