/*
 * Config.h
 * Nozomi RF Selector Controller - ESP32-C3-WROOM-02
 *
 * All project constants, pinout and build options live here.
 * Adjust the pins to match your actual wiring.
 */
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// FIRMWARE INFO
// ---------------------------------------------------------------------------
#define FW_VERSION        "1.0.0"
#define FW_VENDOR         "Nozomi"
#define FW_MODEL          "RF-SEL-ESP32C3"
#define DEFAULT_DEVICE_NAME "NozomiRFSel"

// ---------------------------------------------------------------------------
// BUILD OPTIONS
// ---------------------------------------------------------------------------
// 1 = use a real external I2C EEPROM (e.g. 24LC256/24C02) for the selector
//     state (few writes, no flash wear).
// 0 = fallback: emulate EEPROM via NVS (Preferences library), useful if no
//     external chip is mounted.
#define USE_EXTERNAL_EEPROM   1

// I2C address of the external EEPROM (24Cxx typically 0x50..0x57)
#define EXT_EEPROM_I2C_ADDR   0x50

// Enable/disable modules (to save RAM/flash if not needed)
#define ENABLE_BLE            1
#define ENABLE_WEBSERVER      1
#define ENABLE_CLI_TTL        1
#define ENABLE_CLI_USB        1
#define ENABLE_CLI_BLE        1

// ---------------------------------------------------------------------------
// PHYSICAL PINOUT (ESP32-C3-WROOM-02) - ADJUST TO YOUR REAL WIRING
// ---------------------------------------------------------------------------
#define PIN_BTN_NEXT      4     // "next" button, to GND, uses INPUT_PULLUP
#define PIN_BTN_ENTER     5     // "enter" button, to GND, uses INPUT_PULLUP

#define PIN_I2C_SDA       8     // OLED + external EEPROM share the I2C bus
#define PIN_I2C_SCL       9

#define PIN_UART_TX       21    // External TTL serial (Serial1)
#define PIN_UART_RX       20

// RF selector: 3 select bits (S2,S1,S0) + 1 tri-state/enable bit (S3)
// Truth table:
//   TRI-STATE   -> S3=1, S2:S0 = 0
//   SELECT #n   -> S3=0, S2:S0 = n   (n = 1..7)
#define PIN_RF_S0         2
#define PIN_RF_S1         3
#define PIN_RF_S2         6
#define PIN_RF_S3_TRISTATE 7

// ---------------------------------------------------------------------------
// OLED DISPLAY (SSD1306 I2C 128x64)
// ---------------------------------------------------------------------------
#define OLED_I2C_ADDR     0x3C
#define OLED_WIDTH        128
#define OLED_HEIGHT       64

#define DISPLAY_TIMEOUT_MS   30000UL   // turn display off after 30s of inactivity
#define LOGO_SHOW_MS         2000UL    // splash logo duration at boot

// ---------------------------------------------------------------------------
// FILE SYSTEM / JSON CONFIG
// ---------------------------------------------------------------------------
#define CONFIG_JSON_PATH   "/config.json"
#define NUM_CHANNELS       8       // rf#0..rf#7 (antenna ranges), 1..7 used by the selector menu

// Boot logo bitmap: 1-bit-per-pixel, packed MSB-first, row-padded to a
// whole byte (standard Adafruit_GFX drawBitmap format), exactly
// OLED_WIDTH x OLED_HEIGHT pixels, stored as a raw file in SPIFFS. If the
// file is missing, DisplayUI falls back to a text-only splash screen.
#define LOGO_BITMAP_PATH   "/logo.bin"

// ---------------------------------------------------------------------------
// DEBOUNCE
// ---------------------------------------------------------------------------
#define BTN_DEBOUNCE_MS    35

// Grace period between the write ACK on the BLE "Enable RF"=0 characteristic
// and the actual RF stage shutdown, so the BLE client has time to receive
// the response before the disconnection.
#define RF_DISABLE_GRACE_MS  400UL
