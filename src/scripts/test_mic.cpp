#include <driver/i2s.h>
#include <Arduino.h>
#include <config.h>
#include "../sensors/mic_sensor.h"

MicSensor micSensor;

void setup()
{
  DEBUG_INIT();
  DEBUG_PRINTLN("\n\n=== INMP441 Connection Test ===\n");

  DEBUG_PRINTF("Pin config:\n");
  DEBUG_PRINTF("  WS  (LRCLK) -> GPIO %d\n", config::mic::WS_PIN);
  DEBUG_PRINTF("  SCK (BCLK)  -> GPIO %d\n", config::mic::SCK_PIN);
  DEBUG_PRINTF("  SD  (DATA)  -> GPIO %d\n", config::mic::SD_PIN);
  DEBUG_PRINTF("  Sample rate -> %d Hz\n\n", config::mic::SAMPLE_RATE);

  micSensor.setup();
}

void loop()
{
  micSensor.read();

  const int16_t* samples = reinterpret_cast<const int16_t*>(micSensor.getSamples());
  size_t n = micSensor.getSampleSize();

  if (n == 0) {
    DEBUG_PRINTLN("[ERROR] i2s_read failed or returned 0 bytes — check VDD/GND");
    delay(500);
    return;
  }

  int16_t minVal = samples[0], maxVal = samples[0];
  size_t nonZero = 0;
  for (size_t i = 0; i < n; i++) {
    if (samples[i] < minVal) minVal = samples[i];
    if (samples[i] > maxVal) maxVal = samples[i];
    if (samples[i] != 0) nonZero++;
  }

  DEBUG_PRINTF("n=%u  min=%-6d  max=%-6d  nonzero=%u  -> ", n, minVal, maxVal, nonZero);

  if (nonZero == 0) {
    DEBUG_PRINTLN("ALL ZEROS  (SD disconnected? mic unpowered?)");
  } else if ((maxVal - minVal) < 10) {
    DEBUG_PRINTLN("STUCK VALUE  (SCK/WS issue, or DC bias only)");
  } else {
    DEBUG_PRINTLN("OK - make noise to verify it changes");
  }

  delay(200);
}
