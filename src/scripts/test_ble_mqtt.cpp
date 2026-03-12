#include <Arduino.h>
#include "../config.h"
#include "../comms/ble_server.h"
#include "../comms/mqtt_client.h"

// How often each transport fires
constexpr unsigned long BLE_INTERVAL_MS  = 1000;
constexpr unsigned long MQTT_INTERVAL_MS = 1000;

static BleServer  ble;
static MqttClient mqtt(
  config::mqtt::BROKER,
  config::mqtt::PORT,
  config::mqtt::CLIENT_ID,
  config::mqtt::PUBLISH_INTERVAL_MS,
  config::mqtt::USERNAME,
  config::mqtt::PASSWORD
);

static unsigned long lastBleMs  = 0;
static unsigned long lastMqttMs = 0;
static uint32_t      counter    = 0;

void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("\n\n=== BLE + MQTT Coexistence Test ===\n");

  // Start MQTT first (WiFi init), then BLE
  DEBUG_PRINTLN("Connecting MQTT...");
  bool ok = mqtt.connect(config::wifi::SSID, config::wifi::PASSWORD);
  if (!ok) {
    DEBUG_PRINTLN("[FAIL] MQTT connect failed. Halting.");
    while (true) delay(1000);
  }

  DEBUG_PRINTLN("Starting BLE...");
  ble.begin();

  DEBUG_PRINTLN("Ready. Connect a BLE central to see both transports fire.\n");
}

void loop()
{
  unsigned long now = millis();

  mqtt.loop();

  // ── MQTT TX ───────────────────────────────────────────────────────────────
  if (now - lastMqttMs >= MQTT_INTERVAL_MS)
  {
    char payload[48];
    snprintf(payload, sizeof(payload), "{\"n\":%lu}", counter);
    bool ok = mqtt.publish(config::mqtt::TOPIC_TEST, payload);
    DEBUG_PRINTF("[%6lums] MQTT publish #%lu -> %s\n", now, counter, ok ? "OK" : "FAIL");
    lastMqttMs = now;
  }

  // ── BLE TX ────────────────────────────────────────────────────────────────
  if (now - lastBleMs >= BLE_INTERVAL_MS)
  {
    if (ble.isConnected())
    {
      bool ok = ble.send("ping " + std::to_string(counter));
      DEBUG_PRINTF("[%6lums] BLE  send    #%lu -> %s\n", now, counter, ok ? "OK" : "FAIL");
    }
    else
    {
      DEBUG_PRINTF("[%6lums] BLE  (waiting for central)\n", now);
    }
    lastBleMs = now;
    counter++;
  }
}
