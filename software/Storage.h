/*
 * Storage.h
 * Persistence handling:
 *  - "Soft" config (device name, wifi, antenna ranges) -> JSON on SPIFFS
 *  - "Hard" state (current selector channel, RF enable flag) -> real I2C
 *    EEPROM or NVS
 */
#pragma once
#include <Arduino.h>
#include "Config.h"

struct ChannelCfg {
  char     name[24];     // e.g. "ANT-1"
  uint32_t minHz;        // minimum range in Hz
  uint32_t maxHz;        // maximum range in Hz
  bool     known;        // true if the range has been configured by the user
};

struct DeviceConfig {
  char device_name[32];
  char wifi_ssid[64];
  char wifi_pass[64];
  ChannelCfg channels[NUM_CHANNELS]; // index 0..7 = rf#0..rf#7
};

// Current selector: 0 = TRI-STATE, 1..7 = SELECT #n
extern DeviceConfig g_cfg;

namespace Storage {
  // Must be called once in setup(): mounts SPIFFS, initializes I2C
  // EEPROM/NVS and loads config+state. Returns false if something goes
  // wrong (safe defaults are still applied in that case).
  bool begin();

  // JSON config (SPIFFS) ---------------------------------------------------
  bool loadConfig();          // reads /config.json into g_cfg (or creates defaults)
  bool saveConfig();          // writes g_cfg to /config.json
  void resetConfigToDefault();

  // Selector state (real EEPROM or NVS) ------------------------------------
  uint8_t loadSelectorState();          // 0..7 (0 = tristate)
  void    saveSelectorState(uint8_t v); // only writes if it changed (saves wear)

  // RF wifi/bt stage state (real EEPROM or NVS) ----------------------------
  // Default on very first boot is TRUE: wifi/bt are always present unless
  // the user explicitly disables them (e.g. for SDR captures). The state is
  // remembered across reboots.
  bool loadRfEnabled();
  void saveRfEnabled(bool en); // only writes if it changed
}
