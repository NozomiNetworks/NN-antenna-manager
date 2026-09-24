#include "Storage.h"
#include <Wire.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#if USE_EXTERNAL_EEPROM
  // nothing extra beyond Wire.h: we implement byte read/write over I2C below
#else
  #include <Preferences.h>
  static Preferences g_prefs;
#endif

DeviceConfig g_cfg;

// Virtual address in the external EEPROM where we keep the selector state
#define EEPROM_ADDR_SELECTOR   0x0000
#define EEPROM_ADDR_MAGIC      0x0001
#define EEPROM_ADDR_RFENABLE   0x0002
#define EEPROM_MAGIC_VALUE     0xA5

// ---------------------------------------------------------------------------
// External I2C EEPROM helpers (24Cxx-style; a 1-byte address would be
// enough for small devices, but we use a 2-byte address for compatibility
// with larger chips such as 24LC256/512)
// ---------------------------------------------------------------------------
#if USE_EXTERNAL_EEPROM
static uint8_t extEepromRead(uint16_t addr) {
  Wire.beginTransmission(EXT_EEPROM_I2C_ADDR);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  Wire.endTransmission();
  Wire.requestFrom((int)EXT_EEPROM_I2C_ADDR, 1);
  if (Wire.available()) return Wire.read();
  return 0xFF;
}

static void extEepromWrite(uint16_t addr, uint8_t value) {
  Wire.beginTransmission(EXT_EEPROM_I2C_ADDR);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  Wire.write(value);
  Wire.endTransmission();
  delay(5); // typical EEPROM chip write cycle time
}
#endif

// ---------------------------------------------------------------------------
// Default config
// ---------------------------------------------------------------------------
void Storage::resetConfigToDefault() {
  memset(&g_cfg, 0, sizeof(g_cfg));
  strlcpy(g_cfg.device_name, DEFAULT_DEVICE_NAME, sizeof(g_cfg.device_name));
  g_cfg.wifi_ssid[0] = '\0';
  g_cfg.wifi_pass[0] = '\0';
  for (int i = 0; i < NUM_CHANNELS; i++) {
    snprintf(g_cfg.channels[i].name, sizeof(g_cfg.channels[i].name), "ANT-%d", i);
    g_cfg.channels[i].minHz = 0;
    g_cfg.channels[i].maxHz = 0;
    g_cfg.channels[i].known = false;
  }
}

bool Storage::loadConfig() {
  if (!SPIFFS.exists(CONFIG_JSON_PATH)) {
    resetConfigToDefault();
    return saveConfig();
  }

  File f = SPIFFS.open(CONFIG_JSON_PATH, "r");
  if (!f) {
    resetConfigToDefault();
    return false;
  }

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    resetConfigToDefault();
    return saveConfig();
  }

  resetConfigToDefault(); // start from defaults, then overwrite with present values

  strlcpy(g_cfg.device_name, doc["device_name"] | DEFAULT_DEVICE_NAME, sizeof(g_cfg.device_name));
  strlcpy(g_cfg.wifi_ssid,   doc["wifi_ssid"]   | "", sizeof(g_cfg.wifi_ssid));
  strlcpy(g_cfg.wifi_pass,   doc["wifi_pass"]   | "", sizeof(g_cfg.wifi_pass));

  JsonArray chans = doc["channels"].as<JsonArray>();
  if (!chans.isNull()) {
    int i = 0;
    for (JsonObject c : chans) {
      if (i >= NUM_CHANNELS) break;
      strlcpy(g_cfg.channels[i].name, c["name"] | g_cfg.channels[i].name, sizeof(g_cfg.channels[i].name));
      g_cfg.channels[i].minHz = c["min_hz"] | 0UL;
      g_cfg.channels[i].maxHz = c["max_hz"] | 0UL;
      g_cfg.channels[i].known = c["known"] | (g_cfg.channels[i].maxHz > 0);
      i++;
    }
  }
  return true;
}

bool Storage::saveConfig() {
  StaticJsonDocument<2048> doc;
  doc["device_name"] = g_cfg.device_name;
  doc["wifi_ssid"]   = g_cfg.wifi_ssid;
  doc["wifi_pass"]   = g_cfg.wifi_pass;

  JsonArray chans = doc.createNestedArray("channels");
  for (int i = 0; i < NUM_CHANNELS; i++) {
    JsonObject c = chans.createNestedObject();
    c["name"]   = g_cfg.channels[i].name;
    c["min_hz"] = g_cfg.channels[i].minHz;
    c["max_hz"] = g_cfg.channels[i].maxHz;
    c["known"]  = g_cfg.channels[i].known;
  }

  File f = SPIFFS.open(CONFIG_JSON_PATH, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

// ---------------------------------------------------------------------------
// Selector state
// ---------------------------------------------------------------------------
uint8_t Storage::loadSelectorState() {
#if USE_EXTERNAL_EEPROM
  uint8_t magic = extEepromRead(EEPROM_ADDR_MAGIC);
  if (magic != EEPROM_MAGIC_VALUE) {
    // first run: initialize selector to TRI-STATE (0) and RF to
    // "always present" (enabled=1), then write the magic byte to validate
    extEepromWrite(EEPROM_ADDR_SELECTOR, 0);
    extEepromWrite(EEPROM_ADDR_RFENABLE, 1);
    extEepromWrite(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VALUE);
    return 0;
  }
  uint8_t v = extEepromRead(EEPROM_ADDR_SELECTOR);
  if (v > 7) v = 0;
  return v;
#else
  g_prefs.begin("nozomi_rf", true); // read-only
  uint8_t v = g_prefs.getUChar("sel", 0);
  g_prefs.end();
  if (v > 7) v = 0;
  return v;
#endif
}

void Storage::saveSelectorState(uint8_t v) {
  if (v > 7) v = 0;
#if USE_EXTERNAL_EEPROM
  uint8_t cur = extEepromRead(EEPROM_ADDR_SELECTOR);
  if (cur != v) extEepromWrite(EEPROM_ADDR_SELECTOR, v);
#else
  g_prefs.begin("nozomi_rf", false);
  uint8_t cur = g_prefs.getUChar("sel", 0xFF);
  if (cur != v) g_prefs.putUChar("sel", v);
  g_prefs.end();
#endif
}

bool Storage::loadRfEnabled() {
#if USE_EXTERNAL_EEPROM
  uint8_t magic = extEepromRead(EEPROM_ADDR_MAGIC);
  if (magic != EEPROM_MAGIC_VALUE) {
    // first boot: default "always present" -> enabled
    extEepromWrite(EEPROM_ADDR_RFENABLE, 1);
    extEepromWrite(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VALUE);
    return true;
  }
  return extEepromRead(EEPROM_ADDR_RFENABLE) != 0;
#else
  g_prefs.begin("nozomi_rf", true);
  bool en = g_prefs.getUChar("rfen", 1) != 0; // default enabled
  g_prefs.end();
  return en;
#endif
}

void Storage::saveRfEnabled(bool en) {
#if USE_EXTERNAL_EEPROM
  uint8_t v = en ? 1 : 0;
  uint8_t cur = extEepromRead(EEPROM_ADDR_RFENABLE);
  if (cur != v) extEepromWrite(EEPROM_ADDR_RFENABLE, v);
#else
  g_prefs.begin("nozomi_rf", false);
  uint8_t cur = g_prefs.getUChar("rfen", 0xFF);
  uint8_t v = en ? 1 : 0;
  if (cur != v) g_prefs.putUChar("rfen", v);
  g_prefs.end();
#endif
}

bool Storage::begin() {
  bool ok = true;

  if (!SPIFFS.begin(true)) { // true = format if the filesystem is not valid
    Serial.println("[Storage] ERROR: SPIFFS mount failed");
    ok = false;
  }

#if USE_EXTERNAL_EEPROM
  // Wire.begin() is called centrally in main.ino (shared with the OLED)
#endif

  if (!loadConfig()) ok = false;

  return ok;
}
