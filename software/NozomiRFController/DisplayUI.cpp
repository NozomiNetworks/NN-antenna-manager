#include "DisplayUI.h"
#include "Config.h"
#include "RFSelector.h"
#include "RFCore.h"
#include "Storage.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <SPIFFS.h>

static Adafruit_SH1106G g_oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// ---- callbacks toward the application logic --------------------------------
static ToggleRFCoreCb  s_onToggleRfCore = nullptr;
static SelectChannelCb s_onSelectChannel = nullptr;
static TristateCb      s_onTristate = nullptr;

// ---- menu state -------------------------------------------------------------
// 0 = toggle RF core, 1..7 = SELECT #n, 8 = TRI-STATE
#define MENU_ITEM_COUNT 9
static int s_menuIndex = 0;
static bool s_sleeping = false;
static unsigned long s_lastActivity = 0;
static bool s_needRedraw = true;
static bool s_bootLogoShown = false;

// ---- button debounce --------------------------------------------------------
static bool s_lastNextRaw = HIGH;      // last raw level seen on the pin
static bool s_lastEnterRaw = HIGH;
static bool s_lastNextState = HIGH;    // debounced state
static bool s_lastEnterState = HIGH;
static unsigned long s_lastNextChange = 0;
static unsigned long s_lastEnterChange = 0;

// Expected size, in bytes, of the packed 1bpp logo bitmap: row-padded to
// a whole byte per row, OLED_HEIGHT rows.
#define LOGO_EXPECTED_BYTES (((OLED_WIDTH + 7) / 8) * OLED_HEIGHT)
static uint8_t *s_logoBuf = nullptr; // allocated on demand, freed after boot

// Tries to load /logo.bin from SPIFFS into s_logoBuf. Returns true on
// success (correct size read). The buffer is heap-allocated because the
// bitmap (128x64/8 = 1024 bytes) is too large to keep as a permanent
// static reservation alongside everything else this firmware already
// holds in RAM; it is freed right after the splash screen is done.
static bool loadLogoBitmap() {
  if (!SPIFFS.exists(LOGO_BITMAP_PATH)) return false;
  File f = SPIFFS.open(LOGO_BITMAP_PATH, "r");
  if (!f) return false;
  size_t sz = f.size();
  if (sz != LOGO_EXPECTED_BYTES) {
    // Wrong size for a 128x64 1bpp bitmap: refuse it rather than risk
    // drawing garbage/reading out of bounds.
    f.close();
    return false;
  }
  s_logoBuf = (uint8_t *)malloc(LOGO_EXPECTED_BYTES);
  if (!s_logoBuf) { f.close(); return false; }
  size_t got = f.read(s_logoBuf, LOGO_EXPECTED_BYTES);
  f.close();
  if (got != LOGO_EXPECTED_BYTES) {
    free(s_logoBuf);
    s_logoBuf = nullptr;
    return false;
  }
  return true;
}

// Text-only fallback splash, used if /logo.bin is missing/invalid in
// SPIFFS (e.g. before the filesystem image has ever been uploaded).
static void drawBootLogoTextFallback() {
  g_oled.clearDisplay();
  g_oled.setTextColor(SH110X_WHITE);
  g_oled.setTextSize(2);
  g_oled.setCursor(14, 18);
  g_oled.print("NOZOMI");
  g_oled.setTextSize(1);
  g_oled.setCursor(18, 42);
  g_oled.print("RF SELECTOR CTRL");
  g_oled.setCursor(30, 54);
  g_oled.print("v" FW_VERSION);
  g_oled.display();
}

// Draws the boot splash: real bitmap logo from SPIFFS if present and
// valid, otherwise the text-only fallback above. Frees the bitmap buffer
// right after drawing it (it's only needed for this single frame).
static void drawBootLogo() {
  if (loadLogoBitmap()) {
    g_oled.clearDisplay();
    g_oled.drawBitmap(0, 0, s_logoBuf, OLED_WIDTH, OLED_HEIGHT, SH110X_WHITE);
    g_oled.display();
    free(s_logoBuf);
    s_logoBuf = nullptr;
  } else {
    drawBootLogoTextFallback();
  }
}

static const char *menuLabel(int idx) {
  static char buf[24];
  if (idx == 0) {
    return RFCore::isEnabled() ? "DISABLE RF (BT/WIFI)" : "ENABLE RF (BT/WIFI)";
  } else if (idx >= 1 && idx <= 7) {
    snprintf(buf, sizeof(buf), "SELECT #%d", idx);
    return buf;
  } else {
    return "TRI-STATE";
  }
}

static void formatHz(uint32_t hz, char *out, size_t outLen) {
  if (hz >= 1000000000UL) snprintf(out, outLen, "%.2fG", hz / 1e9);
  else if (hz >= 1000000UL) snprintf(out, outLen, "%.2fM", hz / 1e6);
  else if (hz >= 1000UL) snprintf(out, outLen, "%.2fK", hz / 1e3);
  else snprintf(out, outLen, "%luHz", (unsigned long)hz);
}

static void drawStatusBar() {
  // left column: wifi
  int8_t rssi = RFCore::wifiRssiPercent();
  g_oled.setCursor(0, 0);
  if (!RFCore::isEnabled()) {
    g_oled.print("W:off");
  } else if (rssi < 0) {
    g_oled.print("W:--");
  } else {
    g_oled.print("W:");
    g_oled.print(rssi);
    g_oled.print("%");
  }

  // bluetooth
  g_oled.setCursor(48, 0);
  g_oled.print(RFCore::bleActive() ? "BT" : "  ");

  // channel / tristate
  g_oled.setCursor(72, 0);
  uint8_t sel = RFSelector::current();
  if (sel == 0) g_oled.print("TRI");
  else { g_oled.print("CH"); g_oled.print(sel); }

  // known antenna range for the current channel (if known)
  g_oled.setCursor(0, 9);
  if (sel >= 1 && sel <= 7 && g_cfg.channels[sel].known) {
    char lo[10], hi[10];
    formatHz(g_cfg.channels[sel].minHz, lo, sizeof(lo));
    formatHz(g_cfg.channels[sel].maxHz, hi, sizeof(hi));
    g_oled.print(lo); g_oled.print("-"); g_oled.print(hi);
  } else if (sel == 0) {
    g_oled.print("mixer disabled");
  } else {
    g_oled.print("range n/a");
  }

  g_oled.drawFastHLine(0, 18, OLED_WIDTH, SH110X_WHITE);
}

static void drawMenu() {
  g_oled.clearDisplay();
  drawStatusBar();

  g_oled.setTextSize(1);
  g_oled.setCursor(0, 24);
  g_oled.print("> ");
  g_oled.print(menuLabel(s_menuIndex));

  // small position indicator (e.g. 3/9)
  char pos[8];
  snprintf(pos, sizeof(pos), "%d/%d", s_menuIndex + 1, MENU_ITEM_COUNT);
  g_oled.setCursor(OLED_WIDTH - (strlen(pos) * 6), 24);
  g_oled.print(pos);

  g_oled.display();
}

static void wake() {
  s_sleeping = false;
  s_menuIndex = 0;
  s_lastActivity = millis();
  s_needRedraw = true;
}

static void sleep() {
  s_sleeping = true;
  g_oled.clearDisplay();
  g_oled.display();
  g_oled.oled_command(SH110X_DISPLAYOFF);
}

static void wakeDisplayHw() {
  g_oled.oled_command(SH110X_DISPLAYON);
}

void DisplayUI::begin(ToggleRFCoreCb onToggleRfCore,
                       SelectChannelCb onSelectChannel,
                       TristateCb onTristate) {
  s_onToggleRfCore = onToggleRfCore;
  s_onSelectChannel = onSelectChannel;
  s_onTristate = onTristate;

  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_ENTER, INPUT_PULLUP);

  g_oled.begin(OLED_I2C_ADDR);
  g_oled.setTextColor(SH110X_WHITE);

  drawBootLogo();
  s_bootLogoShown = true;
  s_lastActivity = millis();
}

void DisplayUI::requestRedraw() { s_needRedraw = true; }
bool DisplayUI::isSleeping() { return s_sleeping; }

void DisplayUI::update() {
  unsigned long now = millis();

  // handle the "boot logo -> menu" transition
  if (s_bootLogoShown) {
    if (now - s_lastActivity < LOGO_SHOW_MS) return;
    s_bootLogoShown = false;
    s_lastActivity = now;
    s_needRedraw = true;
  }

  // --- button reading with debounce -----------------------------------
  bool nextPressed = false, enterPressed = false;

  // Track the raw level and the debounced level separately: the debounced
  // state may only change after the raw level has been stable for
  // BTN_DEBOUNCE_MS. A press is the debounced falling edge (active-low).
  bool nextRaw = digitalRead(PIN_BTN_NEXT);
  if (nextRaw != s_lastNextRaw) {
    s_lastNextRaw = nextRaw;
    s_lastNextChange = now;
  }
  if ((now - s_lastNextChange) > BTN_DEBOUNCE_MS && nextRaw != s_lastNextState) {
    s_lastNextState = nextRaw;
    if (nextRaw == LOW) nextPressed = true;
  }

  bool enterRaw = digitalRead(PIN_BTN_ENTER);
  if (enterRaw != s_lastEnterRaw) {
    s_lastEnterRaw = enterRaw;
    s_lastEnterChange = now;
  }
  if ((now - s_lastEnterChange) > BTN_DEBOUNCE_MS && enterRaw != s_lastEnterState) {
    s_lastEnterState = enterRaw;
    if (enterRaw == LOW) enterPressed = true;
  }

  if (nextPressed || enterPressed) {
    if (s_sleeping) {
      // any button: wake the display and reset to menu 0, without
      // executing the action associated with that press
      wakeDisplayHw();
      wake();
    } else {
      s_lastActivity = now;
      if (nextPressed) {
        s_menuIndex = (s_menuIndex + 1) % MENU_ITEM_COUNT;
        s_needRedraw = true;
      }
      if (enterPressed) {
        if (s_menuIndex == 0) {
          if (s_onToggleRfCore) s_onToggleRfCore();
        } else if (s_menuIndex >= 1 && s_menuIndex <= 7) {
          if (s_onSelectChannel) s_onSelectChannel((uint8_t)s_menuIndex);
        } else {
          if (s_onTristate) s_onTristate();
        }
        s_needRedraw = true;
      }
    }
  }

  // --- power-off timeout -------------------------------------------
  if (!s_sleeping && (now - s_lastActivity) > DISPLAY_TIMEOUT_MS) {
    sleep();
    return;
  }

  if (!s_sleeping && s_needRedraw) {
    drawMenu();
    s_needRedraw = false;
  }
}
