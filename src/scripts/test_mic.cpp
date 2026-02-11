#include <driver/i2s.h>
#include <Arduino.h>
#include <config.h>
#include "../sensors/mic_sensor.h"

MicSensor micSensor;

void setup()
{
  Serial.begin(config::BAUD_RATE);
  Serial.println("\n\n=== INMP441 Connection Test ===\n");

  micSensor.setup();
}

void loop()
{
  micSensor.read();
  micSensor.print();
}