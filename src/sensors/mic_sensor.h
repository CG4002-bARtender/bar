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
  void flush();
  void print();

  const uint8_t* getSamples() const { return reinterpret_cast<const uint8_t*>(samples); }
  const size_t   getSampleSize() { return numSamples; }
private:
  union {
    int32_t raw32[config::mic::SAMPLE_BATCH_SIZE];
    int16_t samples[config::mic::SAMPLE_BATCH_SIZE];
  };
  size_t numSamples;
};
