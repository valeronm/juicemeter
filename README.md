# Juicemeter

> Bench meter for profiling LiPo-powered devices — live V/I/P on a TFT and a live dashboard in your browser.

An ESP32-C3 sits between a LiPo cell and your device under test with an INA226 on a 0.1 Ω shunt. It shows live battery-terminal voltage, current and power on a small ST7789 display, and — once WiFi associates — serves a real-time dashboard at `http://juicemeter.local/` with a rolling chart, accumulated charge / energy, and a reset button.

<!-- TODO: drop a photo of the bench setup here -->

## Features

- **Live V / I / P** — 5 Hz averaged readout on the TFT and WebSocket-pushed to the web dashboard.
- **Signed current** — positive = discharge (battery → DUT), negative = charge (charger → battery). Colour-coded and reflected in the header state (`DISCHARGE` / `CHARGE` / `IDLE`).
- **Charge and energy accumulators** — separate totals for "out" (discharged) and "in" (charged) in mAh and mWh, so a full charge/discharge cycle stays legible.
- **Peak hold** — signed peaks of I and P tracked by magnitude, visible on the dashboard.
- **Autoranging** — 50 / 100 / 200 / 500 / 800 mA with hysteresis; catches both low-current quiescent loads and hundreds-of-mA peaks without manual range swapping.
- **Wire-loss compensation** — corrects out the lumped wire + contact resistance on each side so the headline voltage is the *actual* battery terminal, not just what the INA226 sees.
- **Web dashboard** — responsive single-page UI served from flash; no filesystem partition, no external CDN. Works over mDNS (`juicemeter.local`) or the DHCP IP.
- **Standalone-safe** — if WiFi is unreachable the bench tool still works as a local display; the web server just never starts.

## Hardware

| Part | Notes | Price | Source |
|---|---|---|---|
| ESP32-C3 supermini | PlatformIO board `esp32-c3-devkitc-02`, pioarduino platform, native USB-CDC | €2.85 | [AliExpress](https://pt.aliexpress.com/item/1005006406538478.html) |
| INA226 breakout | I²C address `0x40`, 0.1 Ω shunt | €2.30 | [AliExpress](https://pt.aliexpress.com/item/1005009890292157.html) |
| ST7789 banner panel | 320×172, requires `CGRAM_OFFSET` and BGR colour order | €2.88 | [AliExpress](https://pt.aliexpress.com/item/1005011879100861.html) |
| 2× JST-PH 2.0 | battery-side and DUT-side power terminals | €2.85 / 10 pairs | [AliExpress](https://pt.aliexpress.com/item/4001034326413.html) |

### Wiring

INA226 is in the high side between the battery and the DUT. Sign convention is baked into the firmware:

```
[ battery + ] ──► INA226 IN+   IN- ──► [ DUT + ]
[ battery - ] ───────────────────────► [ DUT - ]
```

| Signal | ESP32-C3 GPIO |
|---|---|
| INA226 SDA | 4 |
| INA226 SCL | 3 |
| INA226 ALERT | 2 (reserved, not yet used by firmware) |
| TFT SCLK | 5 |
| TFT MOSI | 6 |
| TFT RST | 7 |
| TFT DC | 8 |
| TFT CS | 9 |
| TFT BL | 10 |

GPIO 20 / 21 are USB D+/D-; GPIO 2 / 8 / 9 are strap pins. Don't reassign without understanding the consequences.

## Build and flash

[PlatformIO](https://platformio.org/) handles toolchain + library pins.

```sh
# 1. WiFi credentials — gitignored
cp include/secrets.h.example include/secrets.h
$EDITOR include/secrets.h

# 2. Build and flash
pio run -t upload

# 3. Follow the boot log on the serial monitor
pio device monitor
```

On first boot you'll see `[juicemeter] boot`, followed by INA226 config, the assigned IP, and then a 2 Hz telemetry line per display tick.

## Using it

- Open `http://juicemeter.local/` (or the IP printed on serial and on the TFT header) in a browser on the same network.
- The **Reset totals** button zeroes the peak-hold, the in/out accumulators, and the elapsed-time counter.

## How it works

If you're poking around in the source, [`.claude/CLAUDE.md`](.claude/CLAUDE.md) has the design rationale — why sampling is decoupled from display, how wire-loss compensation is derived, why the autoranger resets peaks on range change, and the boot-sequence quirks that are specific to the ESP32-C3 and the banner-panel ST7789.

## License

[MIT](LICENSE).
