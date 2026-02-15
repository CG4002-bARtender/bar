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

// --- Helpers ---

size_t packFragment(uint8_t* out, uint16_t msgId, uint16_t fragId, const uint8_t* audio, size_t audioLen);
void enqueueFragment(uint16_t msgId, uint16_t fragId, const uint8_t* audio, size_t audioLen);

// --- State handlers ---
void handleIdle();
void handleRecording(unsigned long now);
void handleEnd();

// --- Core 0: MQTT task ---

void mqttPublishTask(void* param);

// --- Core 1: Poll and upload sensor data ---

void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("\n=== Bar Firmware ===");

  // Heap-allocate the audio buffer pool (keeps .bss small)
  audioPool = new AudioMessage[config::MQTT_AUDIO_QUEUE_SIZE];

  // Create queues (audio queues carry pointers, not full structs)
  audioQueue = xQueueCreate(config::MQTT_AUDIO_QUEUE_SIZE, sizeof(AudioMessage*));
  freePool   = xQueueCreate(config::MQTT_AUDIO_QUEUE_SIZE, sizeof(AudioMessage*));
  hallQueue  = xQueueCreate(1, sizeof(int8_t));

  // Seed the free pool with pointers to every slot
  for (size_t i = 0; i < config::MQTT_AUDIO_QUEUE_SIZE; i++)
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

  // Connect WiFi + MQTT (safe: Core 0 task doesn't exist yet)
  while (!mqtt.connect(config::WIFI_SSID, config::WIFI_PASSWORD))
  {
    DEBUG_PRINTLN("MQTT connect failed. Retrying in 5s...");
    delay(5000);
  }

  // Launch MQTT task on Core 0
  xTaskCreatePinnedToCore(mqttPublishTask, "mqtt_pub", 4096, NULL, 1, NULL, 0);

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

void mqttPublishTask(void* param)
{
  AudioMessage* audioMsg;
  int8_t hallIdx;
  TickType_t lastHallPublish = 0;

  while (true)
  {
    mqtt.loop();

    while (xQueueReceive(audioQueue, &audioMsg, 0) == pdPASS)
    {
      mqtt.publish(config::MQTT_AUDIO_TOPIC, audioMsg->data, audioMsg->length);
      xQueueSend(freePool, &audioMsg, 0);  // return slot to pool
    }

    TickType_t now = xTaskGetTickCount();
    if (state == State::IDLE &&
        (now - lastHallPublish) >= pdMS_TO_TICKS(config::MQTT_PUBLISH_INTERVAL_MS) &&
        xQueueReceive(hallQueue, &hallIdx, 0) == pdPASS)
    {
      char json[32];
      snprintf(json, sizeof(json), "{\"strongest\":%d}", hallIdx);
      mqtt.publish(config::MQTT_HALL_TOPIC, json);
      lastHallPublish = now;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
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
    DEBUG_PRINTLN("Recording stopped.");
    state = State::END;
    return;
  }

  mic.read();

  size_t bytesRead = mic.getNumBytes();
  if (bytesRead == 0)
  {
    return;
  }

  const uint8_t* audioData = mic.getSamplesBuffer();

  size_t offset = 0;
  while (offset < bytesRead)
  {
    size_t chunkLen = bytesRead - offset;
    if (chunkLen > config::MQTT_AUDIO_CHUNK_SIZE)
    {
      chunkLen = config::MQTT_AUDIO_CHUNK_SIZE;
    }

    enqueueFragment(messageId, fragmentId, audioData + offset, chunkLen);
    fragmentId++;
    offset += chunkLen;
  }
}

void handleEnd()
{
  enqueueFragment(messageId, config::FRAGMENT_SENTINEL, nullptr, 0);
  DEBUG_PRINTF("Sentinel queued. Session %u complete: %u fragments.\n", messageId, fragmentId);

  digitalWrite(config::RECORDING_LED_PIN, LOW);
  state = State::IDLE;
}


size_t packFragment(uint8_t* out, uint16_t msgId, uint16_t fragId,
                    const uint8_t* audio, size_t audioLen)
{
  // Copy message id header bits
  out[0] = msgId & 0xFF;
  out[1] = (msgId >> 8) & 0xFF;
  
  // Copy frag id header bits
  out[2] = fragId & 0xFF;
  out[3] = (fragId >> 8) & 0xFF;
  
  // Copy audio data bits
  if (audioLen > 0)
  {
    memcpy(out + 4, audio, audioLen);
  }
  return 4 + audioLen;
}

void enqueueFragment(uint16_t msgId, uint16_t fragId,
                     const uint8_t* audio, size_t audioLen)
{
  AudioMessage* slot;
  if (xQueueReceive(freePool, &slot, 0) != pdPASS)
  {
    DEBUG_PRINTLN("Pool exhausted, fragment dropped");
    return;
  }

  slot->length = packFragment(slot->data, msgId, fragId, audio, audioLen);

  if (xQueueSend(audioQueue, &slot, 0) != pdPASS)
  {
    DEBUG_PRINTLN("Queue full, fragment dropped");
    xQueueSend(freePool, &slot, 0);  // return slot on failure
  }
}