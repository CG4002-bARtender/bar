#pragma once

#include <Arduino.h>

class Sensor
{
public:
  Sensor(unsigned long intervalMs) : intervalMs(intervalMs), lastReadMs(0) {}
  virtual ~Sensor() = default;

  virtual void setup() = 0;
  virtual void read() = 0;
  virtual void print() = 0;

  bool shouldRead(unsigned long now)
  {
    if (now - lastReadMs >= intervalMs)
    {
      lastReadMs = now;
      return true;
    }
    return false;
  }

protected:
  unsigned long intervalMs;
  unsigned long lastReadMs;
};
