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
#include "comms/ble_client.h"
#include "actuators/mic_led.h"

ButtonSensor button;
HallSensor   hall;
MicSensor    mic;

MicLED  mic_led;

// ── State machine ─────────────────────────────────────────────────────────────
enum class State { IDLE, RECORDING, DRAINING, WAITING_ACK, FLASH };
static std::atomic<State> state { State::IDLE };

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
static QueueHandle_t gloveQ;

static std::atomic<int>  pendingAck   { -1 };
static std::atomic<int>  pendingHall  { -1 };  // -1 = no pending update
static std::atomic<bool> captureIdle  { true };

static_assert(config::mqtt::AUDIO_POOL_SIZE <= 254, "AUDIO_POOL_SIZE exceeds chunkIdx sentinel range (max 254)");

// ── Callbacks ─────────────────────────────────────────────────────────
static void onGloveNotify(const uint8_t* data, size_t length)
{
  if (length == 0) return;
  uint8_t gestureId = data[0];
  DEBUG_PRINTF("[GLOVE] gesture %u\n", gestureId);
  xQueueSend(gloveQ, &gestureId, 0);   // non-blocking; drop if queue full
}

void onMqttMessage(const char* topic, const uint8_t* payload, unsigned int len)
{
  if (len > 0) pendingAck.store(payload[0], std::memory_order_relaxed);
}

// ── Communications ─────────────────────────────────────────────────────────

BleClient glove_ble(
  config::ble::GLOVE_DEVICE_NAME,
  config::ble::SERVICE_UUID,
  config::ble::CHAR_UUID_TX,
  onGloveNotify
);

MqttClient mqtt_client(
  config::mqtt::BROKER,
  config::mqtt::PORT,
  config::mqtt::CLIENT_ID,
  config::mqtt::PUBLISH_INTERVAL_MS,
  config::mqtt::USERNAME,
  config::mqtt::PASSWORD
);

// ── Capture task (Core 0) ──────────────────────────────────────────────────────
//
// Continuously reads the I2S microphone and packs samples into AudioChunk pool
// slots while state == RECORDING.  When recording stops, any partial chunk is
// flushed to audioReadyQ before captureIdle is set true.
//
// Producer of audioReadyQ / consumer of audioFreeQ.
static void captureTask(void*);

// ── MQTT task (Core 0) ─────────────────────────────────────────────────────────
//
// Pumps MQTT (receives ACKs, keeps WiFi alive), drains the audio ready queue,
// and publishes any pending hall sensor changes.
//
// Consumer of audioReadyQ / producer of audioFreeQ.
static void mqttTask(void*);

// ── Helper forward declaration ─────────────────────────────────────────────────
void pollHall();

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("==== BAR::MAIN ====");

  // Setup Sensors
  button.setup();
  hall.setup();
  mic.setup();

  // Setup Actuators
  for (size_t i = 0; i < config::hall::SENSOR_PINS_LEN; i++)
  {
    pinMode(config::hall::LED_PINS[i], OUTPUT);
    digitalWrite(config::hall::LED_PINS[i], LOW);
  }

  mic_led.setup();

  // Initialise queues
  audioFreeQ  = xQueueCreate(config::mqtt::AUDIO_POOL_SIZE, sizeof(uint8_t));
  audioReadyQ = xQueueCreate(config::mqtt::AUDIO_POOL_SIZE, sizeof(uint8_t));
  gloveQ      = xQueueCreate(8, sizeof(uint8_t));

  // Populate the free queue with every pool index
  for (uint8_t i = 0; i < (uint8_t)config::mqtt::AUDIO_POOL_SIZE; i++)
    xQueueSend(audioFreeQ, &i, 0);

  // Setup Comms
  mqtt_client.connect(config::wifi::SSID, config::wifi::PASSWORD);
  mqtt_client.subscribe(config::mqtt::TOPIC_ACK, onMqttMessage);

  // glove_ble.begin();

  // Setup Multithreading
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

  // glove_ble.loop();

  switch (state.load())
  {
  case State::IDLE:
    if (button.wasPressed())
    {
      recStart = now;
      mic_led.setRecording();
      DEBUG_PRINTLN("Recording started.");
      state.store(State::RECORDING);
    }
    break;

  case State::RECORDING:
    if (button.wasPressed() || (now - recStart >= (unsigned long)config::button::DURATION_MS))
    {
      mic_led.setIdle();
      state.store(State::DRAINING);
    }
    break;

  case State::DRAINING:
  {
    const bool queueEmpty    = (uxQueueMessagesWaiting(audioReadyQ) == 0);
    const bool captureIsDone = captureIdle.load();
    if (queueEmpty && captureIsDone)
    {
      pendingAck.store(-1, std::memory_order_relaxed);
      waitAckStart = now;
      mic_led.setWaitingAck(now);
      state.store(State::WAITING_ACK);
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
      state.store(State::FLASH);
    }
    else if (ack == 0x00 || now - waitAckStart >= config::feedback::ACK_TIMEOUT_MS)
    {
      DEBUG_PRINTLN(ack == 0x00 ? "[NACK] received" : "[ACK] timeout — treating as NACK");
      mic_led.setNackFlash(now);
      state.store(State::FLASH);
    }
    else
    {
      mic_led.update(now);
    }
    break;
  }

  case State::FLASH:
    if (mic_led.update(now)) state.store(State::IDLE);
    break;
  }
}


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

    // Publish latest hall index if changed
    const int hallVal = pendingHall.exchange(-1, std::memory_order_relaxed);
    if (hallVal >= 0)
    {
      const uint8_t v = (uint8_t)hallVal;
      mqtt_client.publish(config::mqtt::TOPIC_HALL, &v, 1);
    }

    // Drain glove gesture queue
    uint8_t gestureId;
    if (xQueueReceive(gloveQ, &gestureId, 0) == pdTRUE)
    {
      mqtt_client.publish(config::mqtt::TOPIC_GLOVE, &gestureId, 1);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void captureTask(void*)
{
  uint8_t chunkIdx = 0xFF;   // 0xFF = no slot currently held
  size_t  fillPos  = 0;

  for (;;)
  {
    if (state.load() == State::RECORDING)
    {
      captureIdle.store(false);

      // Claim a free slot if we don't have one
      if (chunkIdx == 0xFF)
      {
        if (xQueueReceive(audioFreeQ, &chunkIdx, pdMS_TO_TICKS(10)) != pdTRUE)
        {
          DEBUG_PRINTLN("[WARN] Audio pool exhausted — dropping mic read");
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
          DEBUG_PRINTF("[WARN] Audio pool exhausted — dropping %u overflow bytes\n", (unsigned)remainder);
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
      captureIdle.store(true);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void pollHall()
{
  hall.read();
  hall.print();

  if (hall.prevClosestHall != hall.closestHall)
  {
    if (hall.prevClosestHall >= 0) {
      digitalWrite(config::hall::LED_PINS[hall.prevClosestHall], LOW);
    }

    if (hall.closestHall >= 0) {
      digitalWrite(config::hall::LED_PINS[hall.closestHall], HIGH);
    }

    pendingHall.store((int)hall.closestHall, std::memory_order_relaxed);
  }
}

