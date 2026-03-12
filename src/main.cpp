#include <Arduino.h>
#include <driver/i2s.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "config.h"

#include "sensors/mic_sensor.h"
#include "sensors/button_sensor.h"
#include "sensors/hall_sensor.h"
#include "comms/mqtt_client.h"
#include "actuators/hall_led.h"
#include "actuators/mic_led.h"

ButtonSensor button;
HallSensor   hall;
MicSensor    mic;

MqttClient mqtt_client(
  config::mqtt::BROKER,
  config::mqtt::PORT,
  config::mqtt::CLIENT_ID,
  config::mqtt::PUBLISH_INTERVAL_MS,
  config::mqtt::USERNAME,
  config::mqtt::PASSWORD
);

HallLED hall_led;
MicLED  mic_led;

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
static QueueHandle_t hallQ;

static std::atomic<int>  pendingAck   { -1 };
static volatile bool     captureIdle  = true;

// ── MQTT ACK callback (called from mqttTask / Core 0) ─────────────────────────
void onMqttMessage(const char* /*topic*/, const uint8_t* payload, unsigned int len)
{
  if (len > 0) pendingAck.store(payload[0], std::memory_order_relaxed);
}

// ── Capture task (Core 0) ──────────────────────────────────────────────────────
//
// Continuously reads the I2S microphone and packs samples into AudioChunk pool
// slots while state == RECORDING.  When recording stops, any partial chunk is
// flushed to audioReadyQ before captureIdle is set true.
//
// Producer of audioReadyQ / consumer of audioFreeQ.
static void captureTask(void*)
{
  uint8_t chunkIdx = 0xFF;   // 0xFF = no slot currently held
  size_t  fillPos  = 0;

  for (;;)
  {
    if (state == State::RECORDING)
    {
      captureIdle = false;

      // Claim a free slot if we don't have one
      if (chunkIdx == 0xFF)
      {
        if (xQueueReceive(audioFreeQ, &chunkIdx, pdMS_TO_TICKS(10)) != pdTRUE)
        {
          mic.flush();   // pool exhausted — discard this read
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
        // Chunk is full — push it and carry any overflow into the next slot
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
      // Not recording — push any partial chunk then go idle
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
//
// Pumps MQTT (receives ACKs, keeps WiFi alive), drains the audio ready queue,
// and publishes any pending hall sensor changes.
//
// Consumer of audioReadyQ / producer of audioFreeQ.
static void mqttTask(void*)
{
  for (;;)
  {
    mqtt_client.loop();   // pump MQTT — delivers ACKs via onMqttMessage

    // Drain audio queue
    uint8_t chunkIdx;
    if (xQueueReceive(audioReadyQ, &chunkIdx, 0) == pdTRUE)
    {
      mqtt_client.publish(config::mqtt::TOPIC_AUDIO,
                          audioPool[chunkIdx].data,
                          audioPool[chunkIdx].len);
      xQueueSend(audioFreeQ, &chunkIdx, portMAX_DELAY);   // return slot to pool
    }

    // Drain hall queue
    int8_t hallVal;
    if (xQueueReceive(hallQ, &hallVal, 0) == pdTRUE)
    {
      mqtt_client.publish(config::mqtt::TOPIC_HALL, (const uint8_t*)&hallVal, 1);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ── Helper forward declaration ─────────────────────────────────────────────────
void pollHall();

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("==== BAR::MAIN ====");

  button.setup();
  hall.setup();
  mic.setup();

  hall_led.setup();
  mic_led.setup();

  // Initialise queues
  audioFreeQ  = xQueueCreate(config::mqtt::AUDIO_POOL_SIZE, sizeof(uint8_t));
  audioReadyQ = xQueueCreate(config::mqtt::AUDIO_POOL_SIZE, sizeof(uint8_t));
  hallQ       = xQueueCreate(8, sizeof(int8_t));

  // Populate the free queue with every pool index
  for (uint8_t i = 0; i < (uint8_t)config::mqtt::AUDIO_POOL_SIZE; i++)
    xQueueSend(audioFreeQ, &i, 0);

  mqtt_client.connect(config::wifi::SSID, config::wifi::PASSWORD);
  mqtt_client.subscribe(config::mqtt::TOPIC_ACK, onMqttMessage);

  // Core 0: capture (priority 2 — preempts MQTT so I2S DMA is never starved)
  //         mqtt    (priority 1)
  xTaskCreatePinnedToCore(captureTask, "capture", 4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(mqttTask,    "mqtt",    8192, nullptr, 1, nullptr, 0);

  DEBUG_PRINTLN("Ready. Press button to record.");
}

// ── Main loop (Core 1) ─────────────────────────────────────────────────────────
void loop()
{
  unsigned long now = millis();

  if (button.shouldRead(now)) button.read();
  if (hall.shouldRead(now))   pollHall();

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

void pollHall()
{
  hall.read();
  hall.print();

  if (hall.prevClosestHall != hall.closestHall)
  {
    hall_led.offLED(hall.prevClosestHall);
    hall_led.onLED(hall.closestHall);

    int8_t hallVal = (int8_t)hall.closestHall;
    xQueueSend(hallQ, &hallVal, 0);
  }
}
