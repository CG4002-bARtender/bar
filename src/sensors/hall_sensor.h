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

  int prevClosestHall;
  int closestHall;

private:
  void calibrate();

  int offsetValues[config::hall::SENSOR_PINS_LEN];
  int baselineValues[config::hall::SENSOR_PINS_LEN];
};
