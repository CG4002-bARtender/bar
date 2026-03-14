#include <Arduino.h>
#include <NimBLEDevice.h>
#include "../config.h"
#include "../comms/mqtt_client.h"

static MqttClient mqtt(
  config::mqtt::BROKER,
  config::mqtt::PORT,
  config::mqtt::CLIENT_ID,
  config::mqtt::PUBLISH_INTERVAL_MS,
  config::mqtt::USERNAME,
  config::mqtt::PASSWORD
);

// ── BLE client state ──────────────────────────────────────────────────────────
static NimBLEClient*           pClient  = nullptr;
static NimBLEAdvertisedDevice* pDevice  = nullptr;
static volatile bool           doConnect  = false;
static volatile bool           connected  = false;

// ── Notification callback: BLE RX → MQTT publish ─────────────────────────────
static void notifyCB(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool)
{
  std::string msg(reinterpret_cast<char*>(data), length);
  DEBUG_PRINTF("[BLE RX] %s\n", msg.c_str());
  mqtt.publish(config::mqtt::TOPIC_TEST, msg.c_str());
}

// ── Scan callbacks: stop scan when target found ───────────────────────────────
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks
{
  void onResult(NimBLEAdvertisedDevice* dev) override
  {
    if (dev->getName() == config::ble::DEVICE_NAME)
    {
      DEBUG_PRINTF("Found '%s' [%s]\n", dev->getName().c_str(), dev->getAddress().toString().c_str());
      NimBLEDevice::getScan()->stop();
      pDevice   = dev;
      doConnect = true;
    }
  }
};

// ── Connect and subscribe to TX characteristic ────────────────────────────────
static bool connectToServer()
{
  pClient = NimBLEDevice::createClient();

  if (!pClient->connect(pDevice))
  {
    DEBUG_PRINTLN("TCP connect failed");
    NimBLEDevice::deleteClient(pClient);
    pClient = nullptr;
    return false;
  }
  DEBUG_PRINTF("Connected to %s\n", pClient->getPeerAddress().toString().c_str());

  NimBLERemoteService* pSvc = pClient->getService(config::ble::SERVICE_UUID);
  if (!pSvc)
  {
    DEBUG_PRINTLN("Service not found");
    pClient->disconnect();
    return false;
  }

  NimBLERemoteCharacteristic* pChar = pSvc->getCharacteristic(config::ble::CHAR_UUID_TX);
  if (!pChar || !pChar->canNotify())
  {
    DEBUG_PRINTLN("TX characteristic not found / not notifiable");
    pClient->disconnect();
    return false;
  }

  pChar->subscribe(true, notifyCB);
  DEBUG_PRINTLN("Subscribed to BLE notifications.");
  return true;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("=== BLE Receiver + MQTT Publisher ===");

  mqtt.connect(config::wifi::SSID, config::wifi::PASSWORD);

  NimBLEDevice::init("");
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), false);
  pScan->setActiveScan(true);
  pScan->start(0, false);   // scan indefinitely

  DEBUG_PRINTF("Scanning for BLE device '%s'...\n", config::ble::DEVICE_NAME);
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop()
{
  mqtt.loop();

  if (doConnect)
  {
    doConnect = false;
    if (connectToServer())
    {
      connected = true;
    }
    else
    {
      DEBUG_PRINTLN("Connection failed. Restarting scan...");
      NimBLEDevice::getScan()->start(0, false);
    }
  }

  if (connected && pClient && !pClient->isConnected())
  {
    connected = false;
    DEBUG_PRINTLN("Disconnected. Restarting scan...");
    NimBLEDevice::deleteClient(pClient);
    pClient = nullptr;
    NimBLEDevice::getScan()->start(0, false);
  }

  delay(10);
}
