# NN-antenna-manager

Open source hardware + firmware for a custom antenna manager (RF selector) for SDR cards.

The board lets an SDR receiver/transceiver share a single RF port while switching between up to
**8 antennas** through a 50 Ω **SP8T RF switch**, plus a **tri-state** (all ports disconnected)
mode. Antenna selection can be changed:

- locally, with **2 buttons + an OLED display**;
- over **USB / TTL serial / BLE** with a text CLI;
- from a **BLE GATT client** (custom characteristics for channel, antenna Hz ranges, RF enable);
- from a **WiFi web portal** (status, channel selection, antenna configuration).

The selector state is persisted and re-applied immediately at boot, before any radio is enabled.
For SDR capture sessions, the ESP32's own WiFi/Bluetooth stage can be disabled entirely
(persisted until re-enabled) so the device does not emit any RF itself.

## Repository structure

```
hardware/
  kicad_project/   KiCad 8+ schematic and PCB (NN-antenna-manager.kicad_pro)
  fabrication/     BOM.xlsx, CPL.xlsx (pick & place), gerber.zip — ready for manufacturing
software/          Arduino IDE sketch for the ESP32-C3-WROOM-02 (see software/README.md)
LICENSES/          CERN-OHL-P-2.0.txt, Apache-2.0.txt
```

## Hardware

KiCad project with full fabrication outputs. Main components:

| Ref | Part | Role |
|-----|------|------|
| U1 | ESP32-C3-WROOM-02 | WiFi 2.4 GHz + BLE 5 module (RISC-V), runs the firmware |
| IC1 | PE42482A-X (pSemi) | SP8T absorptive 50 Ω RF switch, QFN-24 |
| U3 | AP2112K-3.3 | 3.3 V / 600 mA LDO regulator |
| U2 | USBLC6-2SC6 | ESD protection on the USB data lines |
| D1–D3 | PESD5V0L1ULD + LED | ESD protection, status LED |
| J2–J10 | SMA edge-mount (Amphenol 132289) | 8 antenna ports + 1 common port (to the SDR) |
| J1 | USB-C receptacle | Power + native USB serial (CLI) |
| J11 | 1x04 pin socket | I2C header for the SSD1306 128x64 OLED |
| J12 | 1x03 pin header | TTL UART (Serial1 CLI) |
| SW1–SW4 | B3U-1000P push buttons | NEXT / ENTER menu navigation |

The default pin mapping (buttons, I2C, UART, selector bits S0–S3) is defined in
`software/Config.h` and documented in `software/README.md`.

### Selector logic

```
SELECT #1..#7  ->  S3=0, S2:S0 = 1..7 (binary)
TRI-STATE      ->  S3=1, S2:S0 = 0
```

## Software

The `software/` folder is an Arduino IDE sketch (open the folder directly; the `.ino` plus all
`.h/.cpp` files compile together). Features:

- **Local UI** — boot logo, status bar (WiFi signal, BLE, channel, antenna Hz range), menu with
  channel selection, tri-state and RF enable/disable; display auto-off after 30 s.
- **CLI** (same command set on TTL serial, USB CDC and BLE Nordic-UART):
  `help`, `status`, `select <1-7>`, `tristate`, `rf on|off|toggle`, `name`, `wifi`, `ant`,
  `save`, `reboot`.
- **BLE GATT** — Device Information Service (0x180A), custom service with selected channel,
  active antenna Hz range, "Enable RF" boolean, and per-antenna Hz ranges (read/write).
- **Web portal** — `/` status, `/antennas`, `/channel`, `/wifi` (setup AP fallback if no SSID is
  configured), `/api/status` (JSON).
- **Persistence** — selector and RF state in an external I2C EEPROM (24LCxx, or NVS emulation via
  `USE_EXTERNAL_EEPROM 0`); device name, WiFi credentials and per-antenna Hz ranges in SPIFFS
  (`/config.json`).

Required libraries, wiring tables, build options and flashing instructions (including the SPIFFS
boot logo) are detailed in **[software/README.md](software/README.md)**.

## Getting started

1. **Board**: order the PCB using `hardware/fabrication/gerber.zip` with `BOM.xlsx` and
   `CPL.xlsx`, or open `hardware/kicad_project/NN-antenna-manager.kicad_pro` to modify/re-export.
2. **Firmware**: open `software/` in the Arduino IDE, select an **ESP32C3 Dev Module** board with
   **USB CDC On Boot: Enabled**, install the libraries listed in `software/README.md`, and flash.
3. **Use**: select the antenna from the OLED menu, the web portal, a BLE app or the serial CLI;
   set each antenna's known Hz range so the `/channel` page can suggest the right one.

## License

- `hardware/` — [CERN Open Hardware Licence Version 2 - Permissive](LICENSES/CERN-OHL-P-2.0.txt)
- `software/` — [Apache License 2.0](LICENSES/Apache-2.0.txt)

See [LICENSE](LICENSE) for details.
