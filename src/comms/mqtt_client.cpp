#include "mqtt_client.h"
#include "../config.h"
#include <SPIFFS.h>

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
  mqttClient.setBufferSize(config::mqtt::CHUNK_SIZE);
  mqttClient.setServer(broker, port);
  mqttClient.setCallback(pubsubCallback);
}

void MqttClient::loadCerts()
{
  if (!SPIFFS.begin(true))
  {
    DEBUG_PRINTLN("SPIFFS mount failed — halting");
    while (true) delay(1000);
  }

  auto readFile = [this](const char* path, String& out) {
    File f = SPIFFS.open(path, "r");
    if (!f)
    {
      DEBUG_PRINTF("Missing cert file: %s — halting\n", path);
      while (true) delay(1000);
    }
    out = f.readString();
    f.close();
  };

  readFile(config::mqtt::CERT_CA,     caCert);
  readFile(config::mqtt::CERT_CLIENT, clientCert);
  readFile(config::mqtt::CERT_KEY,    clientKey);

  wifiClient.setCACert(caCert.c_str());
  wifiClient.setCertificate(clientCert.c_str());
  wifiClient.setPrivateKey(clientKey.c_str());

  DEBUG_PRINTLN("mTLS certs loaded from SPIFFS");
}

bool MqttClient::connect(const char* ssid, const char* password)
{
  loadCerts();
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

  if (!MDNS.begin("bar")) {
    DEBUG_PRINTLN("mDNS init failed");
  } else {
    DEBUG_PRINTLN("mDNS started — .local names resolvable");
  }
}

bool MqttClient::connectMqtt()
{
  DEBUG_PRINTF("Connecting to MQTT: %s:%d\n", broker, port);

  while (true)
  {
    bool success = (username != nullptr && password != nullptr)
      ? mqttClient.connect(clientId, username, password)
      : mqttClient.connect(clientId);

    if (success) break;

    DEBUG_PRINTF("MQTT connect failed, state=%d. Retrying in 2s...\n", mqttClient.state());
    delay(2000);
  }

  DEBUG_PRINTLN("MQTT connected!");

  if (subscribedTopic != nullptr)
    mqttClient.subscribe(subscribedTopic);

  return true;
}