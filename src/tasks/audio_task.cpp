#include "audio_task.h"

void AudioTask::setup()
{
  pool = new AudioMessage[config::AUDIO_POOL_SIZE];
  audioQueue = xQueueCreate(config::AUDIO_POOL_SIZE, sizeof(AudioMessage*));
  freePool   = xQueueCreate(config::AUDIO_POOL_SIZE, sizeof(AudioMessage*));

  for (size_t i = 0; i < config::AUDIO_POOL_SIZE; i++)
  {
    AudioMessage* p = &pool[i];
    xQueueSend(freePool, &p, 0);
  }
}

void AudioTask::startPublisher(MqttClient& mqtt, SemaphoreHandle_t mqttMutex)
{
  this->mqtt = &mqtt;
  this->mqttMutex = mqttMutex;
  xTaskCreatePinnedToCore(publishTaskFunc, "audio_pub", 4096, this, 1, NULL, 0);
}

void AudioTask::beginRecording(MicSensor& mic)
{
  mic.flush();
  messageId++;
  fragmentId = 0;
  currentSlot = nullptr;
  slotOffset = 0;
}

void AudioTask::record(MicSensor& mic)
{
  // Acquire a pool slot if we don't have one
  if (!currentSlot)
  {
    if (xQueueReceive(freePool, &currentSlot, 0) != pdPASS)
    {
      DEBUG_PRINTLN("Pool exhausted, samples dropped");
      mic.read();  // drain DMA to prevent stall
      return;
    }
    slotOffset = 0;
  }

  // Read I2S and convert 32->16 bit directly into pool slot (after 4-byte header)
  size_t bytesWritten = mic.readInto(currentSlot->data + 4 + slotOffset);
  if (bytesWritten == 0) return;
  slotOffset += bytesWritten;

  // If slot is full, write header and enqueue
  if (slotOffset >= config::MQTT_AUDIO_CHUNK_SIZE)
  {
    writeHeader(currentSlot->data, messageId, fragmentId);
    currentSlot->length = 4 + slotOffset;
    xQueueSend(audioQueue, &currentSlot, 0);
    currentSlot = nullptr;
    fragmentId++;
  }
}

void AudioTask::endRecording()
{
  // Flush any partial slot
  flushCurrentSlot();

  // Send sentinel fragment
  AudioMessage* slot;
  if (xQueueReceive(freePool, &slot, 0) == pdPASS)
  {
    writeHeader(slot->data, messageId, config::FRAGMENT_SENTINEL);
    slot->length = 4;
    xQueueSend(audioQueue, &slot, 0);
  }

  DEBUG_PRINTF("Session %u complete: %u fragments.\n", messageId, fragmentId);
}

void AudioTask::flushCurrentSlot()
{
  if (currentSlot && slotOffset > 0)
  {
    writeHeader(currentSlot->data, messageId, fragmentId);
    currentSlot->length = 4 + slotOffset;
    xQueueSend(audioQueue, &currentSlot, 0);
    currentSlot = nullptr;
    fragmentId++;
  }
  else if (currentSlot)
  {
    // Empty slot, return to pool
    xQueueSend(freePool, &currentSlot, 0);
    currentSlot = nullptr;
  }
}

void AudioTask::writeHeader(uint8_t* out, uint16_t msgId, uint16_t fragId)
{
  out[0] = msgId & 0xFF;
  out[1] = (msgId >> 8) & 0xFF;
  out[2] = fragId & 0xFF;
  out[3] = (fragId >> 8) & 0xFF;
}

void AudioTask::publishTaskFunc(void* param)
{
  AudioTask* self = static_cast<AudioTask*>(param);
  AudioMessage* msg;

  while (true)
  {
    if (xQueueReceive(self->audioQueue, &msg, pdMS_TO_TICKS(10)) == pdPASS)
    {
      if (xSemaphoreTake(self->mqttMutex, portMAX_DELAY) == pdTRUE)
      {
        self->mqtt->loop();
        self->mqtt->publish(config::MQTT_AUDIO_TOPIC, msg->data, msg->length);
        xSemaphoreGive(self->mqttMutex);
      }
      xQueueSend(self->freePool, &msg, 0);  // return slot to pool
    }
    else
    {
      // No audio pending - just maintain MQTT keepalive
      if (xSemaphoreTake(self->mqttMutex, pdMS_TO_TICKS(50)) == pdTRUE)
      {
        self->mqtt->loop();
        xSemaphoreGive(self->mqttMutex);
      }
    }
  }
}
