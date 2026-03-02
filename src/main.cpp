#include <Arduino.h>
#include <driver/i2s.h>
#include <NimBLEDevice.h>
#include "config.h"
#include "sensors/mic_sensor.h"
#include "sensors/button_sensor.h"
#include "sensors/hall_sensor.h"

// ── Audio ring buffer (SPSC) ──────────────────────────────────────────────────
static uint8_t ringBuf[config::audio::MAX_FRAGS][4 + config::audio::CHUNK_SIZE];
static size_t  ringBytes[config::audio::MAX_FRAGS];

static volatile size_t head = 0;   // written by Core 0 captureTask
static          size_t tail = 0;   // written by Core 1 loop()

// ── BLE ───────────────────────────────────────────────────────────────────────
static NimBLECharacteristic* txChar    = nullptr;
static volatile bool         connected = false;

// -1 = no ACK pending, 0x00 = NACK, 0x01 = ACK
static volatile int pendingAck = -1;

class RxCB : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic* c) override
  {
    std::string val = c->getValue();
    if (!val.empty()) pendingAck = (uint8_t)val[0];
  }
};

class ServerCB : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* d) override
  {
    connected = true;
    DEBUG_PRINTLN("[BLE] connected");
    s->updateConnParams(d->conn_handle,
      config::ble::CONN_MIN_INTERVAL, config::ble::CONN_MAX_INTERVAL,
      config::ble::CONN_LATENCY,      config::ble::CONN_TIMEOUT);
  }
  void onDisconnect(NimBLEServer*) override
  {
    connected = false;
    DEBUG_PRINTLN("[BLE] disconnected");
    NimBLEDevice::startAdvertising();
  }
};

// ── State machine ─────────────────────────────────────────────────────────────
enum class State { IDLE, RECORDING, DRAINING, WAITING_ACK, ACK_FLASH, NACK_FLASH };
static volatile State state = State::IDLE;

// Transmission states
static uint16_t      msgId    = 0;
static unsigned long recStart = 0;
static unsigned long lastTxMs = 0;

// LED feedback timing
static unsigned long ledToggleMs  = 0;   // last blink toggle timestamp
static unsigned long flashStart   = 0;   // ACK_FLASH start timestamp
static unsigned long waitAckStart = 0;   // WAITING_ACK entry timestamp
static int           nackCount    = 0;   // NACK half-period counter

// ── Sensors ───────────────────────────────────────────────────────────────────
static MicSensor    mic;  
static ButtonSensor button;
static HallSensor   hall;
static int          activeLedIndex = -1;

// ── Helpers ───────────────────────────────────────────────────────────────────
static void writeHeader(uint8_t* dst, uint16_t msg, uint16_t frag)
{
  dst[0] = msg  & 0xFF;  dst[1] = msg  >> 8;
  dst[2] = frag & 0xFF;  dst[3] = frag >> 8;
}

static void sendRaw(const uint8_t* buf, size_t len)
{
  if (connected) { txChar->setValue(buf, len); txChar->notify(); }
}

static void allLedsOff()
{
  digitalWrite(config::button::GREEN_LED_PIN, LOW);
  digitalWrite(config::button::RED_LED_PIN,   LOW);
}

static void enterAckFlash(unsigned long now)
{
  allLedsOff();
  flashStart  = now;
  ledToggleMs = now;
  state = State::ACK_FLASH;
}

static void enterNackFlash(unsigned long now)
{
  allLedsOff();
  nackCount   = 0;
  ledToggleMs = now;
  state = State::NACK_FLASH;
}

// ── Capture task (Core 0) ─────────────────────────────────────────────────────
static void captureTask(void*)
{
  static int32_t raw32[config::mic::SAMPLE_BATCH_SIZE];

  for (;;)
  {
    while (state != State::RECORDING) vTaskDelay(pdMS_TO_TICKS(1));

    // Flush stale DMA samples accumulated while idle
    {
      size_t dummy;
      while (i2s_read(I2S_NUM_0, raw32, sizeof(raw32), &dummy, 0) == ESP_OK && dummy > 0);
    }

    while (state == State::RECORDING)
    {
      if (head >= config::audio::MAX_FRAGS)
      {
        DEBUG_PRINTLN("[CAP] Buffer full — stopping early");
        state = State::DRAINING;
        break;
      }

      size_t bytesRead;
      if (i2s_read(I2S_NUM_0, raw32, sizeof(raw32), &bytesRead, 1000) != ESP_OK || bytesRead == 0)
        continue;

      uint8_t* dst = ringBuf[head];
      writeHeader(dst, msgId, (uint16_t)head);

      size_t n = bytesRead / sizeof(int32_t);
      int16_t* out = reinterpret_cast<int16_t*>(dst + 4);
      for (size_t i = 0; i < n; i++) out[i] = (int16_t)(raw32[i] >> 16);

      ringBytes[head] = 4 + n * sizeof(int16_t);
      __sync_synchronize();
      head++;
    }

    // Wait for full cycle to complete before next recording
    while (state != State::IDLE) vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ── BLE setup ─────────────────────────────────────────────────────────────────
static void setupBLE()
{
  NimBLEDevice::init(config::ble::DEVICE_NAME);
  NimBLEDevice::setMTU(517);
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new ServerCB());
  NimBLEService* svc = srv->createService(config::ble::SERVICE_UUID);
  txChar = svc->createCharacteristic(config::ble::CHAR_UUID_TX, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* rxChar = svc->createCharacteristic(
    config::ble::CHAR_UUID_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCB());
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(config::ble::SERVICE_UUID);
  adv->start();
}

// ── Hall sensor poll + publish ────────────────────────────────────────────────
static void pollHall(unsigned long now)
{
  if (state != State::IDLE || !hall.shouldRead(now)) return;

  hall.read();
  hall.print();
  int closest = hall.getClosestHall();

  if (closest != activeLedIndex)
  {
    if (activeLedIndex >= 0) digitalWrite(config::hall::LED_PINS[activeLedIndex], LOW);
    if (closest      >= 0) digitalWrite(config::hall::LED_PINS[closest],          HIGH);
    activeLedIndex = closest;
  }

  uint8_t hallByte = (closest >= 0) ? (uint8_t)closest : 0xFF;
  sendRaw(&hallByte, 1);
}

// ── Audio TX (RECORDING and DRAINING states) ──────────────────────────────────
static void txAudio(unsigned long now)
{
  if (state != State::RECORDING && state != State::DRAINING) return;

  if (now - lastTxMs >= config::audio::TX_INTERVAL_MS && tail < head)
  {
    sendRaw(ringBuf[tail], ringBytes[tail]);
    DEBUG_PRINTF("[TX] frag=%u len=%u queued=%u\n",
                 (unsigned)tail,
                 (unsigned)(ringBytes[tail] - 4),
                 (unsigned)(head - tail));
    tail++;
    lastTxMs = now;
  }

  if (state == State::DRAINING && tail >= head)
  {
    static uint8_t sentinelBuf[4];
    writeHeader(sentinelBuf, msgId, config::audio::FRAGMENT_SENTINEL);
    sendRaw(sentinelBuf, 4);
    DEBUG_PRINTF("[TX] Sentinel sent. %u total fragments.\n", (unsigned)tail);
    ledToggleMs  = now;
    waitAckStart = now;
    state = State::WAITING_ACK;
  }
}

// ── Arduino entry points ──────────────────────────────────────────────────────
void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("\n=== Bar Firmware ===");

  mic.setup();
  button.setup();
  hall.setup();

  // Status LEDs
  pinMode(config::button::GREEN_LED_PIN, OUTPUT);
  pinMode(config::button::RED_LED_PIN,   OUTPUT);
  allLedsOff();

  // Hall indicator LEDs
  for (size_t i = 0; i < config::hall::SENSOR_PINS_LEN; i++)
  {
    pinMode(config::hall::LED_PINS[i], OUTPUT);
    digitalWrite(config::hall::LED_PINS[i], LOW);
  }

  setupBLE();
  xTaskCreatePinnedToCore(captureTask, "capture", 8192, nullptr, 1, nullptr, 0);

  DEBUG_PRINTLN("Ready. Press button to record.");
}

void loop()
{
  unsigned long now = millis();

  if (button.shouldRead(now)) button.read();
  pollHall(now);

  // ── State machine ─────────────────────────────────────────────────────────
  switch (state)
  {
  case State::IDLE:
    if (button.wasPressed())
    {
      if (activeLedIndex >= 0) digitalWrite(config::hall::LED_PINS[activeLedIndex], LOW);
      activeLedIndex = -1;

      head = 0; tail = 0;
      pendingAck = -1;
      msgId++;
      recStart = now;
      lastTxMs = now;
      digitalWrite(config::button::GREEN_LED_PIN, HIGH);
      DEBUG_PRINTF("Recording started (msg=%u)\n", msgId);
      state = State::RECORDING;
    }
    break;

  case State::RECORDING:
    if (button.wasPressed() || (now - recStart >= (unsigned long)config::button::DURATION_MS))
    {
      DEBUG_PRINTF("Recording stopped. %u frags captured so far.\n", (unsigned)head);
      state = State::DRAINING;
      digitalWrite(config::button::GREEN_LED_PIN, LOW);
    }
    break;

  case State::DRAINING:
    break;  // handled in txAudio()

  case State::WAITING_ACK:
    if (pendingAck == 0x01)
    {
      DEBUG_PRINTLN("[ACK] received");
      enterAckFlash(now);
    }
    else if (pendingAck == 0x00)
    {
      DEBUG_PRINTLN("[NACK] received");
      enterNackFlash(now);
    }
    else if (now - waitAckStart >= config::feedback::ACK_TIMEOUT_MS)
    {
      DEBUG_PRINTLN("[ACK] timeout — treating as NACK");
      enterNackFlash(now);
    }
    else if (now - ledToggleMs >= config::feedback::WAIT_BLINK_MS)
    {
      // Slow green blink while waiting
      digitalWrite(config::button::GREEN_LED_PIN,
                   !digitalRead(config::button::GREEN_LED_PIN));
      ledToggleMs = now;
    }
    break;

  case State::ACK_FLASH:
    if (now - flashStart >= config::feedback::ACK_DURATION_MS)
    {
      allLedsOff();
      state = State::IDLE;
    }
    else if (now - ledToggleMs >= config::feedback::ACK_BLINK_MS)
    {
      digitalWrite(config::button::GREEN_LED_PIN,
                   !digitalRead(config::button::GREEN_LED_PIN));
      ledToggleMs = now;
    }
    break;

  case State::NACK_FLASH:
    if (now - ledToggleMs >= config::feedback::NACK_BLINK_MS)
    {
      nackCount++;
      digitalWrite(config::button::RED_LED_PIN, (nackCount % 2 == 1) ? HIGH : LOW);
      ledToggleMs = now;
      if (nackCount >= config::feedback::NACK_BLINK_COUNT * 2)
      {
        allLedsOff();
        state = State::IDLE;
      }
    }
    break;
  }

  txAudio(now);
}
