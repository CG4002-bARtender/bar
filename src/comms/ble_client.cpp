#include "ble_client.h"
#include "../config.h"

// ── Scan callbacks ─────────────────────────────────────────────────────────────
class BleClientScanCallbacks : public NimBLEAdvertisedDeviceCallbacks
{
  BleClient* _owner;
public:
  explicit BleClientScanCallbacks(BleClient* o) : _owner(o) {}

  void onResult(NimBLEAdvertisedDevice* dev) override
  {
    _owner->_onDeviceFound(dev);
  }
};

// ── Constructor ────────────────────────────────────────────────────────────────
BleClient::BleClient(const char* deviceName,
                     const char* serviceUuid,
                     const char* charUuid,
                     NotifyCallback cb)
  : _deviceName(deviceName),
    _serviceUuid(serviceUuid),
    _charUuid(charUuid),
    _onNotify(cb)
{}

// ── Public API ─────────────────────────────────────────────────────────────────
void BleClient::begin()
{
  NimBLEDevice::init("");
  NimBLEDevice::getScan()->setAdvertisedDeviceCallbacks(
    new BleClientScanCallbacks(this), false);
  _startScan();
}

void BleClient::loop()
{
  if (_doConnect)
  {
    _doConnect = false;
    if (_connectAndSubscribe())
      _connected = true;
    else
    {
      DEBUG_PRINTF("[BLE-C] Connection to '%s' failed. Restarting scan...\n", _deviceName);
      _startScan();
    }
  }

  if (_connected && _pClient && !_pClient->isConnected())
  {
    _connected = false;
    DEBUG_PRINTF("[BLE-C] Disconnected from '%s'. Restarting scan...\n", _deviceName);
    NimBLEDevice::deleteClient(_pClient);
    _pClient = nullptr;
    _startScan();
  }
}

// ── Private helpers ────────────────────────────────────────────────────────────
void BleClient::_onDeviceFound(NimBLEAdvertisedDevice* dev)
{
  if (dev->getName() != _deviceName) return;

  DEBUG_PRINTF("[BLE-C] Found '%s' [%s]\n",
               dev->getName().c_str(),
               dev->getAddress().toString().c_str());
  NimBLEDevice::getScan()->stop();
  _pDevice   = dev;
  _doConnect = true;
}

bool BleClient::_connectAndSubscribe()
{
  _pClient = NimBLEDevice::createClient();

  if (!_pClient->connect(_pDevice))
  {
    DEBUG_PRINTF("[BLE-C] BLE connect to '%s' failed\n", _deviceName);
    NimBLEDevice::deleteClient(_pClient);
    _pClient = nullptr;
    return false;
  }
  DEBUG_PRINTF("[BLE-C] Connected to %s\n", _pClient->getPeerAddress().toString().c_str());

  NimBLERemoteService* pSvc = _pClient->getService(_serviceUuid);
  if (!pSvc)
  {
    DEBUG_PRINTLN("[BLE-C] Service not found");
    _pClient->disconnect();
    return false;
  }

  NimBLERemoteCharacteristic* pChar = pSvc->getCharacteristic(_charUuid);
  if (!pChar || !pChar->canNotify())
  {
    DEBUG_PRINTLN("[BLE-C] TX characteristic not found / not notifiable");
    _pClient->disconnect();
    return false;
  }

  NotifyCallback cb = _onNotify;
  pChar->subscribe(true, [cb](NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool)
  {
    if (cb) cb(data, length);
  });
  DEBUG_PRINTF("[BLE-C] Subscribed to notifications from '%s'\n", _deviceName);
  return true;
}

void BleClient::_startScan()
{
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->start(0, false);   // scan indefinitely until device found
  DEBUG_PRINTF("[BLE-C] Scanning for '%s'...\n", _deviceName);
}
