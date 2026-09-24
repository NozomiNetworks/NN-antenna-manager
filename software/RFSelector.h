/*
 * RFSelector.h
 * Applies the 3-bit + tri-state RF selector state to the physical pins.
 *
 * Convention (see Config.h):
 *   value == 0        -> TRI-STATE : S3=1, S2:S0 = 0
 *   value == 1..7     -> SELECT #v : S3=0, S2:S0 = v
 */
#pragma once
#include <Arduino.h>

namespace RFSelector {
  void begin();                 // configures the pins as OUTPUT
  void apply(uint8_t value);    // writes the 4 physical pins (0..7, 0 = tristate)
  uint8_t current();            // last applied value (0..7)
  bool isTristate();            // current() == 0
}
