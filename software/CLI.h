/*
 * CLI.h
 * Text command interpreter shared across:
 *   - Serial1 (external TTL serial)
 *   - Serial  (ESP32-C3 native USB CDC)
 *   - BLE "Nordic UART"-like (see BleGatt module)
 *
 * Main commands (case-insensitive):
 *   help                          shows the command list
 *   status                        prints full status (JSON)
 *   select <1-7>                  sets the RF selector
 *   tristate                      forces the selector into tri-state
 *   rf on|off|toggle              enables/disables the RF stage (wifi+bt)
 *   name <name>                   sets the device name
 *   wifi <ssid> <password>        sets the WiFi credentials
 *   ant <0-7> <minHz> <maxHz>     sets an antenna's range
 *   save                          saves the configuration to SPIFFS
 *   reboot                        reboots the device
 */
#pragma once
#include <Arduino.h>

namespace CLI {
  void begin();
  void update(); // call this on every loop(), reads from all active sources
}
