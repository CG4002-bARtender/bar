#include <Arduino.h>
#include "../config.h"
#include "../comms/ble_server.h"

static BleServer ble;
static uint32_t  counter = 0;

void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("=== BLE Test Script ===");

  ble.begin();
}

void loop()
{
  if (!ble.isConnected()) return;
  
  ble.send("ping " + std::to_string(counter++));
  delay(config::ble::PING_INTERVAL_MS);
}
