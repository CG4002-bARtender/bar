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

  const uint8_t* getSamplesBuffer() const { return reinterpret_cast<const uint8_t*>(samples); }
  size_t getNumBytesRead() const { return numBytesRead; }

private:
  int32_t samples[config::MIC_SAMPLE_BATCH_SIZE];
  size_t numBytesRead;
};
