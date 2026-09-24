/*
 * RFCore.h
 * "RF core" = the ESP32's built-in radio stage (WiFi + Bluetooth).
 *
 * WiFi and BLE are ALWAYS present/active by default (the stack starts at
 * boot, restoring the last saved state): they let you always reach the
 * device via app/web portal, without having to go through the local menu
 * or a wired serial connection.
 *
 * The user can however explicitly disable the RF stage (truly turning off
 * WiFi/BT emission) when doing a capture with SDR boards, to prevent the
 * device itself from interfering. This can be done from:
 *   - the local menu (buttons + OLED)
 *   - the CLI (TTL / USB serial, "rf off" command)
 *   - the boolean BLE "Enable RF" characteristic
 * The enabled/disabled state is remembered across reboots.
 */
#pragma once
#include <Arduino.h>

namespace RFCore {
  // Initializes, starting WiFi+BLE if the last saved state was "enabled"
  // (default on the very first boot: enabled = TRUE, "always present").
  void begin();

  void enable();          // turns WiFi+BLE on (also starts WebPortal/BleGatt), persists the state
  void disable();         // turns WiFi+BLE off (also stops WebPortal/BleGatt), persists the state
  void toggle();
  bool isEnabled();

  // Call this when a disable request comes from a BLE context (e.g. the
  // "Enable RF" characteristic written to 0): it does NOT shut things down
  // immediately (that would break the BLE connection mid-write), but
  // schedules it with a short grace delay, executed by update().
  void requestDisable();

  // Call this on every loop() iteration: performs any deferred disable
  // request (see requestDisable()).
  void update();

  // Info for the display status bar
  int8_t wifiRssiPercent();  // 0-100, or -1 if not connected/disabled
  bool   bleActive();
}
