#pragma once

#include "sensor.h"
#include "../config.h"

class ButtonSensor : public Sensor
{
public:
  ButtonSensor();

  void setup() override;
  void read() override;
  void print() override;

  bool wasPressed();

private:
  bool lastReading;
  bool pressed;
};
