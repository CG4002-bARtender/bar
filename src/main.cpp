#include <Arduino.h>
#include "config.h"
#include "sensors/mic_sensor.h"
#include "sensors/button_sensor.h"
#include "sensors/hall_sensor.h"
#include "comms/mqtt_client.h"
#include "tasks/audio_task.h"
#include "tasks/hall_task.h"

// --- State machine ---

enum class State { IDLE, RECORDING };

State         state = State::IDLE;
unsigned long recordStartMs = 0;

// --- Peripherals ---

MicSensor     mic;
ButtonSensor  button;
HallSensor    hall;
MqttClient    mqtt(config::MQTT_BROKER, config::MQTT_PORT, config::MQTT_CLIENT_ID,
                   config::MQTT_PUBLISH_INTERVAL_MS, config::MQTT_USERNAME, config::MQTT_PASSWORD);

// --- Core 0 tasks ---

AudioTask     audioTask;
HallTask      hallTask;
SemaphoreHandle_t mqttMutex;

// --- Core 1: Poll sensors, run state machine ---

void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("\n=== Bar Firmware ===");

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

  // Task setup (allocate pools and queues)
  audioTask.setup();
  hallTask.setup();

  // Connect WiFi + MQTT (safe: Core 0 tasks don't exist yet)
  while (!mqtt.connect(config::WIFI_SSID, config::WIFI_PASSWORD))
  {
    DEBUG_PRINTLN("MQTT connect failed. Retrying in 5s...");
    delay(5000);
  }

  // Launch Core 0 publish tasks (hall at higher priority to interleave with audio)
  mqttMutex = xSemaphoreCreateMutex();
  audioTask.startPublisher(mqtt, mqttMutex);
  hallTask.startPublisher(mqtt, mqttMutex);

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

  // Poll hall sensors
  if (hall.shouldRead(now))
  {
    hall.read();
    int closestHall = hall.getClosestHall();

    // Actuate hall LEDs
    for (size_t i = 0; i < config::HALL_SENSOR_PINS_LEN; i++)
    {
      digitalWrite(config::HALL_LED_PINS[i], i == closestHall);
    }

    hallTask.update(closestHall);
  }

  switch (state)
  {
  case State::IDLE:
    if (button.wasPressed())
    {
      audioTask.beginRecording(mic);
      recordStartMs = millis();
      digitalWrite(config::RECORDING_LED_PIN, HIGH);
      DEBUG_PRINTF("Recording started (message_id=%u)\n", audioTask.getMessageId());
      state = State::RECORDING;
    }
    break;

  case State::RECORDING:
    if (button.wasPressed() || (now - recordStartMs >= config::RECORDING_DURATION_MS))
    {
      audioTask.endRecording();
      digitalWrite(config::RECORDING_LED_PIN, LOW);
      DEBUG_PRINTLN("Recording stopped.");
      state = State::IDLE;
    }
    else
    {
      audioTask.record(mic);
    }
    break;
  }
}
