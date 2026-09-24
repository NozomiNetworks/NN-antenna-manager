/*
 * BleGatt.cpp
 *
 * Implemented with NimBLE-Arduino (https://github.com/h2zero/NimBLE-Arduino).
 * Verified against the official 2.5.0 API docs/headers (class member
 * lists for NimBLEService, NimBLECharacteristic, NimBLEServerCallbacks,
 * NimBLECharacteristicCallbacks, NimBLEAttValue): createCharacteristic(),
 * setValue(uint8_t*, size_t), notify(), and all callback signatures below
 * match 2.5.0 exactly. If you have an older 1.4.x version installed,
 * remove the NimBLEConnInfo& / reason parameters from the callbacks below
 * and switch getValue<std::string>() back to plain getValue().
 *
 * This is NOT the classic Bluedroid-based "ESP32 BLE Arduino" library.
 * That library's headers (e.g. esp_gap_ble_api.h) are frequently
 * missing/mismatched on the ESP32-C3 (BLE-only chip) depending on the
 * exact core/library versions installed. NimBLE avoids Bluedroid
 * entirely, is officially recommended for C3/S3, uses less RAM/flash,
 * and has a cleaner dependency footprint.
 *
 * Required library (Arduino IDE Library Manager): "NimBLE-Arduino" by
 * h2zero, version 2.5.0. If you also have the old classic
 * "ESP32_BLE_Arduino" library installed manually in your Arduino
 * libraries folder, remove it to avoid header conflicts. Likewise, if you
 * previously had this project's old "BLEService.h"/"BLEService.cpp"
 * files in your sketch folder, DELETE them physically — Arduino IDE
 * compiles every .cpp file present in the sketch folder, so leftover old
 * files keep failing to build even after new files are added alongside
 * them.
 */
#include "BleGatt.h"
#include "Config.h"
#include "Storage.h"
#include "RFSelector.h"
#include "RFCore.h"

#include <NimBLEDevice.h>
#include <queue>

// ---------------------------------------------------------------------------
// UUIDs
// ---------------------------------------------------------------------------
// Standard Device Information Service (BLE SIG)
#define UUID_DEVINFO_SERVICE   "0000180A-0000-1000-8000-00805F9B34FB"
#define UUID_CHAR_MANUFACTURER "00002A29-0000-1000-8000-00805F9B34FB"
#define UUID_CHAR_MODEL        "00002A24-0000-1000-8000-00805F9B34FB"
#define UUID_CHAR_FWREV        "00002A26-0000-1000-8000-00805F9B34FB"

// Custom Nozomi service
#define UUID_NOZOMI_SERVICE     "12345678-1234-5678-1234-56789abcdef0"
#define UUID_CHAR_SELECTED_CH   "12345678-1234-5678-1234-56789abcdef1"
#define UUID_CHAR_SELECTED_RNG  "12345678-1234-5678-1234-56789abcdef2"
#define UUID_CHAR_ENABLE_RF     "12345678-1234-5678-1234-56789abcdef3" // bool, 1 byte, r/w/notify
// range for rf#0..rf#7: base UUID ...dea0 + index (0-7)
#define UUID_CHAR_ANT_BASE      "12345678-1234-5678-1234-56789abcdea"

// "Nordic UART"-like CLI service
#define UUID_NUS_SERVICE  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define UUID_NUS_RX       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // client->device write
#define UUID_NUS_TX       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // device->client notify

static NimBLEServer *s_server = nullptr;
static NimBLECharacteristic *s_chSelected = nullptr;
static NimBLECharacteristic *s_chSelectedRange = nullptr;
static NimBLECharacteristic *s_chEnableRf = nullptr;
static NimBLECharacteristic *s_chAntenna[NUM_CHANNELS] = { nullptr };
static NimBLECharacteristic *s_nusTx = nullptr;
static NimBLECharacteristic *s_nusRx = nullptr;
static bool s_running = false;

static std::queue<String> s_bleCliRxLines;
static String s_bleCliRxBuffer;

// ---------------------------------------------------------------------------
// Helper: packs a range (min32,max32) little-endian into 8 bytes
// ---------------------------------------------------------------------------
static void packRange(uint8_t *out8, uint32_t minHz, uint32_t maxHz) {
  memcpy(out8, &minHz, 4);
  memcpy(out8 + 4, &maxHz, 4);
}

static void updateSelectedCharacteristics() {
  uint8_t sel = RFSelector::current();
  if (s_chSelected) {
    s_chSelected->setValue(&sel, 1);
    s_chSelected->notify();
  }
  if (s_chSelectedRange) {
    uint8_t buf[8] = {0};
    if (sel >= 1 && sel <= 7) packRange(buf, g_cfg.channels[sel].minHz, g_cfg.channels[sel].maxHz);
    s_chSelectedRange->setValue(buf, 8);
    s_chSelectedRange->notify();
  }
  if (s_chEnableRf) {
    uint8_t en = RFCore::isEnabled() ? 1 : 0;
    s_chEnableRf->setValue(&en, 1);
    s_chEnableRf->notify();
  }
}

// ---------------------------------------------------------------------------
// Write callback: selected channel
// ---------------------------------------------------------------------------
class SelectedChWriteCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    std::string v = c->getValue<std::string>();
    if (v.length() < 1) return;
    uint8_t val = (uint8_t)v[0];
    if (val > 7) return;
    RFSelector::apply(val);
    Storage::saveSelectorState(val);
    updateSelectedCharacteristics();
  }
};

// ---------------------------------------------------------------------------
// Write callback: enable/disable RF stage (bool, 1 byte)
//
// IMPORTANT: if the written value is 0 (disable), we do NOT turn WiFi/BLE
// off right here: this callback runs in the BLE stack's own task/context,
// and tearing down the BLE stack while it is still processing its own
// write can hang/crash the device. We therefore delegate the actual
// shutdown to RFCore::update(), called from the main loop(), after a short
// grace delay (so the BLE client still receives the GATT ACK before the
// disconnection).
// ---------------------------------------------------------------------------
class EnableRfWriteCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    std::string v = c->getValue<std::string>();
    if (v.length() < 1) return;
    bool wantEnabled = (v[0] != 0);
    if (wantEnabled) {
      // If it's already enabled (practically always the case, since we are
      // connected via BLE) there is nothing to do; this idempotency avoids
      // needless stack restarts.
      if (!RFCore::isEnabled()) RFCore::enable();
    } else {
      RFCore::requestDisable();
    }
  }
};

// ---------------------------------------------------------------------------
// Write callback: antenna rf#i range
// ---------------------------------------------------------------------------
class AntennaWriteCb : public NimBLECharacteristicCallbacks {
public:
  explicit AntennaWriteCb(int idx) : m_idx(idx) {}
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    std::string v = c->getValue<std::string>();
    if (v.length() < 8) return;
    uint32_t minHz, maxHz;
    memcpy(&minHz, v.data(), 4);
    memcpy(&maxHz, v.data() + 4, 4);
    g_cfg.channels[m_idx].minHz = minHz;
    g_cfg.channels[m_idx].maxHz = maxHz;
    g_cfg.channels[m_idx].known = (maxHz > 0);
    Storage::saveConfig();
    updateSelectedCharacteristics();
  }
private:
  int m_idx;
};

// ---------------------------------------------------------------------------
// Callback: CLI reception over BLE (Nordic UART RX)
// ---------------------------------------------------------------------------
class NusRxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    std::string v = c->getValue<std::string>();
    for (char ch : v) {
      if (ch == '\n' || ch == '\r') {
        if (s_bleCliRxBuffer.length() > 0) {
          s_bleCliRxLines.push(s_bleCliRxBuffer);
          s_bleCliRxBuffer = "";
        }
      } else {
        s_bleCliRxBuffer += ch;
      }
    }
  }
};

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, NimBLEConnInfo &connInfo) override { /* nothing special */ }
  void onDisconnect(NimBLEServer *s, NimBLEConnInfo &connInfo, int reason) override {
    // restart advertising so a new client can connect
    if (s_running) NimBLEDevice::getAdvertising()->start();
  }
};

void BleGatt::begin() {
  if (s_running) return;

  NimBLEDevice::init(g_cfg.device_name);
  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new ServerCb());

  // ---- Device Information Service -----------------------------------
  NimBLEService *devInfo = s_server->createService(UUID_DEVINFO_SERVICE);
  auto *chManuf = devInfo->createCharacteristic(UUID_CHAR_MANUFACTURER, NIMBLE_PROPERTY::READ);
  chManuf->setValue(FW_VENDOR);
  auto *chModel = devInfo->createCharacteristic(UUID_CHAR_MODEL, NIMBLE_PROPERTY::READ);
  chModel->setValue(FW_MODEL);
  auto *chFw = devInfo->createCharacteristic(UUID_CHAR_FWREV, NIMBLE_PROPERTY::READ);
  chFw->setValue(FW_VERSION);
  devInfo->start();

  // ---- Custom Nozomi service -----------------------------------------
  NimBLEService *nz = s_server->createService(UUID_NOZOMI_SERVICE);

  // Note: NimBLE automatically adds the 0x2902 (CCCD) descriptor to any
  // characteristic that has the NOTIFY or INDICATE property, so there is
  // no need to manually add a BLE2902-style descriptor as with the
  // classic Bluedroid-based BLE library.
  s_chSelected = nz->createCharacteristic(UUID_CHAR_SELECTED_CH,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  s_chSelected->setCallbacks(new SelectedChWriteCb());

  s_chSelectedRange = nz->createCharacteristic(UUID_CHAR_SELECTED_RNG,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  // Boolean "Enable RF": 1 = wifi/bt active (normal "always present"
  // state), 0 = RF stage disabled (e.g. for SDR captures). Writing 0 here
  // will also cause the BLE connection itself to drop after a short grace
  // delay (see EnableRfWriteCb / RFCore::requestDisable()).
  s_chEnableRf = nz->createCharacteristic(UUID_CHAR_ENABLE_RF,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  s_chEnableRf->setCallbacks(new EnableRfWriteCb());

  for (int i = 0; i < NUM_CHANNELS; i++) {
    // build a slightly different UUID for each antenna by replacing the
    // last nibble of the base UUID with the index (0-7)
    char uuidBuf[40];
    snprintf(uuidBuf, sizeof(uuidBuf), "%s%01x", UUID_CHAR_ANT_BASE, i);
    s_chAntenna[i] = nz->createCharacteristic(uuidBuf,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    uint8_t buf[8];
    packRange(buf, g_cfg.channels[i].minHz, g_cfg.channels[i].maxHz);
    s_chAntenna[i]->setValue(buf, 8);
    s_chAntenna[i]->setCallbacks(new AntennaWriteCb(i));
  }
  nz->start();

  // ---- CLI service (Nordic UART-like) --------------------------------
#if ENABLE_CLI_BLE
  NimBLEService *nus = s_server->createService(UUID_NUS_SERVICE);
  s_nusTx = nus->createCharacteristic(UUID_NUS_TX, NIMBLE_PROPERTY::NOTIFY);
  s_nusRx = nus->createCharacteristic(UUID_NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  s_nusRx->setCallbacks(new NusRxCb());
  nus->start();
#endif

  updateSelectedCharacteristics();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(UUID_NOZOMI_SERVICE);
  // Note: no explicit setScanResponse() call here — its availability/name
  // has varied across NimBLE-Arduino versions (some expose
  // setScanResponse(bool), others only setScanResponseData(...), others
  // manage it automatically). The device name and service UUID above are
  // enough for discovery; add a scan response payload here only if you
  // need extra advertising bytes and check your installed version's API.
  adv->start();

  s_running = true;
}

void BleGatt::stop() {
  if (!s_running) return;
  NimBLEDevice::deinit(true);
  s_server = nullptr;
  s_chSelected = nullptr;
  s_chSelectedRange = nullptr;
  s_chEnableRf = nullptr;
  for (int i = 0; i < NUM_CHANNELS; i++) s_chAntenna[i] = nullptr;
  s_nusTx = s_nusRx = nullptr;
  s_running = false;
}

bool BleGatt::isRunning() { return s_running; }

void BleGatt::notifySelectorChanged() {
  if (s_running) updateSelectedCharacteristics();
}

void BleGatt::bleCliPrint(const String &s) {
  if (!s_running || !s_nusTx) return;
  // Notify has a limited MTU: we chunk it into 20-byte pieces
  int len = s.length();
  const int chunk = 20;
  for (int i = 0; i < len; i += chunk) {
    String part = s.substring(i, min(i + chunk, len));
    s_nusTx->setValue((uint8_t *)part.c_str(), part.length());
    s_nusTx->notify();
    delay(5);
  }
}

bool BleGatt::bleCliAvailable() { return !s_bleCliRxLines.empty(); }

String BleGatt::bleCliReadLine() {
  if (s_bleCliRxLines.empty()) return "";
  String l = s_bleCliRxLines.front();
  s_bleCliRxLines.pop();
  return l;
}
