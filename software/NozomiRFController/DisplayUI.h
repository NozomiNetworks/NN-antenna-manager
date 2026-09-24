/*
 * DisplayUI.h
 * Manages the I2C SH1106 OLED, boot logo, 2-button menu (next/enter),
 * status bar (wifi/bt/channel/antenna range) and auto power-off.
 *
 * This module does NOT know about business logic: it just invokes
 * callbacks when the user confirms an action from the menu.
 */
#pragma once
#include <Arduino.h>

typedef void (*ToggleRFCoreCb)();
typedef void (*SelectChannelCb)(uint8_t channel1to7);
typedef void (*TristateCb)();

namespace DisplayUI {
  void begin(ToggleRFCoreCb onToggleRfCore,
             SelectChannelCb onSelectChannel,
             TristateCb onTristate);

  // Call this on every loop() iteration: reads the buttons, handles the
  // timeout, redraws the screen when needed.
  void update();

  // Forces an immediate redraw (e.g. after an external state change, such
  // as a selection made via BLE/Web, so the status bar stays consistent)
  void requestRedraw();

  // true if the display is currently off due to timeout
  bool isSleeping();
}
