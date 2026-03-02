#include <Arduino.h>
#include "../config.h"

void setup()
{
  DEBUG_INIT();
  pinMode(config::blink::LED_PIN, OUTPUT);
}

void loop()
{
  DEBUG_PRINTLN("ON");
  digitalWrite(config::blink::LED_PIN, HIGH);
  delay(config::blink::DELAY_MS);

  DEBUG_PRINTLN("OFF");
  digitalWrite(config::blink::LED_PIN, LOW);
  delay(config::blink::DELAY_MS);
}
