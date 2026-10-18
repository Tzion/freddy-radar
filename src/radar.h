#pragma once
#include <Arduino.h>
#include <MyLD2410.h>

// One LD2410C. Its serial port must be started before begin().
struct Radar {
  static const uint32_t TIMEOUT_MS = 1000; // no frame for this long = offline

  int outPin;
  MyLD2410 dev;

  Radar(Stream &uart, int outPin) : outPin(outPin), dev(uart) {}

  void begin() {
    pinMode(outPin, INPUT_PULLDOWN); // reads 0 if no radar is wired
    if (dev.begin()) dev.enhancedMode(true); // per-gate energies
  }

  void poll() {
    while (dev.check() != MyLD2410::FAIL) {}
  }

  bool online() {
    unsigned long ts = dev.getTimestamp();
    return ts && millis() - ts < TIMEOUT_MS;
  }

  bool out() const { return digitalRead(outPin); }
};
