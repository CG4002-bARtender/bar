#include "hall_task.h"

void HallTask::setup()
{
  hallQueue = xQueueCreate(1, sizeof(int8_t));
}

void HallTask::startPublisher(MqttClient& mqtt, SemaphoreHandle_t mqttMutex)
{
  this->mqtt = &mqtt;
  this->mqttMutex = mqttMutex;
  xTaskCreatePinnedToCore(publishTaskFunc, "hall_pub", 4096, this, 2, NULL, 0);
}

void HallTask::update(int hallIdx)
{
  int8_t idx = (int8_t)hallIdx;
  xQueueOverwrite(hallQueue, &idx);
}

void HallTask::publishTaskFunc(void* param)
{
  HallTask* self = static_cast<HallTask*>(param);
  int8_t hallIdx;
  TickType_t lastPublish = 0;

  while (true)
  {
    TickType_t now = xTaskGetTickCount();

    if ((now - lastPublish) >= pdMS_TO_TICKS(config::MQTT_PUBLISH_INTERVAL_MS) &&
        xQueueReceive(self->hallQueue, &hallIdx, 0) == pdPASS)
    {
      if (xSemaphoreTake(self->mqttMutex, portMAX_DELAY) == pdTRUE)
      {
        self->mqtt->loop();
        char json[32];
        snprintf(json, sizeof(json), "{\"strongest\":%d}", hallIdx);
        self->mqtt->publish(config::MQTT_HALL_TOPIC, json);
        xSemaphoreGive(self->mqttMutex);
      }
      lastPublish = xTaskGetTickCount();
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
