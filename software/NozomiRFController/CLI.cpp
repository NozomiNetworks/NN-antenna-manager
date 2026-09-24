#include "CLI.h"
#include "Config.h"
#include "Storage.h"
#include "RFSelector.h"
#include "RFCore.h"
#include "BleGatt.h"
#include <ArduinoJson.h>

// source of a command line, so we can reply on the same channel
enum class Source { USB, TTL, BLE };

static String buildStatusJson() {
  StaticJsonDocument<1024> doc;
  doc["device_name"] = g_cfg.device_name;
  doc["fw_version"] = FW_VERSION;
  doc["rf_core_enabled"] = RFCore::isEnabled();
  doc["selector"] = RFSelector::current();
  JsonArray chans = doc.createNestedArray("channels");
  for (int i = 0; i < NUM_CHANNELS; i++) {
    JsonObject c = chans.createNestedObject();
    c["idx"] = i; c["name"] = g_cfg.channels[i].name;
    c["min_hz"] = g_cfg.channels[i].minHz; c["max_hz"] = g_cfg.channels[i].maxHz;
    c["known"] = g_cfg.channels[i].known;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

static void reply(Source src, const String &line) {
  switch (src) {
    case Source::USB: Serial.println(line); break;
    case Source::TTL: Serial1.println(line); break;
    case Source::BLE: BleGatt::bleCliPrint(line + "\r\n"); break;
  }
}

static const char *HELP_TEXT =
  "Available commands:\r\n"
  "  help                        shows this help\r\n"
  "  status                      status in JSON format\r\n"
  "  select <1-7>                sets the RF selector\r\n"
  "  tristate                    forces the selector into tri-state\r\n"
  "  rf on|off|toggle            enables/disables the RF stage (wifi+bt)\r\n"
  "                              (use 'rf off' before an SDR capture)\r\n"
  "  name <name>                 sets the device name\r\n"
  "  wifi <ssid> <password>      sets the WiFi credentials\r\n"
  "  ant <0-7> <minHz> <maxHz>   sets an antenna's range\r\n"
  "  save                        saves the configuration to SPIFFS\r\n"
  "  reboot                      reboots the device";

static void processLine(Source src, String line) {
  line.trim();
  if (line.length() == 0) return;

  int sp = line.indexOf(' ');
  String cmd = (sp == -1) ? line : line.substring(0, sp);
  String rest = (sp == -1) ? "" : line.substring(sp + 1);
  cmd.toLowerCase();

  if (cmd == "help") {
    reply(src, HELP_TEXT);

  } else if (cmd == "status") {
    reply(src, buildStatusJson());

  } else if (cmd == "select") {
    int v = rest.toInt();
    if (v >= 1 && v <= 7) {
      RFSelector::apply((uint8_t)v);
      Storage::saveSelectorState((uint8_t)v);
      BleGatt::notifySelectorChanged();
      reply(src, "OK select=" + String(v));
    } else {
      reply(src, "ERR valid range is 1-7");
    }

  } else if (cmd == "tristate") {
    RFSelector::apply(0);
    Storage::saveSelectorState(0);
    BleGatt::notifySelectorChanged();
    reply(src, "OK tristate");

  } else if (cmd == "rf") {
    rest.trim(); rest.toLowerCase();
    if (rest == "on") RFCore::enable();
    else if (rest == "off") RFCore::disable();
    else RFCore::toggle();
    reply(src, String("OK rf=") + (RFCore::isEnabled() ? "on" : "off"));

  } else if (cmd == "name") {
    if (rest.length() > 0) {
      strlcpy(g_cfg.device_name, rest.c_str(), sizeof(g_cfg.device_name));
      reply(src, "OK name=" + rest + " (use 'save' to make it persistent)");
    } else {
      reply(src, "ERR usage: name <name>");
    }

  } else if (cmd == "wifi") {
    int sp2 = rest.indexOf(' ');
    if (sp2 == -1) { reply(src, "ERR usage: wifi <ssid> <password>"); return; }
    String ssid = rest.substring(0, sp2);
    String pass = rest.substring(sp2 + 1);
    strlcpy(g_cfg.wifi_ssid, ssid.c_str(), sizeof(g_cfg.wifi_ssid));
    strlcpy(g_cfg.wifi_pass, pass.c_str(), sizeof(g_cfg.wifi_pass));
    reply(src, "OK wifi set (use 'save' then 'reboot')");

  } else if (cmd == "ant") {
    int p1 = rest.indexOf(' ');
    int p2 = (p1 == -1) ? -1 : rest.indexOf(' ', p1 + 1);
    if (p1 == -1 || p2 == -1) { reply(src, "ERR usage: ant <0-7> <minHz> <maxHz>"); return; }
    int idx = rest.substring(0, p1).toInt();
    uint32_t minHz = (uint32_t)rest.substring(p1 + 1, p2).toDouble();
    uint32_t maxHz = (uint32_t)rest.substring(p2 + 1).toDouble();
    if (idx < 0 || idx >= NUM_CHANNELS) { reply(src, "ERR index must be 0-7"); return; }
    g_cfg.channels[idx].minHz = minHz;
    g_cfg.channels[idx].maxHz = maxHz;
    g_cfg.channels[idx].known = (maxHz > 0);
    BleGatt::notifySelectorChanged();
    reply(src, "OK ant" + String(idx) + " set (use 'save')");

  } else if (cmd == "save") {
    bool ok = Storage::saveConfig();
    reply(src, ok ? "OK saved" : "ERR save failed");

  } else if (cmd == "reboot") {
    reply(src, "OK rebooting...");
    delay(200);
    ESP.restart();

  } else {
    reply(src, "ERR unknown command, use 'help'");
  }
}

// ---------------------------------------------------------------------------
// Non-blocking, line-by-line reading from a generic Stream
// ---------------------------------------------------------------------------
static String s_usbBuf, s_ttlBuf;

static void pumpStream(Stream &s, String &buf, Source src) {
  while (s.available()) {
    char c = (char)s.read();
    if (c == '\n' || c == '\r') {
      if (buf.length() > 0) { processLine(src, buf); buf = ""; }
    } else {
      buf += c;
      if (buf.length() > 200) buf = ""; // anti-overflow guard
    }
  }
}

void CLI::begin() {
#if ENABLE_CLI_TTL
  Serial1.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
#endif
  // Serial (USB CDC) is already initialized in main.ino's setup()
}

void CLI::update() {
#if ENABLE_CLI_USB
  pumpStream(Serial, s_usbBuf, Source::USB);
#endif
#if ENABLE_CLI_TTL
  pumpStream(Serial1, s_ttlBuf, Source::TTL);
#endif
#if ENABLE_CLI_BLE
  while (BleGatt::bleCliAvailable()) {
    processLine(Source::BLE, BleGatt::bleCliReadLine());
  }
#endif
}
