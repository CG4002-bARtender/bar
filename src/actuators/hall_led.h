#pragma once

#include "../config.h"

class HallLED
{
public:
  void setup();
  void offLED(int led);
  void onLED(int led);
};
