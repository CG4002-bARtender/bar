#include <Arduino.h>
#include "../config.h"
#include "../comms/ble_client.h"

static void onGloveData(const uint8_t* data, size_t length)
{
  Serial.printf("[GLOVE] %u bytes:", length);
  for (size_t i = 0; i < length; i++)
    Serial.printf(" %u", data[i]);
  Serial.println();
}

static BleClient glove(
  config::ble::GLOVE_DEVICE_NAME,
  config::ble::SERVICE_UUID,
  config::ble::CHAR_UUID_TX,
  onGloveData
);

void setup()
{
  Serial.begin(115200);
  Serial.println("=== BLE Client Test ===");
  Serial.printf("Free heap: %u\n", esp_get_free_heap_size());
  glove.begin();
  Serial.println("begin() returned.");
}

void loop()
{
  glove.loop();
}
