#include <Arduino.h>
#include <driver/i2s.h>
#include <NimBLEDevice.h>
#include "../config.h"
#include "../sensors/button_sensor.h"

// ── Buffers ─────────────────────────────────────────────────────────────────
// I2S always gives 32-bit words; this is the unavoidable intermediate.
static int32_t raw32[config::mic::SAMPLE_BATCH_SIZE];

// All recorded fragments are held here before transmitting.
// 100 × 504 bytes ≈ 49 KB — well within ESP32 SRAM.
constexpr size_t MAX_FRAGS = 100;
static uint8_t  recordBuf[MAX_FRAGS][4 + config::audio::CHUNK_SIZE];
static size_t   fragBytes[MAX_FRAGS];   // actual total bytes (hdr + audio) per frag
static size_t   totalFrags = 0;

// Reused only for the 4-byte sentinel packet
static uint8_t txBuf[4];

// ── BLE state ────────────────────────────────────────────────────────────────
static NimBLECharacteristic* txChar    = nullptr;
static bool                  connected = false;

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
enum class State { IDLE, RECORDING, TRANSMITTING };
static State         state    = State::IDLE;
static unsigned long recStart = 0;
static uint16_t      msgId    = 0;
static uint16_t      fragId   = 0;
static ButtonSensor  button;

// Transmit pacing — 62 ms per packet ≈ 8 KB/s (half the capture rate)
constexpr unsigned long TX_INTERVAL_MS = 62;
static size_t        txIdx   = 0;
static unsigned long lastTxMs = 0;

// ── Helpers ──────────────────────────────────────────────────────────────────
static void flushDMA()
{
  size_t dummy;
  while (i2s_read(I2S_NUM_0, raw32, sizeof(raw32), &dummy, 0) == ESP_OK && dummy > 0);
}

static void sendRaw(const uint8_t* buf, size_t len)
{
  if (connected) { txChar->setValue(buf, len); txChar->notify(); }
}

static void writeHeader(uint8_t* dst, uint16_t msg, uint16_t frag)
{
  dst[0] = msg  & 0xFF;  dst[1] = msg  >> 8;
  dst[2] = frag & 0xFF;  dst[3] = frag >> 8;
}

// ── Arduino entry points ─────────────────────────────────────────────────────
void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("=== test_record_and_send ===");

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

  DEBUG_PRINTLN("Ready. Press button to record.");
}

void loop()
{
  unsigned long now = millis();
  if (button.shouldRead(now)) button.read();

  switch (state)
  {
  // ── IDLE ──────────────────────────────────────────────────────────────────
  case State::IDLE:
    if (button.wasPressed())
    {
      flushDMA();
      msgId++;  fragId = 0;  totalFrags = 0;
      recStart = millis();
      digitalWrite(config::button::LED_PIN, HIGH);
      DEBUG_PRINTF("Recording started (msg=%u)\n", msgId);
      state = State::RECORDING;
    }
    break;

  // ── RECORDING: capture into RAM, no BLE yet ───────────────────────────────
  case State::RECORDING:
    if (button.wasPressed() || (now - recStart >= (unsigned long)config::button::DURATION_MS))
    {
      DEBUG_PRINTF("Recording done. %u fragments buffered. Transmitting at %lu ms/frag...\n",
                   totalFrags, TX_INTERVAL_MS);
      txIdx = 0;  lastTxMs = millis();
      state = State::TRANSMITTING;
    }
    else
    {
      // Block ~31 ms until the next batch of samples is ready
      size_t bytesRead;
      if (i2s_read(I2S_NUM_0, raw32, sizeof(raw32), &bytesRead, 1000) != ESP_OK || bytesRead == 0)
        break;

      if (fragId < MAX_FRAGS)
      {
        size_t n     = bytesRead / sizeof(int32_t);
        uint8_t* dst = recordBuf[fragId];

        writeHeader(dst, msgId, fragId);
        int16_t* out = reinterpret_cast<int16_t*>(dst + 4);
        for (size_t i = 0; i < n; i++) out[i] = (int16_t)(raw32[i] >> 16);

        fragBytes[fragId] = 4 + n * sizeof(int16_t);
        totalFrags = fragId + 1;
        fragId++;
      }
      else
      {
        DEBUG_PRINTLN("Buffer full — stopping early");
        txIdx = 0;  lastTxMs = millis();
        state = State::TRANSMITTING;
      }
    }
    break;

  // ── TRANSMITTING: drain buffer at TX_INTERVAL_MS per packet ──────────────
  case State::TRANSMITTING:
    if (txIdx < totalFrags)
    {
      if (now - lastTxMs >= TX_INTERVAL_MS)
      {
        sendRaw(recordBuf[txIdx], fragBytes[txIdx]);
        DEBUG_PRINTF("TX frag=%u len=%u\n", txIdx, (unsigned)(fragBytes[txIdx] - 4));
        txIdx++;
        lastTxMs = now;
      }
    }
    else
    {
      // All data sent — transmit sentinel then go idle
      writeHeader(txBuf, msgId, config::audio::FRAGMENT_SENTINEL);
      sendRaw(txBuf, 4);
      DEBUG_PRINTF("Sentinel sent. %u fragments transmitted.\n", totalFrags);
      digitalWrite(config::button::LED_PIN, LOW);
      state = State::IDLE;
    }
    break;
  }
}
