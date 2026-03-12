#pragma once

#include "../config.h"

class MicLED
{
public:
  void setup();

  void setRecording();                    // solid green
  void setWaitingAck(unsigned long now);  // slow green blink
  void setAckFlash(unsigned long now);    // fast green blink (timed)
  void setNackFlash(unsigned long now);   // red blink (timed)
  void setIdle();                         // all off

  // Call every loop() in ACK_FLASH / NACK_FLASH / WAITING_ACK states.
  // Returns true when a timed flash sequence (ACK/NACK) is done.
  bool update(unsigned long now);

private:
  enum class LedState { IDLE, RECORDING, WAITING_ACK, ACK_FLASH, NACK_FLASH };
  LedState     _state       = LedState::IDLE;
  unsigned long _toggleMs   = 0;
  unsigned long _flashStart = 0;
  int           _nackCount  = 0;

  void _allOff();
};
