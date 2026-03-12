#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

using MqttMessageCallback = void(*)(const char* topic, const uint8_t* payload, unsigned int length);

class MqttClient
{
public:
  MqttClient(const char* broker, int port, const char* clientId, unsigned long publishIntervalMs,
             const char* username = nullptr, const char* password = nullptr);

  bool connect(const char* ssid, const char* wifiPassword);
  bool isConnected();
  void loop();

  bool subscribe(const char* topic, MqttMessageCallback callback);

  bool shouldPublish(unsigned long now);
  bool publish(const char* topic, const JsonDocument& doc);
  bool publish(const char* topic, const char* payload);
  bool publish(const char* topic, const uint8_t* payload, unsigned int length);

  int getMessageCount() const { return messageCount; }

private:
  void connectWifi(const char* ssid, const char* password);
  bool connectMqtt();

  WiFiClientSecure wifiClient;
  PubSubClient mqttClient;

  const char* broker;
  int port;
  const char* clientId;
  const char* username;
  const char* password;

  unsigned long         publishIntervalMs;
  unsigned long         lastPublishMs;
  int                   messageCount;
  MqttMessageCallback   userCallback;
  const char*           subscribedTopic;
};