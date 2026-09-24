# Nozomi RF Selector Controller — ESP32-C3-WROOM-02

Firmware for controlling a 3-bit + tri-state RF selector (mixer), with a
local interface (2 buttons + OLED), a CLI over TTL/USB/BLE serial,
configuration via BLE GATT, and a WiFi web portal.

## Project structure

Open the folder in Arduino IDE: the `.ino` file plus all the `.h/.cpp`
files in the same folder are compiled together as a single sketch.

```
NozomiRFController.ino   setup()/loop(), module orchestration
Config.h                 pins, constants, build #define options
Storage.h / .cpp         JSON config (SPIFFS) + selector/RF state (EEPROM/NVS)
RFSelector.h / .cpp      writes the 4 physical selector pins
RFCore.h / .cpp          combined WiFi+BLE enable/disable (RF stage)
DisplayUI.h / .cpp       OLED, boot logo, menu, status bar, auto power-off
WebPortal.h / .cpp       config web server (STA or setup AP)
BleGatt.h / .cpp         standard+custom GATT, Nordic UART-like BLE CLI
CLI.h / .cpp             shared TTL/USB/BLE command interpreter
```

## Required libraries (Arduino IDE Library Manager)

- **Adafruit SH110X** + **Adafruit GFX Library** (for the SH1106 I2C OLED)
- **ArduinoJson** (v6.x)
- **NimBLE-Arduino** by h2zero, **version 2.5.0** (the code has been checked
  line-by-line against the official 2.5.0 API docs/headers) — used for all
  BLE functionality (GATT server + Nordic UART-like CLI). The BLE module in
  this project is `BleGatt.h`/`BleGatt.cpp` (intentionally **not** called
  `BLEService`: NimBLE-Arduino 2.x defines, for backward compatibility with
  the old library, a preprocessor macro `#define BLEService NimBLEService`
  — naming our own module `BLEService` would get silently rewritten by the
  preprocessor and fail to link/compile in confusing ways).
- **If you previously had `BLEService.h`/`BLEService.cpp` in your sketch
  folder from an earlier version of this project, delete those files
  physically.** Arduino IDE compiles every `.cpp` file present in the
  sketch folder, so leftover old files keep failing to build even after
  `BleGatt.h`/`BleGatt.cpp` are added alongside them. When in doubt, empty
  the sketch folder entirely and re-extract the zip into it.
- **Do not** also install the old classic "ESP32 BLE Arduino" library
  (Bluedroid-based, historically by nkolban): it does not fully support
  the BLE-only ESP32-C3, and if present in your Arduino `libraries` folder
  it can shadow headers and cause build errors such as
  `fatal error: esp_gap_ble_api.h: No such file or directory`. Remove that
  folder if present.
- **ESP32 by Espressif** core (provides `WiFi.h`, `WebServer.h`,
  `SPIFFS.h`, `Preferences.h`)

In the Board Manager select an **ESP32C3 Dev Module** board (or equivalent)
with **USB CDC On Boot: Enabled** (required to have `Serial` act as the
native USB serial on the ESP32-C3).

> The "Multiple libraries were found for WiFi.h" message some IDEs print
> (e.g. pointing at a WiFiNINA fork also installed) is just an informational
> warning, not an error — the ESP32 core's own `WiFi.h` is the one actually
> used, as shown by the "Used:" line.

## Physical wiring (defaults in `Config.h`, adjustable)

| Signal                   | ESP32-C3 pin |
|--------------------------|:---:|
| NEXT button (to GND)     | GPIO4 |
| ENTER button (to GND)    | GPIO5 |
| I2C SDA (OLED + EEPROM)  | GPIO8 |
| I2C SCL (OLED + EEPROM)  | GPIO9 |
| UART TTL TX (Serial1)    | GPIO21 |
| UART TTL RX (Serial1)    | GPIO20 |
| Selector S0              | GPIO2 |
| Selector S1               | GPIO3 |
| Selector S2               | GPIO6 |
| Selector S3 (tri-state)   | GPIO7 |
| USB                        | native board USB pins (D+/D-) |

Buttons should be wired between the pin and GND, using `INPUT_PULLUP`
(already handled in firmware — no external resistors needed).

## Selector logic

```
SELECT #1..#7  ->  S3=0, S2:S0 = 1..7 (binary)
TRI-STATE      ->  S3=1, S2:S0 = 0
```

The state is saved **on every change** to EEPROM (or NVS) and re-applied to
the physical pins right after boot, **before** enabling any radio.

## Real EEPROM vs emulated

In `Config.h`:

```c
#define USE_EXTERNAL_EEPROM   1   // 1 = external I2C chip (e.g. 24LC256)
#define EXT_EEPROM_I2C_ADDR   0x50
```

With `USE_EXTERNAL_EEPROM 0`, the selector/RF-enable state is instead saved
in NVS via `Preferences` (emulated flash) — useful if no dedicated EEPROM
chip is mounted, at the cost of more flash wear over time if the selector
is changed very frequently.

## "Soft" configuration (SPIFFS `/config.json`)

Contains the device name, WiFi credentials, and the Hz range of each of the
8 antennas (`rf#0`..`rf#7`). Example:

```json
{
  "device_name": "NozomiRFSel",
  "wifi_ssid": "",
  "wifi_pass": "",
  "channels": [
    { "name": "ANT-0", "min_hz": 0, "max_hz": 0, "known": false },
    { "name": "ANT-1", "min_hz": 400000000, "max_hz": 1000000000, "known": true }
  ]
}
```

It can be edited from: the CLI (`ant`, `wifi`, `name`, `save`), the web
pages (`/antennas`, `/wifi`), or the custom BLE characteristics.

## RF core behavior (WiFi + Bluetooth)

WiFi and BLE are **always present**: the stack is started right in
`setup()`, restoring the last saved state (on the very first boot ever the
default is **enabled**, since "always present" is the normal behavior —
the device is reachable right away via app/web portal without going
through the local menu).

When you need to do a **capture with SDR boards** and want to prevent the
device itself from emitting WiFi/BT during the measurement, the RF stage
can be explicitly disabled from:

- the **local menu** (the "DISABLE RF (BT/WIFI)" / "ENABLE RF (BT/WIFI)" entry);
- the **CLI** over TTL or USB serial (`rf off` / `rf on` / `rf toggle` —
  these serial channels always remain available, regardless of the RF
  state, so you can always turn the device back on over a wired connection);
- the **boolean BLE characteristic "Enable RF"** (`...def3`,
  read/write/notify, 1 byte: `1`=on, `0`=disabled) — meant to be driven by
  a script/app connected over BLE right before starting the SDR capture.

The enabled/disabled state is **persisted** (same EEPROM/NVS as the
selector): if you disable the RF stage for a measurement session and power
the device off, it will stay disabled on the next boot too, until you
explicitly re-enable it.

> **Technical note on the BLE characteristic**: writing `0` to "Enable RF"
> will also shut down the BLE connection itself (naturally, since BLE is
> being turned off). To avoid tearing down the Bluetooth stack while it is
> still processing the write in progress (risk of a hang/crash), the actual
> shutdown is **deferred by ~400 ms** (`RF_DISABLE_GRACE_MS` in
> `Config.h`) and executed from the main `loop()` — so the BLE client has
> time to receive the write ACK before the disconnection. To turn RF back
> on after disabling it via BLE you then need the local menu or the wired
> CLI (BLE is, by definition, no longer reachable).

If no SSID is configured, when WiFi starts up the device opens a **setup
Access Point** (`<device_name>-setup`, "classic" ESP32 method); connect to
it and go to `http://192.168.4.1/wifi` to enter the SSID/password. If the
STA connection attempt is slow/fails, boot can take up to ~15s before the
setup AP opens.

## OLED menu

- Boot: Nozomi logo, loaded from `/logo.bin` in SPIFFS (128x64, 1 bit per
  pixel, packed MSB-first with each row padded to a whole byte — the
  standard `Adafruit_GFX::drawBitmap` format). The bitmap ships in this
  project's `data/` folder; see "Uploading the logo to SPIFFS" below for
  how to flash it. If the file is missing or the wrong size, `DisplayUI`
  automatically falls back to a text-only splash screen instead of
  crashing or drawing garbage.
- Top row: WiFi signal strength (or `off`), a `BT` icon if active,
  channel/`TRI`, known Hz range of the selected antenna.
- Bottom row: current menu entry.
- **NEXT**: scrolls the menu. **ENTER**: confirms/toggles the current action.
- After 30s without any button press the display turns off; the next press
  turns it back on and resets the menu to the first entry (that press does
  **not** execute an action, it only wakes up the display).

## Uploading the logo to SPIFFS

The `data/logo.bin` file in this project needs to be flashed to the
device's SPIFFS filesystem (separately from the sketch itself). Arduino
IDE 2.x removed the old "ESP32 Sketch Data Upload" menu item from 1.8.x,
so use one of these:

**Option A — IDE plugin (easiest if available):** install the community
"Arduino ESP32 filesystem uploader" extension for Arduino IDE 2.x (search
the VS Code/Arduino IDE extension marketplace, or see
https://github.com/earlephilhower/arduino-littlefs-upload — note this
specific plugin targets LittleFS, so if you switch this project from
SPIFFS to LittleFS you'd use it directly; for SPIFFS look for an
equivalent "ESP32FS" fork compatible with IDE 2.x). Once installed, it
adds a command (usually via the command palette, Cmd/Ctrl+Shift+P) to
upload the sketch's `data/` folder to the flash filesystem.

**Option B — manual, always works, no plugin needed:**
1. Find `mkspiffs` (bundled with the ESP32 board package, under
   `~/Library/Arduino15/packages/esp32/tools/mkspiffs/<version>/mkspiffs`
   on macOS) and `esptool.py`/`esptool` (also bundled under
   `~/Library/Arduino15/packages/esp32/tools/esptool_py/<version>/`).
2. Build a filesystem image matching your partition scheme's SPIFFS size
   and offset (check **Tools → Partition Scheme** in the IDE, and look up
   the exact size/offset for that scheme in the corresponding `.csv` file
   under `~/Library/Arduino15/packages/esp32/hardware/esp32/<version>/tools/partitions/`).
   For example, with "Minimal SPIFFS (1.9MB APP with OTA)":
   ```
   mkspiffs -c data -b 4096 -p 256 -s 0x170000 spiffs.bin
   ```
   (adjust `-s` to the actual SPIFFS partition size from the `.csv` file).
3. Flash it to the SPIFFS partition offset (also from the `.csv` file),
   e.g.:
   ```
   esptool.py --chip esp32c3 --port /dev/cu.usbmodemXXXX write_flash 0x290000 spiffs.bin
   ```
4. Reset the board. The logo should now appear at boot; if it doesn't
   (wrong offset/size), the firmware safely falls back to the text splash
   instead of showing garbage.

Regenerating the bitmap from a different source image (e.g. a new logo
revision) is straightforward with Python + Pillow: render/resize to
128x64 1-bit, pad rows to a whole byte, pack MSB-first — ask if you'd
like the conversion script.



## CLI (TTL / USB / BLE)

```
help
status
select <1-7>
tristate
rf on|off|toggle
name <name>
wifi <ssid> <password>
ant <0-7> <minHz> <maxHz>
save
reboot
```

The same CLI is also reachable over BLE via a **Nordic UART**-like service
(UUID `6E400001-...`), usable with apps such as "nRF Connect" or "Serial
Bluetooth Terminal".

## BLE GATT (available whenever the RF core is active)

- Standard Device Information Service (`0x180A`): manufacturer, model,
  firmware revision.
- Custom service `12345678-1234-5678-1234-56789abcdef0`:
  - selected channel (`...def1`, read/write/notify, 1 byte)
  - Hz range of the active channel (`...def2`, read/notify, 8 bytes: min32+max32 LE)
  - **Enable RF** (`...def3`, read/write/notify, 1 boolean byte — see the
    grace-delay note above)
  - Hz range for rf#0..rf#7 (`...bcdea0`..`...bcdea7`, read/write, 8 bytes)
- Nordic UART-like CLI service (`6E400001-...`).

## Web (available whenever the RF core is active)

- `/` — device status, active channel + range, range of all antennas
- `/antennas` — form to set name+min+max for each of the 8 antennas
- `/channel` — channel 1-7/tri-state radio group + antenna suggestion given
  a requested range (with a Hz/MHz/GHz unit selector)
- `/wifi` — SSID/password/device name (save and reboot)
- `/api/status` — status as JSON

## Notes / possible extensions

- **OTA** updates: the ESP32 core includes `ArduinoOTA`/`Update.h`; it's
  not included by default in this skeleton to keep the code more readable,
  but it's easy to add into `WebPortal::begin()` with
  `ArduinoOTA.begin()` + `ArduinoOTA.handle()` in the loop while the RF
  core is active, or with an `/update` endpoint based on `Update.h` for
  browser-based upload.
- The first-setup AP portal does not implement a true captive-portal DNS
  redirect; the user needs to manually browse to `192.168.4.1`. For a real
  captive portal, add a `DNSServer` with a wildcard redirect.
