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
  const uint8_t* getSamples16Buffer() const { return reinterpret_cast<const uint8_t*>(samples16); }
  size_t getNumBytesRead() const { return numBytesRead; }
  size_t getNumBytes16() const { return numBytes16; }

private:
  int32_t samples[config::MIC_SAMPLE_BATCH_SIZE];
  int16_t samples16[config::MIC_SAMPLE_BATCH_SIZE];
  size_t numBytesRead;
  size_t numBytes16;
};
