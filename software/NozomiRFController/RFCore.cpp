#include "RFCore.h"
#include "Config.h"
#include "Storage.h"
#include "WebPortal.h"
#include "BleGatt.h"
#include <WiFi.h>

static bool s_enabled = false;
static bool s_disablePending = false;
static unsigned long s_disableAt = 0;

static void doEnable() {
  if (s_enabled) return;
  s_enabled = true;
#if ENABLE_WEBSERVER
  WebPortal::begin();   // handles STA/AP depending on the saved config
#endif
#if ENABLE_BLE
  BleGatt::begin();
#endif
}

static void doDisable() {
  if (!s_enabled) return;
  s_enabled = false;
#if ENABLE_WEBSERVER
  WebPortal::stop();
#endif
#if ENABLE_BLE
  BleGatt::stop();
#endif
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  btStop();
}

void RFCore::begin() {
  // WiFi and BLE are "always present": on boot we restore the last saved
  // state (absolute default on the very first boot: enabled = true).
  bool wantEnabled = Storage::loadRfEnabled();
  if (wantEnabled) doEnable();
}

void RFCore::enable() {
  doEnable();
  Storage::saveRfEnabled(true);
  s_disablePending = false;
}

void RFCore::disable() {
  doDisable();
  Storage::saveRfEnabled(false);
  s_disablePending = false;
}

void RFCore::toggle() {
  if (s_enabled) disable(); else enable();
}

bool RFCore::isEnabled() { return s_enabled; }

void RFCore::requestDisable() {
  if (!s_enabled || s_disablePending) return;
  s_disablePending = true;
  s_disableAt = millis() + RF_DISABLE_GRACE_MS;
}

void RFCore::update() {
  if (s_disablePending && (long)(millis() - s_disableAt) >= 0) {
    s_disablePending = false;
    disable(); // executed from the main loop context, not from a BLE callback
  }
}

int8_t RFCore::wifiRssiPercent() {
  if (!s_enabled) return -1;
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    if (WiFi.softAPgetStationNum() > 0) return 100;
    return -1;
  }
  if (WiFi.status() != WL_CONNECTED) return -1;
  long rssi = WiFi.RSSI(); // typically -100 (worst) .. -30 (best)
  int pct = (int)((rssi + 100) * 2); // map -100..-50 -> 0..100
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (int8_t)pct;
}

bool RFCore::bleActive() {
#if ENABLE_BLE
  return s_enabled && BleGatt::isRunning();
#else
  return false;
#endif
}
