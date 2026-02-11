#include <Arduino.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "../comms/mqtt_client.h"

MqttClient mqttClient(config::MQTT_BROKER, config::MQTT_PORT, config::MQTT_CLIENT_ID, config::MQTT_PUBLISH_INTERVAL_MS,
                  config::MQTT_USERNAME, config::MQTT_PASSWORD);

void createDummyData(JsonDocument &doc);

void setup()
{
  Serial.begin(config::BAUD_RATE);
  Serial.println("=== MQTT Test Script ===");

  while (!mqttClient.connect(config::WIFI_SSID, config::WIFI_PASSWORD))
  {
    Serial.println("Failed to connect. Reconnecting...");
    delay(5000);
  }
}

void loop()
{
  mqttClient.loop();

  unsigned long now = millis();

  if (mqttClient.shouldPublish(now))
  {
    JsonDocument doc;
    createDummyData(doc);

    mqttClient.publish(config::MQTT_TOPIC, doc);
  }
}

void createDummyData(JsonDocument &doc)
{
  doc["msg_id"] = mqttClient.getMessageCount();
  doc["timestamp"] = millis();  
  doc["message"] = "Hello from bar";
}
