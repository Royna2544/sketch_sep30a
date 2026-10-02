#pragma once

#include <Arduino.h>

class RGBLED {
public:
  class Effect {
  public:
    struct {
      uint8_t r, g, b;
      uint8_t duration_ms;
    } step[20];
    uint8_t step_count;
  } effects[5];
  RGBLED(uint8_t r, uint8_t g, uint8_t b);
  void setColor(uint8_t r, uint8_t g, uint8_t b);
  void playEffect(const Effect *effect, bool clearAfter = true);

private:
  uint8_t redPin;
  uint8_t greenPin;
  uint8_t bluePin;
};