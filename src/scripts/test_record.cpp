#include <Arduino.h>
#include "../config.h"
#include "../sensors/mic_sensor.h"
#include "../sensors/button_sensor.h"
#include "../comms/mqtt_client.h"

// State machine
enum class State { IDLE, RECORDING, SENDING_SENTINEL };

State        state = State::IDLE;
uint16_t     messageId = 0;
uint16_t     fragmentId = 0;
unsigned long recordStartMs = 0;

// Peripherals
MicSensor    mic;
ButtonSensor button;
MqttClient   mqtt(config::MQTT_BROKER, config::MQTT_PORT, config::MQTT_CLIENT_ID,
                  config::MQTT_PUBLISH_INTERVAL_MS, config::MQTT_USERNAME, config::MQTT_PASSWORD);

// Transmit buffer: 4-byte header + 1024-byte audio payload
static uint8_t txBuffer[4 + config::MQTT_AUDIO_CHUNK_SIZE];

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
  if (now - recordStartMs >= config::RECORDING_DURATION_MS)
  {
    Serial.println("Recording duration reached.");
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

    size_t packetLen = packFragment(txBuffer, messageId, fragmentId, audioData + offset, chunkLen);
    mqtt.publish(config::MQTT_AUDIO_TOPIC, txBuffer, packetLen);

    fragmentId++;
    offset += chunkLen;
  }
}

static void handleSentinel()
{
  size_t packetLen = packFragment(txBuffer, messageId, config::FRAGMENT_SENTINEL, nullptr, 0);

  if (mqtt.publish(config::MQTT_AUDIO_TOPIC, txBuffer, packetLen))
  {
    Serial.printf("Sentinel sent. Session %u complete: %u fragments.\n", messageId, fragmentId);
    digitalWrite(config::RECORDING_LED_PIN, LOW);
    state = State::IDLE;
  }
}

void setup()
{
  Serial.begin(config::BAUD_RATE);
  Serial.println("\n=== Record & Send Test ===");

  pinMode(config::RECORDING_LED_PIN, OUTPUT);
  digitalWrite(config::RECORDING_LED_PIN, LOW);

  mic.setup();
  button.setup();

  while (!mqtt.connect(config::WIFI_SSID, config::WIFI_PASSWORD))
  {
    Serial.println("MQTT connect failed. Retrying in 5s...");
    delay(5000);
  }

  Serial.println("Ready. Press button to start recording.");
}

void loop()
{
  mqtt.loop();

  unsigned long now = millis();

  if (button.shouldRead(now))
  {
    button.read();
  }

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
}
