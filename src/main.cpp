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

// --- FreeRTOS queues ---

QueueHandle_t audioQueue;
QueueHandle_t hallQueue;
SemaphoreHandle_t mqttMutex;

// --- Helpers ---

size_t packFragment(uint8_t* out, uint16_t msgId, uint16_t fragId, const uint8_t* audio, size_t audioLen);
void enqueueFragment(uint16_t msgId, uint16_t fragId, const uint8_t* audio, size_t audioLen);

// --- State handlers ---
void handleIdle();
void handleRecording(unsigned long now);
void handleEnd();

// --- Core 0: MQTT tasks ---

void hallPublishTask(void* param);
void audioPublishTask(void* param);

// --- Core 1: Poll and upload sensor data ---

void setup()
{
  Serial.begin(config::BAUD_RATE);
  DEBUG_PRINTLN("\n=== Bar Firmware ===");

  // Create queues + mutex
  audioQueue = xQueueCreate(config::MQTT_QUEUE_SIZE, sizeof(AudioMessage));
  hallQueue  = xQueueCreate(1, sizeof(int8_t));
  mqttMutex  = xSemaphoreCreateMutex();

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

  // Launch MQTT tasks on Core 0 (hall at higher priority for consistent cadence)
  xTaskCreatePinnedToCore(hallPublishTask,  "hall_pub",  4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(audioPublishTask, "audio_pub", 4096, NULL, 1, NULL, 0);

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

  // Poll hall sensors + actuate LEDs
  if (hall.shouldRead(now))
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

void hallPublishTask(void* param)
{
  int8_t idx;
  while (true)
  {
    xSemaphoreTake(mqttMutex, portMAX_DELAY);
    mqtt.loop();

    if (xQueueReceive(hallQueue, &idx, 0) == pdPASS)
    {
      char json[32];
      snprintf(json, sizeof(json), "{\"strongest\":%d}", idx);
      mqtt.publish(config::MQTT_HALL_TOPIC, json);
    }
    xSemaphoreGive(mqttMutex);

    vTaskDelay(pdMS_TO_TICKS(config::MQTT_HALL_PUBLISH_INTERVAL_MS));
  }
}

void audioPublishTask(void* param)
{
  AudioMessage msg;
  while (true)
  {
    if (xQueueReceive(audioQueue, &msg, portMAX_DELAY) == pdPASS)
    {
      do {
        xSemaphoreTake(mqttMutex, portMAX_DELAY);
        mqtt.publish(config::MQTT_AUDIO_TOPIC, msg.data, msg.length);
        xSemaphoreGive(mqttMutex);
      } while (xQueueReceive(audioQueue, &msg, 0) == pdPASS);
    }
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

  size_t bytesRead = mic.getNumBytes16();
  if (bytesRead == 0)
  {
    return;
  }

  const uint8_t* audioData = mic.getSamples16Buffer();

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
  out[0] = msgId & 0xFF;
  out[1] = (msgId >> 8) & 0xFF;
  out[2] = fragId & 0xFF;
  out[3] = (fragId >> 8) & 0xFF;
  if (audioLen > 0)
  {
    memcpy(out + 4, audio, audioLen);
  }
  return 4 + audioLen;
}

void enqueueFragment(uint16_t msgId, uint16_t fragId,
                     const uint8_t* audio, size_t audioLen)
{
  AudioMessage msg;
  msg.length = packFragment(msg.data, msgId, fragId, audio, audioLen);

  if (xQueueSend(audioQueue, &msg, 0) != pdPASS)
  {
    DEBUG_PRINTLN("Queue full, fragment dropped");
  }
}