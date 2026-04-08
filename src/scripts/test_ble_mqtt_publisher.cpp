#include <Arduino.h>
#include "../config.h"
#include "../comms/ble_server.h"

static BleServer      ble(config::ble::GLOVE_DEVICE_NAME);
static unsigned long  lastBleMs = 0;
static uint8_t        counter   = 0;

void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("=== BLE Publisher ===");
  ble.begin();
  DEBUG_PRINTLN("Advertising. Waiting for central...");
}

void loop()
{
  unsigned long now = millis();

  if (now - lastBleMs >= 1000)
  {
    if (ble.isConnected())
    {
      bool ok = ble.sendRaw(&counter, 1);
      DEBUG_PRINTF("[%6lums] BLE send gesture %u -> %s\n", now, counter, ok ? "OK" : "FAIL");
      counter = (counter + 1) % 5;
    }
    else
    {
      DEBUG_PRINTF("[%6lums] waiting for central\n", now);
    }
    lastBleMs = now;
  }
}
