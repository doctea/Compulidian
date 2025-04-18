#include <Arduino.h>

#include "computer.h"
uint8_t num = 0;

uint8_t randoms[NUM_LEDS] = {0, 0, 0, 0, 0, 0};

void setup() {
  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(leds[i], OUTPUT);
    randoms[i] = random(65535);
  }
  analogWriteRange(256);
}

void loop() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint32_t t = num + (i * randoms[i]) ;
    digitalWrite(leds[i], (t % 24 < 11));
  }
  delay(10);
  num++;
}