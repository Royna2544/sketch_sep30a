#include "led.h"

RGBLED::RGBLED(uint8_t r, uint8_t g, uint8_t b)
    : redPin(r), greenPin(g), bluePin(b) {
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  currentEffect = nullptr;
  effects[0].step_count = 6;
  effects[0].step[0] = {255, 0, 0, 100};   // Red for 100ms
  effects[0].step[1] = {125, 125, 0, 100}; // Yellow for 100ms
  effects[0].step[2] = {0, 255, 0, 100};   // Green for 100ms
  effects[0].step[3] = {0, 125, 125, 100}; // Cyan for 100ms
  effects[0].step[4] = {0, 0, 255, 100};   // Blue for 100ms
  effects[0].step[5] = {125, 0, 125, 100}; // Magenta for 100ms
}

void RGBLED::setColor(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(redPin, r);
  analogWrite(greenPin, g);
  analogWrite(bluePin, b);
}

void RGBLED::playEffect(const Effect *effect) {
  for (uint8_t i = 0; i < effect->step_count; ++i) {
    const auto &s = effect->step[i];
    setColor(s.r, s.g, s.b);
    delay(s.duration_ms);
  }
}