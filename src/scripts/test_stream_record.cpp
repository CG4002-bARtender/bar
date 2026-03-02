#include <Arduino.h>
#include <driver/i2s.h>
#include <NimBLEDevice.h>
#include "../config.h"
#include "../sensors/button_sensor.h"

// ── Tuneable ─────────────────────────────────────────────────────────────────
// Lower = faster drain. Must stay above the BLE connection interval (~20 ms).
// At 62 ms the drain rate is half the fill rate; lower values close the gap.
constexpr unsigned long TX_INTERVAL_MS = 32;

// ── Ring buffer ───────────────────────────────────────────────────────────────
// Sized to hold the full 3-second recording so the TX side can never overrun.
constexpr size_t MAX_FRAGS = 100;
static uint8_t ringBuf[MAX_FRAGS][4 + config::audio::CHUNK_SIZE];
static size_t  ringBytes[MAX_FRAGS];  // actual bytes (hdr + audio) per slot

// SPSC indices.
//   head — next slot to write; written only by Core 0 capture task.
//   tail — next slot to read;  written only by Core 1 loop().
static volatile size_t head = 0;
static          size_t tail = 0;

// ── BLE ───────────────────────────────────────────────────────────────────────
static NimBLECharacteristic* txChar    = nullptr;
static volatile bool         connected = false;

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

// ── State ─────────────────────────────────────────────────────────────────────
enum class State { IDLE, RECORDING, DRAINING };
static volatile State state    = State::IDLE;
static          uint16_t msgId = 0;

static unsigned long recStart = 0;
static unsigned long lastTxMs = 0;

static ButtonSensor button;

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

// ── Capture task (Core 0) ─────────────────────────────────────────────────────
// Blocks on i2s_read (~31 ms per batch at 8 kHz / 250 samples).
// Writes each fragment into the ring buffer then increments head.
// The memory barrier ensures the payload is visible on Core 1 before head moves.
static void captureTask(void*)
{
  static int32_t raw32[config::mic::SAMPLE_BATCH_SIZE];

  for (;;)
  {
    // Wait for a recording to begin
    while (state != State::RECORDING) vTaskDelay(pdMS_TO_TICKS(1));

    // Flush any stale DMA samples accumulated while idle
    {
      size_t dummy;
      while (i2s_read(I2S_NUM_0, raw32, sizeof(raw32), &dummy, 0) == ESP_OK && dummy > 0);
    }

    while (state == State::RECORDING)
    {
      if (head >= MAX_FRAGS)
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

      // Ensure payload bytes are globally visible before head advances
      __sync_synchronize();
      head++;
    }

    // Wait for the TX side to finish draining before the next recording
    while (state != State::IDLE) vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ── Arduino entry points ─────────────────────────────────────────────────────
void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("=== test_stream_record ===");

  // I2S mic
  i2s_config_t icfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = config::mic::SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = config::mic::DMA_BUFFER_COUNT,
    .dma_buf_len          = config::mic::DMA_BUFFER_LEN,
  };
  i2s_pin_config_t pcfg = {
    .bck_io_num   = config::mic::SCK_PIN,
    .ws_io_num    = config::mic::WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = config::mic::SD_PIN,
  };
  i2s_driver_install(I2S_NUM_0, &icfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pcfg);

  button.setup();
  pinMode(config::button::LED_PIN, OUTPUT);
  digitalWrite(config::button::LED_PIN, LOW);

  // BLE
  NimBLEDevice::init(config::ble::DEVICE_NAME);
  NimBLEDevice::setMTU(517);
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new ServerCB());
  NimBLEService* svc = srv->createService(config::ble::SERVICE_UUID);
  txChar = svc->createCharacteristic(config::ble::CHAR_UUID_TX, NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(config::ble::SERVICE_UUID);
  adv->start();

  // Capture task pinned to Core 0; loop() runs on Core 1
  xTaskCreatePinnedToCore(captureTask, "capture", 8192, nullptr, 1, nullptr, 0);

  DEBUG_PRINTLN("Ready. Press button to record.");
}

void loop()
{
  unsigned long now = millis();
  if (button.shouldRead(now)) button.read();

  // ── State transitions (button / timeout) ─────────────────────────────────
  switch (state)
  {
  case State::IDLE:
    if (button.wasPressed())
    {
      // Reset ring buffer indices while capture task is still in its IDLE wait
      head = 0;  tail = 0;
      msgId++;
      recStart = now;
      lastTxMs = now;
      digitalWrite(config::button::LED_PIN, HIGH);
      DEBUG_PRINTF("Recording started (msg=%u)\n", msgId);
      state = State::RECORDING;   // releases capture task
    }
    break;

  case State::RECORDING:
    if (button.wasPressed() || (now - recStart >= (unsigned long)config::button::DURATION_MS))
    {
      DEBUG_PRINTF("Recording stopped. %u frags captured so far.\n", (unsigned)head);
      state = State::DRAINING;
      digitalWrite(config::button::LED_PIN, LOW);
    }
    break;

  case State::DRAINING:
    break;  // TX section below handles the rest
  }

  // ── BLE TX: drain ring buffer (active during RECORDING and DRAINING) ──────
  if (state != State::IDLE)
  {
    if (now - lastTxMs >= TX_INTERVAL_MS && tail < head)
    {
      sendRaw(ringBuf[tail], ringBytes[tail]);
      DEBUG_PRINTF("[TX] frag=%u len=%u queued=%u\n",
                   (unsigned)tail,
                   (unsigned)(ringBytes[tail] - 4),
                   (unsigned)(head - tail));
      tail++;
      lastTxMs = now;
    }

    // Once draining is complete and all fragments are sent, send sentinel
    if (state == State::DRAINING && tail >= head)
    {
      static uint8_t sentinelBuf[4];
      writeHeader(sentinelBuf, msgId, config::audio::FRAGMENT_SENTINEL);
      sendRaw(sentinelBuf, 4);
      DEBUG_PRINTF("[TX] Sentinel sent. %u total fragments.\n", (unsigned)tail);
      state = State::IDLE;   // releases capture task back to its idle wait
    }
  }
}
