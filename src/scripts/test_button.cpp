#include <Arduino.h>
#include "../config.h"
#include "../sensors/button_sensor.h"

ButtonSensor button;

void setup()
{
  DEBUG_INIT();  
  DEBUG_PRINTLN("\n=== Button Test ===");
  button.setup();
  DEBUG_PRINTLN("Press the button...");
}

void loop()
{
  unsigned long now = millis();

  if (button.shouldRead(now)) {
    button.read();
    
    if (button.wasPressed()) {
      button.print();
    }
  }
}
