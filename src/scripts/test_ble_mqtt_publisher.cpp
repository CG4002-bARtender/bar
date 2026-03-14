#include <Arduino.h>
#include "../config.h"
#include "../comms/ble_server.h"

static BleServer      ble;
static unsigned long  lastBleMs = 0;
static uint32_t       counter   = 0;

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
      bool ok = ble.send("ping " + std::to_string(counter));
      DEBUG_PRINTF("[%6lums] BLE send #%lu -> %s\n", now, counter, ok ? "OK" : "FAIL");
    }
    else
    {
      DEBUG_PRINTF("[%6lums] waiting for central\n", now);
    }
    lastBleMs = now;
    counter++;
  }
}
