#include <Arduino.h>
#include "../config.h"
#include "../sensors/hall_sensor.h"

HallSensor hallSensor;

void setup()
{
  Serial.begin(config::BAUD_RATE);
  Serial.println("=== Hall Sensor Test Script ===");
  
  hallSensor.setup();
}

void loop()
{
  unsigned long now = millis();

  if (hallSensor.shouldRead(now))
  {
    hallSensor.read();
    hallSensor.print();
  }
}
