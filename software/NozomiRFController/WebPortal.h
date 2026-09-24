/*
 * WebPortal.h
 * Configuration web server. If WiFi credentials are not set, it starts in
 * Access Point mode with a minimal setup form ("classic" ESP32 method:
 * AP + setup web page).
 *
 * Pages:
 *   /            device status (name, selected channel + range, range of
 *                all antennas)
 *   /antennas    form to set min/max Hz for each of the 8 antennas
 *   /channel     channel radio group 1-7 (+tristate) with automatic
 *                suggestion given a requested Hz/MHz/GHz range
 *   /wifi        SSID/password/device name
 *   /api/status  status JSON (for external integrations)
 */
#pragma once
#include <Arduino.h>

namespace WebPortal {
  void begin();      // called once by RFCore when the radio turns on
  void stop();       // stops the webserver when the radio turns off
  void handle();     // call this in loop() while the radio is active
  bool isRunning();
}
