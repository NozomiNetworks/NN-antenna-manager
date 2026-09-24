/*
 * NozomiRFController.ino
 * ESP32-C3-WROOM-02 controller for a 3-bit + tri-state RF selector (mixer).
 *
 * User interfaces:
 *   - 2 buttons + I2C OLED -> local menu
 *   - Text CLI over: TTL serial (Serial1), USB (Serial/CDC), BLE (NUS-like)
 *   - BLE GATT: standard + custom characteristics (channel, antenna ranges,
 *     RF enable)
 *   - Web server (WiFi) with status/config pages
 *
 * See Config.h for all pins and build options.
 * See README.md for the required libraries and wiring notes.
 */
#include <Wire.h>
#include "Config.h"
#include "Storage.h"
#include "RFSelector.h"
#include "RFCore.h"
#include "DisplayUI.h"
#include "CLI.h"
#if ENABLE_WEBSERVER
  #include "WebPortal.h"
#endif
#if ENABLE_BLE
  #include "BleGatt.h"
#endif

// ---------------------------------------------------------------------------
// Callbacks invoked by the display menu
// ---------------------------------------------------------------------------
static void onMenuToggleRfCore() {
  RFCore::toggle();
  DisplayUI::requestRedraw();
}

static void onMenuSelectChannel(uint8_t ch1to7) {
  RFSelector::apply(ch1to7);
  Storage::saveSelectorState(ch1to7);
#if ENABLE_BLE
  BleGatt::notifySelectorChanged();
#endif
  DisplayUI::requestRedraw();
}

static void onMenuTristate() {
  RFSelector::apply(0);
  Storage::saveSelectorState(0);
#if ENABLE_BLE
  BleGatt::notifySelectorChanged();
#endif
  DisplayUI::requestRedraw();
}

void setup() {
  Serial.begin(115200); // ESP32-C3 native USB CDC
  delay(200);
  Serial.println("0: setup started");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.println("1: I2C started");

  // 1) Storage: mount SPIFFS, load JSON config and selector/RF state
  Storage::begin();
  Serial.println("2: storage started");

  // 2) Physical RF selector: apply the last saved state immediately
  RFSelector::begin();
  Serial.println("3: selector started");
  
  uint8_t savedSel = Storage::loadSelectorState();
  RFSelector::apply(savedSel);
  Serial.println("4: selector restored");

  // 3) RF core (wifi/bt): always present, restores its last saved state
  RFCore::begin();
  Serial.println("5: RF core started");

  // 4) Display + local menu
  DisplayUI::begin(onMenuToggleRfCore, onMenuSelectChannel, onMenuTristate);
  Serial.println("6: display started");

  // 5) CLI (TTL + USB always ready; BLE CLI is available whenever the RF
  //    core is enabled)
  CLI::begin();
  Serial.println("7: CLI started");

  Serial.println("Nozomi RF Selector Controller ready. Type 'help' for the command list.");
}

void loop() {
  DisplayUI::update();
  CLI::update();
  RFCore::update(); // executes any RF disable request scheduled from BLE

#if ENABLE_WEBSERVER
  if (RFCore::isEnabled()) WebPortal::handle();
#endif
}
