// test_record.cpp — Key-triggered mic→serial recorder.
//
// Idles until it receives a 1-byte START command (0x01) from the laptop,
// then streams raw PCM audio using the same binary framing as serial_record.
// After sending REC_END it waits for a 1-byte ACK/NACK and drives the LED.
//
// Companion: tools/test_record.py
//
// Protocol (host → device):
//   1 byte: 0x01 = START  (triggers one recording)
//
// Protocol (device → host):
//   [0xAB][0xCD][TYPE:1][LEN:2LE][PAYLOAD:LEN]
//   TYPE 0x01 = AUDIO_CHUNK   (PAYLOAD = raw int16 PCM, little-endian)
//   TYPE 0x02 = REC_END       (LEN = 0, no payload)
//
// Protocol (host → device after REC_END):
//   1 byte: 0x01 = ACK, 0x00 = NACK

#include <Arduino.h>
#include "sensors/mic_sensor.h"
#include "actuators/mic_led.h"
#include "config.h"

// ── Protocol constants ─────────────────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD    = 921600;
static constexpr uint8_t  CMD_START      = 0x01;
static constexpr uint8_t  MAGIC_0        = 0xAB;
static constexpr uint8_t  MAGIC_1        = 0xCD;
static constexpr uint8_t  TYPE_CHUNK     = 0x01;
static constexpr uint8_t  TYPE_REC_END   = 0x02;
static constexpr uint32_t ACK_TIMEOUT_MS = 3000;

static MicSensor mic;
static MicLED    mic_led;

// ── Helpers ────────────────────────────────────────────────────────────────────
static void sendPacket(uint8_t type, const uint8_t* payload, uint16_t len)
{
  uint8_t header[5] = {
    MAGIC_0, MAGIC_1,
    type,
    (uint8_t)(len & 0xFF),
    (uint8_t)(len >> 8)
  };
  Serial.write(header, 5);
  if (payload && len > 0)
    Serial.write(payload, len);
}

// ── Arduino entry points ───────────────────────────────────────────────────────
void setup()
{
  Serial.begin(SERIAL_BAUD);
  mic.setup();
  mic_led.setup();
  mic_led.setIdle();
}

void loop()
{
  // ── Wait for START command ───────────────────────────────────────────────────
  while (Serial.available() == 0)
    delay(1);

  const uint8_t cmd = (uint8_t)Serial.read();
  if (cmd != CMD_START)
    return;   // ignore unexpected bytes

  // ── Record ──────────────────────────────────────────────────────────────────
  mic.flush();   // clear any DMA backlog before starting
  mic_led.setRecording();

  const unsigned long recStart = millis();
  while (millis() - recStart < (unsigned long)config::button::DURATION_MS)
  {
    mic.read();
    const uint16_t bytes = (uint16_t)(mic.getSampleSize() * sizeof(int16_t));
    if (bytes > 0)
      sendPacket(TYPE_CHUNK, mic.getSamples(), bytes);
  }

  sendPacket(TYPE_REC_END, nullptr, 0);
  Serial.flush();

  // ── Wait for ACK ────────────────────────────────────────────────────────────
  const unsigned long ackStart = millis();
  uint8_t ack = 0xFF;   // 0xFF = timeout sentinel
  while (millis() - ackStart < ACK_TIMEOUT_MS)
  {
    if (Serial.available() > 0)
    {
      ack = (uint8_t)Serial.read();
      break;
    }
  }

  // ── LED feedback, then return to idle ───────────────────────────────────────
  const unsigned long now = millis();
  if (ack == 0x01)
    mic_led.setAckFlash(now);
  else
    mic_led.setNackFlash(now);

  while (!mic_led.update(millis()))
    delay(1);

  mic_led.setIdle();
}
