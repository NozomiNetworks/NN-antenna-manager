#include "WebPortal.h"
#include "Config.h"
#include "Storage.h"
#include "RFSelector.h"
#include "RFCore.h"
#include "BleGatt.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

static WebServer server(80);
static bool s_running = false;
static bool s_apSetupMode = false; // true = first-setup AP (ssid/pass missing)

// ---------------------------------------------------------------------------
// Utility: "value + unit" -> Hz conversion
// ---------------------------------------------------------------------------
static uint32_t toHz(double value, const String &unit) {
  if (unit == "GHz") return (uint32_t)(value * 1000000000.0);
  if (unit == "MHz") return (uint32_t)(value * 1000000.0);
  return (uint32_t)value; // "Hz"
}

static String htmlHeader(const String &title) {
  String s;
  s += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>"; s += title; s += "</title>";
  s += "<style>body{font-family:sans-serif;max-width:640px;margin:20px auto;padding:0 12px;background:#101418;color:#e8eef2}"
       "a{color:#7dd3fc} h1,h2{color:#7dd3fc} input,select{padding:6px;margin:4px 0;width:100%;box-sizing:border-box;"
       "background:#1c2530;color:#fff;border:1px solid #33404d;border-radius:4px}"
       "button{padding:8px 14px;background:#2563eb;color:#fff;border:none;border-radius:4px;cursor:pointer;margin-top:8px}"
       "table{width:100%;border-collapse:collapse} td,th{border-bottom:1px solid #33404d;padding:6px;text-align:left}"
       "nav a{margin-right:14px}</style></head><body>";
  s += "<nav><a href='/'>Status</a><a href='/antennas'>Antennas</a><a href='/channel'>Channel</a><a href='/wifi'>WiFi</a></nav>";
  return s;
}
static String htmlFooter() { return "</body></html>"; }

// ---------------------------------------------------------------------------
// "/" page - status
// ---------------------------------------------------------------------------
static void handleRoot() {
  String s = htmlHeader(g_cfg.device_name);
  s += "<h1>" + String(g_cfg.device_name) + "</h1>";
  s += "<p>Firmware " FW_VERSION " &middot; " FW_VENDOR " " FW_MODEL "</p>";
  s += "<p>RF stage: <b>" + String(RFCore::isEnabled() ? "ENABLED" : "DISABLED") + "</b></p>";

  uint8_t sel = RFSelector::current();
  s += "<h2>Selector status</h2><table>";
  s += "<tr><th>Active channel</th><td>";
  s += (sel == 0) ? "TRI-STATE" : ("SELECT #" + String(sel));
  s += "</td></tr>";
  if (sel >= 1 && sel <= 7 && g_cfg.channels[sel].known) {
    s += "<tr><th>Antenna range</th><td>" + String(g_cfg.channels[sel].minHz) + " Hz &ndash; "
       + String(g_cfg.channels[sel].maxHz) + " Hz</td></tr>";
  }
  s += "</table>";

  s += "<h2>Range of all antennas (rf#0..rf#7)</h2><table><tr><th>#</th><th>Name</th><th>Min Hz</th><th>Max Hz</th></tr>";
  for (int i = 0; i < NUM_CHANNELS; i++) {
    s += "<tr><td>" + String(i) + "</td><td>" + String(g_cfg.channels[i].name) + "</td><td>";
    s += g_cfg.channels[i].known ? String(g_cfg.channels[i].minHz) : String("-");
    s += "</td><td>";
    s += g_cfg.channels[i].known ? String(g_cfg.channels[i].maxHz) : String("-");
    s += "</td></tr>";
  }
  s += "</table>";
  s += htmlFooter();
  server.send(200, "text/html", s);
}

// ---------------------------------------------------------------------------
// "/antennas" page - configure the range for each of the 8 antennas
// ---------------------------------------------------------------------------
static void handleAntennasGet() {
  String s = htmlHeader("Antennas");
  s += "<h1>Antenna configuration</h1><form method='POST' action='/antennas'>";
  for (int i = 0; i < NUM_CHANNELS; i++) {
    s += "<h2>rf#" + String(i) + "</h2>";
    s += "Name<input name='name" + String(i) + "' value='" + String(g_cfg.channels[i].name) + "'>";
    s += "Min Hz<input type='number' step='any' name='min" + String(i) + "' value='" + String(g_cfg.channels[i].minHz) + "'>";
    s += "Max Hz<input type='number' step='any' name='max" + String(i) + "' value='" + String(g_cfg.channels[i].maxHz) + "'>";
  }
  s += "<button type='submit'>Save</button></form>";
  s += htmlFooter();
  server.send(200, "text/html", s);
}

static void handleAntennasPost() {
  for (int i = 0; i < NUM_CHANNELS; i++) {
    String nameKey = "name" + String(i), minKey = "min" + String(i), maxKey = "max" + String(i);
    if (server.hasArg(nameKey)) strlcpy(g_cfg.channels[i].name, server.arg(nameKey).c_str(), sizeof(g_cfg.channels[i].name));
    if (server.hasArg(minKey)) g_cfg.channels[i].minHz = (uint32_t)server.arg(minKey).toDouble();
    if (server.hasArg(maxKey)) g_cfg.channels[i].maxHz = (uint32_t)server.arg(maxKey).toDouble();
    g_cfg.channels[i].known = (g_cfg.channels[i].maxHz > 0);
  }
  Storage::saveConfig();
  BleGatt::notifySelectorChanged();
  server.sendHeader("Location", "/antennas");
  server.send(303);
}

// ---------------------------------------------------------------------------
// "/channel" page - channel selection + antenna suggestion
// ---------------------------------------------------------------------------
static void handleChannelGet() {
  uint8_t sel = RFSelector::current();
  String s = htmlHeader("Channel");
  s += "<h1>Channel selection</h1><form method='POST' action='/channel'>";
  for (int i = 1; i <= 7; i++) {
    s += "<label><input type='radio' name='ch' value='" + String(i) + "'";
    if (sel == i) s += " checked";
    s += "> SELECT #" + String(i) + "</label><br>";
  }
  s += "<label><input type='radio' name='ch' value='0'";
  if (sel == 0) s += " checked";
  s += "> TRI-STATE</label><br>";
  s += "<button type='submit'>Apply</button></form>";

  s += "<h2>Suggest an antenna for a requested range</h2><form method='GET' action='/channel'>";
  s += "Min<input type='number' step='any' name='rmin' value='" + server.arg("rmin") + "'>";
  s += "Max<input type='number' step='any' name='rmax' value='" + server.arg("rmax") + "'>";
  s += "Unit<select name='runit'>";
  for (auto u : {"Hz", "MHz", "GHz"}) {
    s += "<option"; if (server.arg("runit") == u) s += " selected"; s += ">"; s += u; s += "</option>";
  }
  s += "</select><button type='submit'>Suggest</button></form>";

  if (server.hasArg("rmin") && server.hasArg("rmax")) {
    double rmin = server.arg("rmin").toDouble();
    double rmax = server.arg("rmax").toDouble();
    String unit = server.hasArg("runit") ? server.arg("runit") : "Hz";
    uint32_t reqMin = toHz(rmin, unit), reqMax = toHz(rmax, unit);

    int bestFull = -1, bestOverlap = -1;
    uint32_t bestOverlapAmount = 0;
    for (int i = 1; i <= 7; i++) {
      if (!g_cfg.channels[i].known) continue;
      uint32_t cmin = g_cfg.channels[i].minHz, cmax = g_cfg.channels[i].maxHz;
      if (cmin <= reqMin && cmax >= reqMax) { bestFull = i; break; }
      uint32_t ovMin = max(cmin, reqMin), ovMax = min(cmax, reqMax);
      if (ovMax > ovMin) {
        uint32_t amt = ovMax - ovMin;
        if (amt > bestOverlapAmount) { bestOverlapAmount = amt; bestOverlap = i; }
      }
    }

    s += "<p><b>Requested:</b> " + String(reqMin) + " Hz - " + String(reqMax) + " Hz</p>";
    if (bestFull >= 0) {
      s += "<p style='color:#4ade80'><b>Recommended antenna: SELECT #" + String(bestFull) + "</b> ("
         + String(g_cfg.channels[bestFull].name) + ") fully covers the requested range.</p>";
    } else if (bestOverlap >= 0) {
      s += "<p style='color:#facc15'><b>No antenna fully covers the range.</b> Best partial coverage: SELECT #"
         + String(bestOverlap) + " (" + String(g_cfg.channels[bestOverlap].name) + ").</p>";
    } else {
      s += "<p style='color:#f87171'>No configured antenna covers even part of this range.</p>";
    }
  }

  s += htmlFooter();
  server.send(200, "text/html", s);
}

static void handleChannelPost() {
  if (server.hasArg("ch")) {
    uint8_t ch = (uint8_t)server.arg("ch").toInt();
    if (ch <= 7) {
      RFSelector::apply(ch);
      Storage::saveSelectorState(ch);
      BleGatt::notifySelectorChanged();
    }
  }
  server.sendHeader("Location", "/channel");
  server.send(303);
}

// ---------------------------------------------------------------------------
// "/wifi" page - device name + WiFi credentials
// ---------------------------------------------------------------------------
static void handleWifiGet() {
  String s = htmlHeader("WiFi");
  s += "<h1>WiFi / Device settings</h1><form method='POST' action='/wifi'>";
  s += "Device name<input name='devname' value='" + String(g_cfg.device_name) + "'>";
  s += "SSID<input name='ssid' value='" + String(g_cfg.wifi_ssid) + "'>";
  s += "Password<input name='pass' type='password' value='" + String(g_cfg.wifi_pass) + "'>";
  s += "<button type='submit'>Save and reboot</button></form>";
  if (s_apSetupMode) {
    s += "<p><i>First-setup mode: connected to access point \"" + String(WiFi.softAPSSID()) + "\".</i></p>";
  }
  s += htmlFooter();
  server.send(200, "text/html", s);
}

static void handleWifiPost() {
  if (server.hasArg("devname")) strlcpy(g_cfg.device_name, server.arg("devname").c_str(), sizeof(g_cfg.device_name));
  if (server.hasArg("ssid")) strlcpy(g_cfg.wifi_ssid, server.arg("ssid").c_str(), sizeof(g_cfg.wifi_ssid));
  if (server.hasArg("pass")) strlcpy(g_cfg.wifi_pass, server.arg("pass").c_str(), sizeof(g_cfg.wifi_pass));
  Storage::saveConfig();

  String s = htmlHeader("Rebooting");
  s += "<h1>Configuration saved</h1><p>The device is rebooting to apply the new settings...</p>";
  s += htmlFooter();
  server.send(200, "text/html", s);
  delay(500);
  ESP.restart();
}

// ---------------------------------------------------------------------------
// Status JSON API
// ---------------------------------------------------------------------------
static void handleApiStatus() {
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
  server.send(200, "application/json", out);
}

static void registerRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/antennas", HTTP_GET, handleAntennasGet);
  server.on("/antennas", HTTP_POST, handleAntennasPost);
  server.on("/channel", HTTP_GET, handleChannelGet);
  server.on("/channel", HTTP_POST, handleChannelPost);
  server.on("/wifi", HTTP_GET, handleWifiGet);
  server.on("/wifi", HTTP_POST, handleWifiPost);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(303); });
}

void WebPortal::begin() {
  if (s_running) return;

  bool haveCreds = strlen(g_cfg.wifi_ssid) > 0;

  if (haveCreds) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(g_cfg.device_name);
    WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_pass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(200);
  }

  if (!haveCreds || WiFi.status() != WL_CONNECTED) {
    // "Classic" ESP32 method: open a setup Access Point if not configured
    // or if the STA connection failed.
    s_apSetupMode = true;
    String apName = String(g_cfg.device_name) + "-setup";
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str());
  } else {
    s_apSetupMode = false;
  }

  registerRoutes();
  server.begin();
  s_running = true;
}

void WebPortal::stop() {
  if (!s_running) return;
  server.stop();
  s_running = false;
}

void WebPortal::handle() {
  if (s_running) server.handleClient();
}

bool WebPortal::isRunning() { return s_running; }
