#include <Arduino.h>
#include "../config.h"

void setup()
{
  Serial.begin(config::BAUD_RATE);
  pinMode(config::INBUILT_LED_PIN, OUTPUT);
}

void loop()
{
  Serial.println("ON");
  digitalWrite(config::INBUILT_LED_PIN, HIGH);
  delay(config::BLINK_DELAY_MS);

  Serial.println("OFF");
  digitalWrite(config::INBUILT_LED_PIN, LOW);
  delay(config::BLINK_DELAY_MS);
}
