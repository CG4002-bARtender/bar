#include <Arduino.h>
#include <config.h>
#include "../comms/mqtt_client.h"

MqttClient mqttClient(
  config::mqtt::BROKER,
  config::mqtt::PORT,
  config::mqtt::CLIENT_ID,
  config::mqtt::PUBLISH_INTERVAL_MS,
  config::mqtt::USERNAME,
  config::mqtt::PASSWORD
);

void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("\n\n=== MQTT Connection Test ===\n");
  DEBUG_PRINTF("Broker: %s:%d\n", config::mqtt::BROKER, config::mqtt::PORT);
  DEBUG_PRINTF("Topic:  %s\n\n", config::mqtt::TOPIC_TEST);

  bool ok = mqttClient.connect(config::wifi::SSID, config::wifi::PASSWORD);
  if (!ok) {
    DEBUG_PRINTLN("[FAIL] Could not connect to MQTT broker. Halting.");
    while (true) delay(1000);
  }
}

void loop()
{
  mqttClient.loop();

  unsigned long now = millis();
  if (mqttClient.shouldPublish(now)) {
    int count = mqttClient.getMessageCount() + 1;
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"msg\": %d}", count);

    bool ok = mqttClient.publish(config::mqtt::TOPIC_TEST, payload);
    DEBUG_PRINTF("[%d] publish %s -> %s\n", count, payload, ok ? "OK" : "FAIL");
  }
}
