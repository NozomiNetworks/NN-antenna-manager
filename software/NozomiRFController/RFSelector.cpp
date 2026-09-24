#include "RFSelector.h"
#include "Config.h"

static uint8_t s_current = 0;

void RFSelector::begin() {
  pinMode(PIN_RF_S0, OUTPUT);
  pinMode(PIN_RF_S1, OUTPUT);
  pinMode(PIN_RF_S2, OUTPUT);
  pinMode(PIN_RF_S3_TRISTATE, OUTPUT);
}

void RFSelector::apply(uint8_t value) {
  if (value > 7) value = 0;
  s_current = value;

  if (value == 0) {
    // TRI-STATE: S3=1, S2:S0=0
    digitalWrite(PIN_RF_S3_TRISTATE, HIGH);
    digitalWrite(PIN_RF_S0, LOW);
    digitalWrite(PIN_RF_S1, LOW);
    digitalWrite(PIN_RF_S2, LOW);
  } else {
    // SELECT #value: S3=0, S2:S0 = value (binary)
    digitalWrite(PIN_RF_S3_TRISTATE, LOW);
    digitalWrite(PIN_RF_S0, (value & 0x01) ? HIGH : LOW);
    digitalWrite(PIN_RF_S1, (value & 0x02) ? HIGH : LOW);
    digitalWrite(PIN_RF_S2, (value & 0x04) ? HIGH : LOW);
  }
}

uint8_t RFSelector::current() { return s_current; }
bool RFSelector::isTristate() { return s_current == 0; }
