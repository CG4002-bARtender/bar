#include "mqtt_client.h"
#include "../config.h"

// Internal trampoline: PubSubClient delivers (topic, payload, length) as raw bytes
static MqttMessageCallback s_userCallback = nullptr;
static void pubsubCallback(char* topic, uint8_t* payload, unsigned int length)
{
  if (s_userCallback) s_userCallback(topic, payload, length);
}

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
      messageCount(0),
      userCallback(nullptr),
      subscribedTopic(nullptr)
{
  wifiClient.setInsecure(); // TODO: load CA cert via setCACert() for production
  mqttClient.setBufferSize(config::mqtt::CHUNK_SIZE);
  mqttClient.setServer(broker, port);
  mqttClient.setCallback(pubsubCallback);
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
    DEBUG_PRINTLN("MQTT disconnected. Reconnecting...");
    connectMqtt();
  }
  mqttClient.loop();
}

bool MqttClient::subscribe(const char* topic, MqttMessageCallback callback)
{
  s_userCallback  = callback;
  userCallback    = callback;
  subscribedTopic = topic;
  bool success = mqttClient.subscribe(topic);
  if (!success) DEBUG_PRINTF("Subscribe failed: %s\n", topic);
  else          DEBUG_PRINTF("Subscribed to %s\n", topic);
  return success;
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
  char buffer[config::mqtt::JSON_BUFFER_SIZE];
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
    DEBUG_PRINTLN("Cannot publish: not connected");
    return false;
  }

  bool success = mqttClient.publish(topic, payload, length);
  if (!success)
  {
    DEBUG_PRINTLN("Publish failed");
    return false;
  }

  DEBUG_PRINTF("Published to %s (%u bytes)\n", topic, length);
  messageCount++;
  return true;
}

void MqttClient::connectWifi(const char* ssid, const char* password)
{
  DEBUG_PRINTF("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    DEBUG_PRINTLN("Failed to connect to WiFi. Attempting to reconnect...");
  }

  DEBUG_PRINTLN();
  DEBUG_PRINTF("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
}

bool MqttClient::connectMqtt()
{
  DEBUG_PRINTF("Connecting to MQTT: %s:%d\n", broker, port);

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
    DEBUG_PRINTF("MQTT connect failed, state=%d\n", mqttClient.state());
    return false;
  }

  DEBUG_PRINTLN("MQTT connected!");

  if (subscribedTopic != nullptr)
    mqttClient.subscribe(subscribedTopic);

  return true;
}