#pragma once

#include <driver/i2s.h>

#include "sensor.h"
#include "../config.h"

class MicSensor : public Sensor
{
public:
  MicSensor();
  ~MicSensor();

  void setup() override;
  void read() override;
  void print() override;

private:
  int32_t samples[config::MIC_SAMPLE_BATCH_SIZE];
  size_t numBytesRead;
};
