#include "led.h"

RGBLED::RGBLED(uint8_t r, uint8_t g, uint8_t b)
    : redPin(r), greenPin(g), bluePin(b) {
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  // Rainbow: red, yellow, green, cyan, blue, magenta.
  effects[0].step_count = 6;
  effects[0].step[0] = {255, 0, 0, 100};   // Red for 100ms
  effects[0].step[1] = {125, 125, 0, 100}; // Yellow for 100ms
  effects[0].step[2] = {0, 255, 0, 100};   // Green for 100ms
  effects[0].step[3] = {0, 125, 125, 100}; // Cyan for 100ms
  effects[0].step[4] = {0, 0, 255, 100};   // Blue for 100ms
  effects[0].step[5] = {125, 0, 125, 100}; // Magenta for 100ms

  // Aurora: drift through violet, blue, cyan, and green, then back again.
  effects[1].step_count = 10;
  effects[1].step[0] = {35, 0, 90, 90};
  effects[1].step[1] = {75, 0, 170, 90};
  effects[1].step[2] = {30, 40, 255, 90};
  effects[1].step[3] = {0, 135, 255, 90};
  effects[1].step[4] = {0, 230, 160, 90};
  effects[1].step[5] = {40, 255, 80, 90};
  effects[1].step[6] = {0, 220, 175, 90};
  effects[1].step[7] = {0, 120, 255, 90};
  effects[1].step[8] = {50, 25, 210, 90};
  effects[1].step[9] = {35, 0, 90, 120};

  // Sunset pulse: warm up to gold, pass through pink, and fade to ember.
  effects[2].step_count = 10;
  effects[2].step[0] = {35, 0, 0, 120};
  effects[2].step[1] = {100, 5, 0, 90};
  effects[2].step[2] = {190, 25, 0, 80};
  effects[2].step[3] = {255, 80, 0, 80};
  effects[2].step[4] = {255, 180, 20, 100};
  effects[2].step[5] = {255, 70, 90, 90};
  effects[2].step[6] = {210, 25, 130, 90};
  effects[2].step[7] = {125, 5, 85, 100};
  effects[2].step[8] = {70, 0, 30, 110};
  effects[2].step[9] = {20, 0, 0, 140};

  // Ocean wave: a deep-blue swell with a bright turquoise crest.
  effects[3].step_count = 12;
  effects[3].step[0] = {0, 0, 25, 110};
  effects[3].step[1] = {0, 10, 65, 90};
  effects[3].step[2] = {0, 35, 125, 80};
  effects[3].step[3] = {0, 85, 190, 70};
  effects[3].step[4] = {0, 155, 230, 70};
  effects[3].step[5] = {35, 235, 255, 80};
  effects[3].step[6] = {145, 255, 255, 60};
  effects[3].step[7] = {25, 220, 255, 70};
  effects[3].step[8] = {0, 135, 220, 80};
  effects[3].step[9] = {0, 65, 155, 90};
  effects[3].step[10] = {0, 20, 80, 100};
  effects[3].step[11] = {0, 0, 25, 130};

  // Neon sparkle: quick saturated flashes separated by tiny dark beats.
  effects[4].step_count = 14;
  effects[4].step[0] = {255, 0, 170, 55};
  effects[4].step[1] = {15, 0, 15, 25};
  effects[4].step[2] = {0, 255, 240, 55};
  effects[4].step[3] = {0, 15, 15, 25};
  effects[4].step[4] = {170, 255, 0, 55};
  effects[4].step[5] = {10, 15, 0, 25};
  effects[4].step[6] = {255, 70, 0, 55};
  effects[4].step[7] = {15, 5, 0, 25};
  effects[4].step[8] = {80, 0, 255, 55};
  effects[4].step[9] = {5, 0, 15, 25};
  effects[4].step[10] = {255, 255, 255, 35};
  effects[4].step[11] = {0, 0, 0, 35};
  effects[4].step[12] = {255, 0, 80, 45};
  effects[4].step[13] = {0, 0, 0, 70};
}

void RGBLED::setColor(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(redPin, r);
  analogWrite(greenPin, g);
  analogWrite(bluePin, b);
}

void RGBLED::playEffect(const Effect *effect, bool clearAfter) {
  for (uint8_t i = 0; i < effect->step_count; ++i) {
    const auto &s = effect->step[i];
    setColor(s.r, s.g, s.b);
    // TODO: Use millis() instead of delay() to avoid
    // blocking the main loop.
    delay(s.duration_ms);
  }
  if (clearAfter) {
    setColor(0, 0, 0);
  }
}
