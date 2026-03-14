#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

// BLE central (client) that scans for a named peripheral, subscribes to its
// TX characteristic notifications, and forwards payloads via a user callback.
//
// Usage:
//   BleClient client("glove", SERVICE_UUID, CHAR_UUID_TX, myCallback);
//   client.begin();          // call once in setup()
//   client.loop();           // call every loop() iteration
class BleClient
{
public:
  using NotifyCallback = void(*)(const uint8_t* data, size_t length);

  BleClient(const char* deviceName,
            const char* serviceUuid,
            const char* charUuid,
            NotifyCallback cb);

  void begin();                           // init NimBLE + start scan
  void loop();                            // handle connect / reconnect
  bool isConnected() const { return _connected; }

private:
  friend class BleClientScanCallbacks;

  void _onDeviceFound(NimBLEAdvertisedDevice* dev);
  bool _connectAndSubscribe();
  void _startScan();

  const char*             _deviceName;
  const char*             _serviceUuid;
  const char*             _charUuid;
  NotifyCallback          _onNotify;

  NimBLEClient*           _pClient   = nullptr;
  NimBLEAdvertisedDevice* _pDevice   = nullptr;
  volatile bool           _doConnect = false;
  volatile bool           _connected = false;
};
