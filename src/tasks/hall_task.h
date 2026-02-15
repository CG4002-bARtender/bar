#pragma once

#include <Arduino.h>
#include "../config.h"
#include "../comms/mqtt_client.h"

class HallTask
{
public:
  void setup();
  void startPublisher(MqttClient& mqtt, SemaphoreHandle_t mqttMutex);
  void update(int hallIdx);

private:
  static void publishTaskFunc(void* param);

  QueueHandle_t hallQueue = nullptr;

  // Shared MQTT (set by startPublisher)
  MqttClient* mqtt = nullptr;
  SemaphoreHandle_t mqttMutex = nullptr;
};
