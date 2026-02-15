#pragma once

#include <Arduino.h>
#include "../config.h"
#include "../comms/mqtt_client.h"
#include "../sensors/mic_sensor.h"

struct AudioMessage
{
  uint16_t length;
  uint8_t data[4 + config::MQTT_AUDIO_CHUNK_SIZE];
};

class AudioTask
{
public:
  void setup();
  void startPublisher(MqttClient& mqtt, SemaphoreHandle_t mqttMutex);

  // Called from Core 1 state machine
  void beginRecording(MicSensor& mic);
  void record(MicSensor& mic);
  void endRecording();

  uint16_t getMessageId() const { return messageId; }
  uint16_t getFragmentCount() const { return fragmentId; }

private:
  static void publishTaskFunc(void* param);
  static void writeHeader(uint8_t* out, uint16_t msgId, uint16_t fragId);
  void flushCurrentSlot();

  AudioMessage* pool = nullptr;
  QueueHandle_t audioQueue = nullptr;
  QueueHandle_t freePool = nullptr;

  // Current slot being filled
  AudioMessage* currentSlot = nullptr;
  size_t slotOffset = 0;

  uint16_t messageId = 0;
  uint16_t fragmentId = 0;

  // Shared MQTT (set by startPublisher)
  MqttClient* mqtt = nullptr;
  SemaphoreHandle_t mqttMutex = nullptr;
};
