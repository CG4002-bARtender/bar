#include <Arduino.h>
#include "../config.h"

void setup()
{
  DEBUG_INIT();  
  pinMode(config::INBUILT_LED_PIN, OUTPUT);
}

void loop()
{
  DEBUG_PRINTLN("ON");
  digitalWrite(config::INBUILT_LED_PIN, HIGH);
  delay(config::BLINK_DELAY_MS);

  DEBUG_PRINTLN("OFF");
  digitalWrite(config::INBUILT_LED_PIN, LOW);
  delay(config::BLINK_DELAY_MS);
}
