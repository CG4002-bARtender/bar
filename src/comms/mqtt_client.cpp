#include "mqtt_client.h"
#include "../config.h"

MqttClient::MqttClient(const char* broker, int port, const char* clientId, unsigned long publishIntervalMs,
                       const char* username, const char* password)
    : wifiClient(),
      mqttClient(wifiClient),
      broker(broker),
      port(port),
      clientId(clientId),
      username(username),
      password(password),
      publishIntervalMs(publishIntervalMs),
      lastPublishMs(0),
      messageCount(0)
{
  wifiClient.setInsecure(); // TODO: load CA cert via setCACert() for production
  mqttClient.setBufferSize(config::MQTT_CHUNK_SIZE);
  mqttClient.setServer(broker, port);
}

bool MqttClient::connect(const char* ssid, const char* password)
{
  connectWifi(ssid, password);
  return connectMqtt();
}

bool MqttClient::isConnected()
{
  return mqttClient.connected();
}

void MqttClient::loop()
{
  if (!isConnected())
  {
    Serial.println("MQTT disconnected. Reconnecting...");
    connectMqtt();
  }
  mqttClient.loop();
}

bool MqttClient::shouldPublish(unsigned long now)
{
  if (now - lastPublishMs >= publishIntervalMs)
  {
    lastPublishMs = now;
    return true;
  }
  return false;
}

bool MqttClient::publish(const char* topic, const JsonDocument& doc)
{
  char buffer[config::MQTT_JSON_BUFFER_SIZE];
  serializeJson(doc, buffer);
  return publish(topic, buffer);
}

bool MqttClient::publish(const char* topic, const char* payload)
{
  return publish(topic, (const uint8_t*)payload, strlen(payload));
}

bool MqttClient::publish(const char* topic, const uint8_t* payload, unsigned int length)
{
  if (!isConnected())
  {
    Serial.println("Cannot publish: not connected");
    return false;
  }

  bool success = mqttClient.publish(topic, payload, length);
  if (!success)
  {
    Serial.println("Publish failed");
    return false;
  }

  Serial.printf("Published to %s (%u bytes)\n", topic, length);
  messageCount++;
  return true;
}

void MqttClient::connectWifi(const char* ssid, const char* password)
{
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println("Failed to connect to WiFi. Attempting to reconnect...");
  }

  Serial.println();
  Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
}

bool MqttClient::connectMqtt()
{
  Serial.printf("Connecting to MQTT: %s:%d\n", broker, port);

  bool success;
  if (username != nullptr && password != nullptr)
  {
    success = mqttClient.connect(clientId, username, password);
  }
  else
  {
    success = mqttClient.connect(clientId);
  }

  if (!success)
  {
    Serial.printf("MQTT connect failed, state=%d\n", mqttClient.state());
    return false;
  }

  Serial.println("MQTT connected!");
  return true;
}
