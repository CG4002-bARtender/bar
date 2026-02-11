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

enum class State { IDLE, RECORDING, SENDING_SENTINEL };

static State         state = State::IDLE;
static uint16_t      messageId = 0;
static uint16_t      fragmentId = 0;
static unsigned long recordStartMs = 0;

// --- Peripherals ---

static MicSensor     mic;
static ButtonSensor  button;
static HallSensor    hall;
static MqttClient    mqtt(config::MQTT_BROKER, config::MQTT_PORT, config::MQTT_CLIENT_ID,
                          config::MQTT_PUBLISH_INTERVAL_MS, config::MQTT_USERNAME, config::MQTT_PASSWORD);

// --- FreeRTOS queues ---

static QueueHandle_t audioQueue;
static QueueHandle_t hallQueue;

// --- Helpers ---

static size_t packFragment(uint8_t* out, uint16_t msgId, uint16_t fragId,
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

static void enqueueFragment(uint16_t msgId, uint16_t fragId,
                            const uint8_t* audio, size_t audioLen)
{
  AudioMessage msg;
  msg.length = packFragment(msg.data, msgId, fragId, audio, audioLen);

  if (xQueueSend(audioQueue, &msg, 0) != pdPASS)
  {
    Serial.println("Queue full, fragment dropped");
  }
}

// --- State handlers ---

static void handleIdle()
{
  if (button.wasPressed())
  {
    messageId++;
    fragmentId = 0;
    recordStartMs = millis();

    digitalWrite(config::RECORDING_LED_PIN, HIGH);
    Serial.printf("Recording started (message_id=%u)\n", messageId);

    state = State::RECORDING;
  }
}

static void handleRecording(unsigned long now)
{
  if (button.wasPressed() || (now - recordStartMs >= config::RECORDING_DURATION_MS))
  {
    Serial.println("Recording stopped.");
    state = State::SENDING_SENTINEL;
    return;
  }

  mic.read();

  size_t bytesRead = mic.getNumBytesRead();
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

static void handleSentinel()
{
  enqueueFragment(messageId, config::FRAGMENT_SENTINEL, nullptr, 0);
  Serial.printf("Sentinel queued. Session %u complete: %u fragments.\n", messageId, fragmentId);

  digitalWrite(config::RECORDING_LED_PIN, LOW);
  state = State::IDLE;
}

// --- Hall LED actuation ---

static void actuateHallLeds()
{
  for (size_t i = 0; i < config::HALL_SENSOR_PINS_LEN; i++)
  {
    if (abs(hall.getOffset(i)) > config::HALL_THRESHOLD)
    {
      digitalWrite(config::HALL_LED_PINS[i], HIGH);
    }
    else
    {
      digitalWrite(config::HALL_LED_PINS[i], LOW);
    }
  }
}

// --- Hall MQTT publish ---

static unsigned long lastHallPublishMs = 0;

static void enqueueStrongestHall(unsigned long now)
{
  if (now - lastHallPublishMs < config::MQTT_HALL_PUBLISH_INTERVAL_MS) return;
  lastHallPublishMs = now;

  int8_t strongestIdx = -1;
  int strongestVal = config::HALL_THRESHOLD;

  for (size_t i = 0; i < config::HALL_SENSOR_PINS_LEN; i++)
  {
    int absOffset = abs(hall.getOffset(i));
    if (absOffset > strongestVal)
    {
      strongestVal = absOffset;
      strongestIdx = i;
    }
  }

  if (strongestIdx < 0) return;

  if (xQueueSend(hallQueue, &strongestIdx, 0) != pdPASS)
  {
    Serial.println("Hall queue full, dropped");
  }
}

// --- Core 0: MQTT task ---

static void mqttTask(void* param)
{
  AudioMessage audioMsg;
  int8_t hallIdx;

  while (true)
  {
    mqtt.loop();

    if (xQueueReceive(audioQueue, &audioMsg, 0) == pdPASS)
    {
      mqtt.publish(config::MQTT_AUDIO_TOPIC, audioMsg.data, audioMsg.length);
    }

    if (xQueueReceive(hallQueue, &hallIdx, 0) == pdPASS)
    {
      char json[32];
      snprintf(json, sizeof(json), "{\"strongest\":%d}", hallIdx);
      mqtt.publish(config::MQTT_HALL_TOPIC, json);
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// --- Arduino entry points (Core 1) ---

void setup()
{
  Serial.begin(config::BAUD_RATE);
  Serial.println("\n=== Bar Firmware ===");

  // Create queues
  audioQueue = xQueueCreate(config::MQTT_QUEUE_SIZE, sizeof(AudioMessage));
  hallQueue  = xQueueCreate(4, sizeof(int8_t));

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
    Serial.println("MQTT connect failed. Retrying in 5s...");
    delay(5000);
  }

  // Launch MQTT task on Core 0
  xTaskCreatePinnedToCore(mqttTask, "mqtt", 4096, NULL, 1, NULL, 0);

  Serial.println("Ready. Press button to start recording.");
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
  case State::SENDING_SENTINEL:
    handleSentinel();
    break;
  }

  // Poll hall sensors + actuate LEDs
  if (hall.shouldRead(now))
  {
    hall.read();
    actuateHallLeds();
    enqueueStrongestHall(now);
  }
}
