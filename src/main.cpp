#include <Arduino.h>
#include "config.h"
#include "sensors/mic_sensor.h"
#include "sensors/button_sensor.h"
#include "sensors/hall_sensor.h"
#include "comms/ble_server.h"
#include "tasks/audio_task.h"
#include "tasks/hall_task.h"

// --- State machine ---

enum class State { IDLE, RECORDING };

State         state = State::IDLE;
unsigned long recordStartMs = 0;

// --- Peripherals ---

MicSensor    mic;
ButtonSensor button;
HallSensor   hall;
BleServer    ble;

// --- Core 0 tasks ---

AudioTask         audioTask;
HallTask          hallTask;
SemaphoreHandle_t bleMutex;

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
  pinMode(config::button::LED_PIN, OUTPUT);
  digitalWrite(config::button::LED_PIN, LOW);
  for (size_t i = 0; i < config::hall::SENSOR_PINS_LEN; i++)
  {
    pinMode(config::hall::LED_PINS[i], OUTPUT);
    digitalWrite(config::hall::LED_PINS[i], LOW);
  }

  // Task setup (allocate pools and queues)
  audioTask.setup();
  hallTask.setup();

  // Start BLE advertising
  ble.begin();

  // Launch Core 0 publish tasks (hall at higher priority to interleave with audio)
  bleMutex = xSemaphoreCreateMutex();
  audioTask.startPublisher(ble, bleMutex);
  hallTask.startPublisher(ble, bleMutex);

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
    for (size_t i = 0; i < config::hall::SENSOR_PINS_LEN; i++)
    {
      digitalWrite(config::hall::LED_PINS[i], i == (size_t)closestHall);
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
      digitalWrite(config::button::LED_PIN, HIGH);
      DEBUG_PRINTF("Recording started (message_id=%u)\n", audioTask.getMessageId());
      state = State::RECORDING;
    }
    break;

  case State::RECORDING:
    if (button.wasPressed() || (now - recordStartMs >= (unsigned long)config::button::DURATION_MS))
    {
      audioTask.endRecording();
      digitalWrite(config::button::LED_PIN, LOW);
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
