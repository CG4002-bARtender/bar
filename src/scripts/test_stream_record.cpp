#include <Arduino.h>
#include <driver/i2s.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "../config.h"

#include "../sensors/mic_sensor.h"
#include "../sensors/button_sensor.h"
#include "../comms/mqtt_client.h"
#include "../actuators/mic_led.h"

static ButtonSensor button;
static MicSensor    mic;
static MicLED       mic_led;

static MqttClient mqtt_client(
  config::mqtt::BROKER,
  config::mqtt::PORT,
  config::mqtt::CLIENT_ID,
  config::mqtt::PUBLISH_INTERVAL_MS,
  config::mqtt::USERNAME,
  config::mqtt::PASSWORD
);

// ── State machine ─────────────────────────────────────────────────────────────
enum class State { IDLE, RECORDING, DRAINING, WAITING_ACK, FLASH };
static volatile State state = State::IDLE;

static unsigned long recStart     = 0;
static unsigned long waitAckStart = 0;

// ── Thread-safe shared state ───────────────────────────────────────────────────
struct AudioChunk {
  uint8_t data[config::mqtt::AUDIO_CHUNK_SIZE];
  size_t  len;
};

static AudioChunk    audioPool[config::mqtt::AUDIO_POOL_SIZE];
static QueueHandle_t audioFreeQ;
static QueueHandle_t audioReadyQ;

static std::atomic<int> pendingAck  { -1 };
static volatile bool    captureIdle = true;

// ── MQTT ACK callback (called from mqttTask / Core 0) ─────────────────────────
static void onMqttMessage(const char* /*topic*/, const uint8_t* payload, unsigned int len)
{
  if (len > 0) pendingAck.store(payload[0], std::memory_order_relaxed);
}

// ── Capture task (Core 0) ──────────────────────────────────────────────────────
static void captureTask(void*)
{
  uint8_t chunkIdx = 0xFF;
  size_t  fillPos  = 0;

  for (;;)
  {
    if (state == State::RECORDING)
    {
      captureIdle = false;

      if (chunkIdx == 0xFF)
      {
        if (xQueueReceive(audioFreeQ, &chunkIdx, pdMS_TO_TICKS(10)) != pdTRUE)
        {
          mic.flush();
          continue;
        }
        fillPos = 0;
      }

      mic.read();
      const size_t bytes = mic.getSampleSize() * sizeof(int16_t);
      if (bytes == 0) continue;

      const size_t space = config::mqtt::AUDIO_CHUNK_SIZE - fillPos;
      const size_t copy  = (bytes < space) ? bytes : space;
      memcpy(audioPool[chunkIdx].data + fillPos, mic.getSamples(), copy);
      fillPos += copy;

      if (fillPos >= config::mqtt::AUDIO_CHUNK_SIZE)
      {
        audioPool[chunkIdx].len = fillPos;
        xQueueSend(audioReadyQ, &chunkIdx, portMAX_DELAY);

        const size_t remainder = bytes - copy;
        if (remainder > 0 && xQueueReceive(audioFreeQ, &chunkIdx, pdMS_TO_TICKS(10)) == pdTRUE)
        {
          memcpy(audioPool[chunkIdx].data, mic.getSamples() + copy, remainder);
          fillPos = remainder;
        }
        else
        {
          chunkIdx = 0xFF;
          fillPos  = 0;
        }
      }
    }
    else
    {
      if (chunkIdx != 0xFF && fillPos > 0)
      {
        audioPool[chunkIdx].len = fillPos;
        xQueueSend(audioReadyQ, &chunkIdx, portMAX_DELAY);
        chunkIdx = 0xFF;
        fillPos  = 0;
      }
      captureIdle = true;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

// ── MQTT task (Core 0) ─────────────────────────────────────────────────────────
static void mqttTask(void*)
{
  for (;;)
  {
    mqtt_client.loop();

    uint8_t chunkIdx;
    if (xQueueReceive(audioReadyQ, &chunkIdx, 0) == pdTRUE)
    {
      mqtt_client.publish(config::mqtt::TOPIC_AUDIO,
                          audioPool[chunkIdx].data,
                          audioPool[chunkIdx].len);
      xQueueSend(audioFreeQ, &chunkIdx, portMAX_DELAY);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("=== test_stream_record ===");

  button.setup();
  mic.setup();
  mic_led.setup();

  audioFreeQ  = xQueueCreate(config::mqtt::AUDIO_POOL_SIZE, sizeof(uint8_t));
  audioReadyQ = xQueueCreate(config::mqtt::AUDIO_POOL_SIZE, sizeof(uint8_t));

  for (uint8_t i = 0; i < (uint8_t)config::mqtt::AUDIO_POOL_SIZE; i++)
    xQueueSend(audioFreeQ, &i, 0);

  mqtt_client.connect(config::wifi::SSID, config::wifi::PASSWORD);
  mqtt_client.subscribe(config::mqtt::TOPIC_ACK, onMqttMessage);

  xTaskCreatePinnedToCore(captureTask, "capture", 4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(mqttTask,    "mqtt",    8192, nullptr, 1, nullptr, 0);

  DEBUG_PRINTLN("Ready. Press button to record.");
}

// ── Main loop (Core 1) ─────────────────────────────────────────────────────────
void loop()
{
  unsigned long now = millis();

  if (button.shouldRead(now)) button.read();

  switch (state)
  {
  case State::IDLE:
    if (button.wasPressed())
    {
      recStart = now;
      mic_led.setRecording();
      DEBUG_PRINTLN("Recording started.");
      state = State::RECORDING;
    }
    break;

  case State::RECORDING:
    if (button.wasPressed() || (now - recStart >= (unsigned long)config::button::DURATION_MS))
    {
      mic_led.setIdle();
      state = State::DRAINING;
    }
    break;

  case State::DRAINING:
  {
    const bool queueEmpty    = (uxQueueMessagesWaiting(audioReadyQ) == 0);
    const bool captureIsDone = captureIdle;
    if (queueEmpty && captureIsDone)
    {
      pendingAck.store(-1, std::memory_order_relaxed);
      waitAckStart = now;
      mic_led.setWaitingAck(now);
      state = State::WAITING_ACK;
    }
    break;
  }

  case State::WAITING_ACK:
  {
    const int ack = pendingAck.load(std::memory_order_relaxed);
    if (ack == 0x01)
    {
      DEBUG_PRINTLN("[ACK] received");
      mic_led.setAckFlash(now);
      state = State::FLASH;
    }
    else if (ack == 0x00 || now - waitAckStart >= config::feedback::ACK_TIMEOUT_MS)
    {
      DEBUG_PRINTLN(ack == 0x00 ? "[NACK] received" : "[ACK] timeout — treating as NACK");
      mic_led.setNackFlash(now);
      state = State::FLASH;
    }
    else
    {
      mic_led.update(now);
    }
    break;
  }

  case State::FLASH:
    if (mic_led.update(now)) state = State::IDLE;
    break;
  }
}
