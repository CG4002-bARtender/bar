#include <Arduino.h>
#include "config.h"
#include "sensors/mic_sensor.h"
#include "sensors/button_sensor.h"
#include "sensors/hall_sensor.h"
#include "comms/mqtt_client.h"

// --- Queue structs ---

struct AudioMessage
{
  uint16_t length;
  uint8_t data[4 + config::MQTT_AUDIO_CHUNK_SIZE];
};

// --- State machine ---

enum class State { IDLE, RECORDING, END };

State         state = State::IDLE;
uint16_t      messageId = 0;
uint16_t      fragmentId = 0;
unsigned long recordStartMs = 0;

// Accumulation state: current pool slot being filled
AudioMessage* currentSlot = nullptr;
size_t        slotOffset  = 0;

// --- Peripherals ---

MicSensor     mic;
ButtonSensor  button;
HallSensor    hall;
MqttClient    mqtt(config::MQTT_BROKER, config::MQTT_PORT, config::MQTT_CLIENT_ID,
                          config::MQTT_PUBLISH_INTERVAL_MS, config::MQTT_USERNAME, config::MQTT_PASSWORD);

// --- Audio buffer pool (avoids copying ~1 KB structs through queues) ---

static AudioMessage* audioPool;

// --- FreeRTOS queues (audio queues carry pointers, not full structs) ---

QueueHandle_t audioQueue;   // filled AudioMessage* ready to publish
QueueHandle_t freePool;     // recycled AudioMessage* available to fill
QueueHandle_t hallQueue;

SemaphoreHandle_t mqttMutex;

// --- Helpers ---

inline void writeHeader(uint8_t* out, uint16_t msgId, uint16_t fragId)
{
  out[0] = msgId & 0xFF;
  out[1] = (msgId >> 8) & 0xFF;
  out[2] = fragId & 0xFF;
  out[3] = (fragId >> 8) & 0xFF;
}

void flushCurrentSlot();

// --- State handlers ---
void handleIdle();
void handleRecording(unsigned long now);
void handleEnd();

// --- Core 0: MQTT tasks ---

void audioPublishTask(void* param);
void hallPublishTask(void* param);

// --- Core 1: Poll and upload sensor data ---

void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("\n=== Bar Firmware ===");

  // Heap-allocate the audio buffer pool (keeps .bss small)
  audioPool = new AudioMessage[config::AUDIO_POOL_SIZE];

  // Create queues (audio queues carry pointers, not full structs)
  audioQueue = xQueueCreate(config::AUDIO_POOL_SIZE, sizeof(AudioMessage*));
  freePool   = xQueueCreate(config::AUDIO_POOL_SIZE, sizeof(AudioMessage*));
  hallQueue  = xQueueCreate(1, sizeof(int8_t));

  // Seed the free pool with pointers to every slot
  for (size_t i = 0; i < config::AUDIO_POOL_SIZE; i++)
  {
    AudioMessage* p = &audioPool[i];
    xQueueSend(freePool, &p, 0);
  }

  // Sensor setup
  mic.setup();
  button.setup();
  hall.setup();

  // LED setup
  pinMode(config::RECORDING_LED_PIN, OUTPUT);
  digitalWrite(config::RECORDING_LED_PIN, LOW);
  for (size_t i = 0; i < config::HALL_SENSOR_PINS_LEN; i++)
  {
    pinMode(config::HALL_LED_PINS[i], OUTPUT);
    digitalWrite(config::HALL_LED_PINS[i], LOW);
  }

  // Connect WiFi + MQTT (safe: Core 0 tasks don't exist yet)
  while (!mqtt.connect(config::WIFI_SSID, config::WIFI_PASSWORD))
  {
    DEBUG_PRINTLN("MQTT connect failed. Retrying in 5s...");
    delay(5000);
  }

  // Mutex for shared MQTT socket (PubSubClient is not thread-safe)
  mqttMutex = xSemaphoreCreateMutex();

  // Launch MQTT tasks on Core 0 (hall at higher priority to interleave with audio)
  xTaskCreatePinnedToCore(audioPublishTask, "audio_pub", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(hallPublishTask,  "hall_pub",  4096, NULL, 2, NULL, 0);

  DEBUG_PRINTLN("Ready. Press button to start recording.");
}

void loop()
{
  unsigned long now = millis();

  // Poll button
  if (button.shouldRead(now))
  {
    button.read();
  }

  // State machine
  switch (state)
  {
  case State::IDLE:
    handleIdle();
    break;
  case State::RECORDING:
    handleRecording(now);
    break;
  case State::END:
    handleEnd();
    break;
  }

  // Poll hall sensors only when IDLE (skip during recording for max audio throughput)
  if (state == State::IDLE && hall.shouldRead(now))
  {
    hall.read();
    int closestHall = hall.getClosestHall();

    for (size_t i = 0; i < config::HALL_SENSOR_PINS_LEN; i++)
    {
      digitalWrite(config::HALL_LED_PINS[i], i == closestHall);
    }

    xQueueOverwrite(hallQueue, &closestHall);
  }
}

void audioPublishTask(void* param)
{
  AudioMessage* audioMsg;

  while (true)
  {
    if (xQueueReceive(audioQueue, &audioMsg, pdMS_TO_TICKS(10)) == pdPASS)
    {
      if (xSemaphoreTake(mqttMutex, portMAX_DELAY) == pdTRUE)
      {
        mqtt.loop();
        mqtt.publish(config::MQTT_AUDIO_TOPIC, audioMsg->data, audioMsg->length);
        xSemaphoreGive(mqttMutex);
      }
      xQueueSend(freePool, &audioMsg, 0);  // return slot to pool
    }
    else
    {
      // No audio pending — just maintain MQTT keepalive
      if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(50)) == pdTRUE)
      {
        mqtt.loop();
        xSemaphoreGive(mqttMutex);
      }
    }
  }
}

void hallPublishTask(void* param)
{
  int8_t hallIdx;
  TickType_t lastPublish = 0;

  while (true)
  {
    TickType_t now = xTaskGetTickCount();

    if (state == State::IDLE &&
        (now - lastPublish) >= pdMS_TO_TICKS(config::MQTT_PUBLISH_INTERVAL_MS) &&
        xQueueReceive(hallQueue, &hallIdx, 0) == pdPASS)
    {
      if (xSemaphoreTake(mqttMutex, portMAX_DELAY) == pdTRUE)
      {
        mqtt.loop();
        char json[32];
        snprintf(json, sizeof(json), "{\"strongest\":%d}", hallIdx);
        mqtt.publish(config::MQTT_HALL_TOPIC, json);
        xSemaphoreGive(mqttMutex);
      }
      lastPublish = xTaskGetTickCount();
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void handleIdle()
{
  if (button.wasPressed())
  {
    mic.flush();

    messageId++;
    fragmentId = 0;
    recordStartMs = millis();

    digitalWrite(config::RECORDING_LED_PIN, HIGH);
    DEBUG_PRINTF("Recording started (message_id=%u)\n", messageId);

    state = State::RECORDING;
  }
}

void handleRecording(unsigned long now)
{
  if (button.wasPressed() || (now - recordStartMs >= config::RECORDING_DURATION_MS))
  {
    flushCurrentSlot();
    DEBUG_PRINTLN("Recording stopped.");
    state = State::END;
    return;
  }

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

  // Read I2S and convert 32→16 bit directly into pool slot (after 4-byte header)
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

void handleEnd()
{
  // Send sentinel fragment
  AudioMessage* slot;
  if (xQueueReceive(freePool, &slot, 0) == pdPASS)
  {
    writeHeader(slot->data, messageId, config::FRAGMENT_SENTINEL);
    slot->length = 4;
    xQueueSend(audioQueue, &slot, 0);
  }

  DEBUG_PRINTF("Sentinel queued. Session %u complete: %u fragments.\n", messageId, fragmentId);
  digitalWrite(config::RECORDING_LED_PIN, LOW);
  state = State::IDLE;
}

void flushCurrentSlot()
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