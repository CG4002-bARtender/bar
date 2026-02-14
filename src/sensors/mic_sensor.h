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

  const uint8_t* getSamplesBuffer() const { return reinterpret_cast<const uint8_t*>(samples); }
  size_t getNumBytes() const { return numSamples * sizeof(int16_t); }
  size_t getNumSamples() const { return numSamples; }

private:
  union {
    int32_t raw32[config::MIC_SAMPLE_BATCH_SIZE];
    int16_t samples[config::MIC_SAMPLE_BATCH_SIZE];
  };
  size_t numSamples;
};
