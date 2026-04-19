# juicemeter

Bench tool to monitor power usage of LiPo-battery-powered devices. INA226 sits between the battery and the DUT; an ST7789 displays live readings and an ESP32-C3 serves a live dashboard over WiFi.

## Hardware

- **MCU:** ESP32-C3 supermini (PlatformIO board `esp32-c3-devkitc-02`, pioarduino platform). Native USB-CDC for serial.
- **Sensor:** INA226 on a 0.1 Ω shunt. Wiring convention: **IN+ on battery side, IN- on DUT side** — positive current is discharge, negative is charge. Every sign, colour, and accumulator direction in the firmware follows this.
- **Display:** ST7789 320×172 banner panel. Needs `CGRAM_OFFSET` and `TFT_BGR` — don't "fix" them to match a normal ST7789.

### Pin gotchas

- **GPIO 2 / 8 / 9** are ESP32-C3 strap pins. If boot/flash behaves oddly, suspect these first.
- **GPIO 8** doubles as the onboard blue LED, so it flickers with display traffic.
- **GPIO 20 / 21** are USB D+/D- for CDC — never reassign.
- **INA226 ALERT** is wired to GPIO 2 but not driven in firmware yet.

Pin map lives in `platformio.ini` (TFT) and `src/main.cpp` (`PIN_I2C_*`).

## Libraries

- **`bodmer/TFT_eSPI`** pinned to git master — the 2.5.43 release crashes on Arduino-ESP32 3.x / C3. Configured entirely via `build_flags`, no `User_Setup.h` edits.
- **`ESP32Async/AsyncTCP` + `ESP32Async/ESPAsyncWebServer`** — the me-no-dev originals are unmaintained and break on Arduino-ESP32 3.x.
- **`robtillaart/INA226`**.

## Build setup

WiFi credentials live in `include/secrets.h`, which is gitignored. First-time checkout: `cp include/secrets.h.example include/secrets.h` and fill in `WIFI_SSID` / `WIFI_PASS`.

## Boot quirks worth remembering

- **Native USB-CDC required** (`-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`). Without it, `Serial` writes into the void.
- **Explicit `SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1)` before `tft.init()`** — otherwise `tft.init()` hangs in `begin_tft_write()` on the C3.
- **Backlight held LOW** through `tft.init()` and the first cleared-sprite push to suppress a white flash on power-up.

## Runtime design notes

- **Fast sampling, slow display.** INA226 runs as fast as it can so short transients land in the peak-hold. The display and WebSocket push at a fixed slower cadence, showing a software-average of V/I/P over the refresh window. Averaging cleans noise and gives usable sub-mA readings without needing a dedicated quiescent mode.
- **Autoranging.** Five ranges from 50 mA up to the ~819 mA hardware ceiling (shunt-voltage saturation on the 0.1 Ω shunt). Bump up near full scale, bump down after a brief dwell below the next-lower range. Peaks reset on range change so a spike captured at the old LSB doesn't linger.
- **Wire-loss compensation.** `R_DUT_SIDE_OHM` and `R_BAT_SIDE_OHM` are the lumped outbound+return wire + contact resistances between the INA226 pins and the real terminals. `batTerminalV` / `devTerminalV` recover the true terminal voltages; the raw bus reading stays visible on the TFT as a diagnostic. Calibrate by comparing a multimeter reading at the terminal to the uncorrected screen reading under known current.
- **Power is `V × I`** (signed) computed in MCU rather than read from INA226's unsigned power register, so sign survives and matches the current sign.
- **Accumulators integrate mAh and mWh separately per direction** — out = discharge, in = charge — so a charge/discharge cycle's totals stay legible.
- **Low-current threshold (`LOW_I_THRESH_MA`)** drives both the hi-res numeric format and the `IDLE` state label. The comparison uses the windowed **mean of `|I|`**, not `|mean I|`, so a symmetric charge/discharge swing doesn't false-trigger the low-current view.

## Web UI (`src/webui.*`)

Joins WiFi in STA mode and serves a dashboard at `http://juicemeter.local/` (also its DHCP IP). Shows live V/I/P, DUT/INA voltages, accumulated mAh/mWh + peaks, a rolling V+I chart, and a **Reset totals** button. The TFT header also shows the IP between the state label and the time/range column.

The server only starts once WiFi associates — if the AP is unreachable, the bench tool still works as a standalone display and the serial log keeps flowing. `/reset` is unauthenticated; it flips a flag that `loop()` consumes on the Arduino task so the reset itself never runs on the async-TCP task.
