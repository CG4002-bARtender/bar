#pragma once

#include <Arduino.h>
#include <string>
#include <NimBLEDevice.h>

class BleServer
{
public:
  BleServer() = default;

  void begin();
  bool send(const std::string &msg);
  bool sendRaw(const uint8_t *data, size_t len);
  bool isConnected() const { return _connected; }

  bool shouldPublish(unsigned long now)
  {
    if (now - lastPublishMs >= intervalMs)
    {
      lastPublishMs = now;
      return true;
    }
    return false;
  }

private:
  friend class BleServerCallbacks;

  void _onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc);
  void _onDisconnect();

  NimBLECharacteristic *_txChar    = nullptr;
  bool                   _connected = false;

  unsigned long intervalMs;
  unsigned long lastPublishMs;
};
