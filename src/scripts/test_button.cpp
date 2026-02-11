#include <Arduino.h>
#include "../config.h"
#include "../sensors/button_sensor.h"

ButtonSensor button;

void setup()
{
  Serial.begin(config::BAUD_RATE);
  Serial.println("\n=== Button Test ===");
  button.setup();
  Serial.println("Press the button...");
}

void loop()
{
  unsigned long now = millis();

  if (button.shouldRead(now)) {
    button.read();
    button.print();
  }
}
