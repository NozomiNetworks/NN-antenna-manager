/*
 * BleGatt.h
 * Exposes over BLE (implemented with NimBLE-Arduino, see BleGatt.cpp):
 *  - Standard Device Information Service (name, firmware, vendor, model)
 *  - Custom service with: selected channel (r/w), Hz range of the active
 *    channel (r), boolean RF stage enable (r/w), Hz range of each of the
 *    8 antennas rf#0..rf#7 (r/w)
 *  - A "Nordic UART"-like service for the text CLI over Bluetooth
 *
 * NOTE ON THE NAME: this module is intentionally NOT called "BLEService".
 * NimBLE-Arduino 2.x defines, for backward compatibility with the old
 * Bluedroid-based library, a preprocessor macro
 * "#define BLEService NimBLEService". Since macro substitution is purely
 * textual, naming our own namespace "BLEService" would silently rewrite
 * every "BLEService::" in this codebase into "NimBLEService::", breaking
 * the build in confusing ways. Keep this module's name clear of any of
 * the classic BLE class names (BLEDevice, BLEServer, BLEService,
 * BLECharacteristic, BLEAdvertising, BLE2902, BLEUtils, ...).
 */
#pragma once
#include <Arduino.h>

namespace BleGatt {
  void begin();     // started/stopped together with RFCore
  void stop();
  bool isRunning();

  // Called by main.ino when the selector state changes for a reason
  // external to BLE (physical menu or web), so the characteristic + notify
  // stay consistent.
  void notifySelectorChanged();

  // Text output queue for the BLE CLI (used by CLI.cpp)
  void bleCliPrint(const String &s);
  // true if a line received from the BLE CLI is ready
  bool bleCliAvailable();
  String bleCliReadLine();
}
