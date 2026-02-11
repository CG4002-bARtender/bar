#include <Arduino.h>
#include "../config.h"
#include "../sensors/button_sensor.h"

ButtonSensor g_button;
int g_pressCount = 0;

void setup()
{
  Serial.begin(config::BAUD_RATE);
  Serial.println("\n=== Button Test ===");
  g_button.setup();
  Serial.println("Press the button...");
}

void loop()
{
  g_button.read();

  if (g_button.wasPressed())
  {
    g_pressCount++;
    Serial.printf("Button pressed! (count: %d)\n", g_pressCount);
  }
}
