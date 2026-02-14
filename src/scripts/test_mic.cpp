#include <driver/i2s.h>
#include <Arduino.h>
#include <config.h>
#include "../sensors/mic_sensor.h"

MicSensor micSensor;

void setup()
{
  DEBUG_INIT();  
  DEBUG_PRINTLN("\n\n=== INMP441 Connection Test ===\n");

  micSensor.setup();
}

void loop()
{
  micSensor.read();
  micSensor.print();
}