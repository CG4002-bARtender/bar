#include "mic_led.h"

void MicLED::_allOff()
{
  digitalWrite(config::button::GREEN_LED_PIN, LOW);
  digitalWrite(config::button::RED_LED_PIN,   LOW);
}

void MicLED::setup()
{
  pinMode(config::button::GREEN_LED_PIN, OUTPUT);
  pinMode(config::button::RED_LED_PIN,   OUTPUT);
  _allOff();
}

void MicLED::setRecording()
{
  _allOff();
  digitalWrite(config::button::GREEN_LED_PIN, HIGH);
  _state = LedState::RECORDING;
}

void MicLED::setWaitingAck(unsigned long now)
{
  _allOff();
  _toggleMs = now;
  _state = LedState::WAITING_ACK;
}

void MicLED::setAckFlash(unsigned long now)
{
  _allOff();
  _flashStart = now;
  _toggleMs   = now;
  _state = LedState::ACK_FLASH;
}

void MicLED::setNackFlash(unsigned long now)
{
  _allOff();
  _nackCount = 0;
  _toggleMs  = now;
  _state = LedState::NACK_FLASH;
}

void MicLED::setIdle()
{
  _allOff();
  _state = LedState::IDLE;
}

bool MicLED::update(unsigned long now)
{
  switch (_state)
  {
  case LedState::WAITING_ACK:
    if (now - _toggleMs >= config::feedback::WAIT_BLINK_MS)
    {
      digitalWrite(config::button::GREEN_LED_PIN,
                   !digitalRead(config::button::GREEN_LED_PIN));
      _toggleMs = now;
    }
    return false;

  case LedState::ACK_FLASH:
    if (now - _flashStart >= config::feedback::ACK_DURATION_MS)
    {
      setIdle();
      return true;
    }
    if (now - _toggleMs >= config::feedback::ACK_BLINK_MS)
    {
      digitalWrite(config::button::GREEN_LED_PIN,
                   !digitalRead(config::button::GREEN_LED_PIN));
      _toggleMs = now;
    }
    return false;

  case LedState::NACK_FLASH:
    if (now - _toggleMs >= config::feedback::NACK_BLINK_MS)
    {
      _nackCount++;
      digitalWrite(config::button::RED_LED_PIN, (_nackCount % 2 == 1) ? HIGH : LOW);
      _toggleMs = now;
      if (_nackCount >= config::feedback::NACK_BLINK_COUNT * 2)
      {
        setIdle();
        return true;
      }
    }
    return false;

  default:
    return false;
  }
}
