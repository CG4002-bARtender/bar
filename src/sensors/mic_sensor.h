#pragma once

#include <driver/i2s.h>

#include "sensor.h"
#include "../config.h"

class MicSensor 
{
public:
  MicSensor();
  ~MicSensor();

  void setup();
  void read();
  void print();

private:
  int32_t samples[config::MIC_SAMPLE_BATCH_SIZE];
  size_t numBytesRead;
};
