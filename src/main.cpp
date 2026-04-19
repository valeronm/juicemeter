#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <INA226.h>

#include "webui.h"

static constexpr int PIN_I2C_SDA = 4;
static constexpr int PIN_I2C_SCL = 3;

static constexpr uint8_t INA226_ADDR = 0x40;
static constexpr float   SHUNT_OHMS  = 0.1f;

// Hardware ceiling on a 0.1 ohm shunt is ~819 mA (shunt voltage saturation).
struct Range { float maxA; const char *label; };
static const Range RANGES[] = {
  { 0.05f, "50 mA"  },
  { 0.10f, "100 mA" },
  { 0.20f, "200 mA" },
  { 0.50f, "500 mA" },
  { 0.80f, "800 mA" },
};
static constexpr int N_RANGES = std::size(RANGES);
static int range_idx = 3;   // start at 500 mA

// Below this |I| the readout switches to 3-decimal hi-res and the state
// indicator reads IDLE. One knob keeps the two behaviors consistent.
static constexpr float    LOW_I_THRESH_MA = 1.0f;
static constexpr uint32_t DISPLAY_MS      = 200;

// Integration is in mA*s / mW*s; divide by this to present mAh / mWh.
static constexpr double SECS_PER_HOUR = 3600.0;

// Screen layout y-coordinates.
static constexpr int HDR_TIME_Y  = 4;    // elapsed, top-right row 1
static constexpr int HDR_STATE_Y = 8;    // state, top-left (bigger font, offset down)
static constexpr int HDR_RANGE_Y = 20;   // range, top-right row 2
static constexpr int HDR_SEP_Y   = 36;   // divider line
static constexpr int ROW_BAT_Y   = 44;
static constexpr int ROW_DEV_Y   = 72;
static constexpr int ROW_BUS_Y   = 100;  // diagnostic raw INA V_bus
static constexpr int ROW_OUT_Y   = 122;  // accumulated out mAh / mWh
static constexpr int ROW_IN_Y    = 144;  // accumulated in  mAh / mWh

// Lumped (outbound + return) wire + contact resistance between the INA226
// pins and the real terminals. Calibrate by comparing a multimeter reading
// at the terminal to the uncorrected screen reading under known current.
static constexpr float R_DUT_SIDE_OHM = 0.088f;   // charger+/load+ <-> IN-
static constexpr float R_BAT_SIDE_OHM = 0.013f;   // battery+ <-> IN+

// Recover the real terminal voltage behind each side's wire+contact drop.
// Signed current (INA convention: +IN+->IN-) makes one formula work both
// charge and discharge directions.
static inline float batTerminalV(const float v_bus, const float v_shunt, const float i_mA) {
  return v_bus + v_shunt + (i_mA * 1e-3f) * R_BAT_SIDE_OHM;
}
static inline float devTerminalV(const float v_bus, const float i_mA) {
  return v_bus - (i_mA * 1e-3f) * R_DUT_SIDE_OHM;
}

INA226      ina(INA226_ADDR);
TFT_eSPI    tft;
TFT_eSprite spr(&tft);   // full-frame backbuffer, allocated in setup()

// Positive current = "out" (battery -> load), negative = "in" (charging).
static float    i_peak_mA = 0.0f;
static float    p_peak_mW = 0.0f;
static double   q_out_mAh = 0.0;
static double   q_in_mAh  = 0.0;
static double   e_out_mWh = 0.0;
static double   e_in_mWh  = 0.0;
static uint32_t start_ms  = 0;
static uint32_t last_sample_ms = 0;

// Display-window averages, reset each tick so noise averages out.
static double   disp_v_sum       = 0.0;
static double   disp_i_sum       = 0.0;
static double   disp_p_sum       = 0.0;
static double   disp_i_abs_sum   = 0.0;
static double   disp_vshunt_sum  = 0.0;
static uint32_t disp_n           = 0;

static void resetAccumulators() {
  i_peak_mA = 0.0f;
  p_peak_mW = 0.0f;
  q_out_mAh = q_in_mAh = 0.0;
  e_out_mWh = e_in_mWh = 0.0;
  start_ms = millis();
  last_sample_ms = start_ms;
}

static void applyRange(const int idx) {
  range_idx = idx;
  ina.setMaxCurrentShunt(RANGES[idx].maxA, SHUNT_OHMS, false);
  Serial.printf("[range] %s  LSB=%.3f uA\n",
                RANGES[idx].label, ina.getCurrentLSB() * 1e6f);
}

// Bump up at >80% of full scale (peak may have clipped), bump down when
// below 25% of the next-lower range for >1 s (hysteresis).
static void maybeAutoRange(const float i_mA, const uint32_t now) {
  static uint32_t below_since_ms = 0;
  static bool     below = false;

  const float i_abs = fabsf(i_mA);
  const float max_mA = RANGES[range_idx].maxA * 1000.0f;

  if (i_abs > 0.80f * max_mA && range_idx < N_RANGES - 1) {
    applyRange(range_idx + 1);
    i_peak_mA = 0.0f; p_peak_mW = 0.0f;
    below = false;
    return;
  }

  if (range_idx > 0) {
    const float lower_max_mA = RANGES[range_idx - 1].maxA * 1000.0f;
    if (i_abs < 0.25f * lower_max_mA) {
      if (!below) { below = true; below_since_ms = now; }
      else if (now - below_since_ms > 1000) {
        applyRange(range_idx - 1);
        i_peak_mA = 0.0f; p_peak_mW = 0.0f;
        below = false;
      }
    } else {
      below = false;
    }
  }
}

// All drawing targets the sprite; drawScreen fills and pushes in one go.

static void sprLeft(const int y, const uint16_t color, const char *fmt, const float val) {
  char buf[32];
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(color, TFT_BLACK);
  snprintf(buf, sizeof(buf), fmt, val);
  spr.drawString(buf, 6, y);
}

static void sprRight(const int y, const uint16_t color, const char *fmt, const float val) {
  char buf[32];
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(color, TFT_BLACK);
  snprintf(buf, sizeof(buf), fmt, val);
  spr.drawString(buf, spr.width() - 6, y);
}

static void drawScreen(const float voltage, const float vshunt, const float current_mA, const float power_mW, const bool hires) {
  spr.fillSprite(TFT_BLACK);

  // State colors match the out=red / in=green convention of the energy totals.
  const bool      idle        = hires;
  const char     *state       = idle ? "IDLE" : (current_mA > 0.0f ? "DISCHARGE" : "CHARGE");
  const uint16_t  state_color = idle ? TFT_DARKGREY : (current_mA > 0.0f ? TFT_RED : TFT_GREEN);

  spr.setFreeFont(&FreeMono12pt7b);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(state_color, TFT_BLACK);
  spr.drawString(state, 4, HDR_STATE_Y);

  // IP (or "wifi...") lives in the narrow gap between the state label and
  // the right-aligned time/range column; ML_DATUM vertical-centers it.
  spr.setTextDatum(ML_DATUM);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString(webui::statusLabel(), 134, HDR_SEP_Y / 2, 2);

  const uint32_t s = (millis() - start_ms) / 1000;
  char buf[40];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString(buf, spr.width() - 6, HDR_TIME_Y, 2);
  spr.drawString(RANGES[range_idx].label, spr.width() - 6, HDR_RANGE_Y, 2);

  spr.drawFastHLine(0, HDR_SEP_Y, spr.width(), TFT_DARKGREY);

  // "B:" / "D:" keep enough gap before the mA/mW column in bold monospace.
  spr.setFreeFont(&FreeMonoBold12pt7b);
  sprLeft ( ROW_BAT_Y, TFT_GREEN,  "B: %6.3f V", batTerminalV(voltage, vshunt, current_mA));
  sprLeft ( ROW_DEV_Y, TFT_CYAN,   "D: %6.3f V", devTerminalV(voltage, current_mA));
  sprRight( ROW_BAT_Y, TFT_YELLOW, hires ? "%7.3f mA" : "%7.2f mA", fabsf(current_mA));
  sprRight( ROW_DEV_Y, TFT_ORANGE, hires ? "%7.3f mW" : "%7.2f mW", fabsf(power_mW));

  spr.setTextDatum(TL_DATUM);

  // Uncompensated INA reading for diagnostic comparison with B: / D:.
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  snprintf(buf, sizeof(buf), "bus %6.3f V", voltage);
  spr.drawString(buf, 6, ROW_BUS_Y, 2);

  spr.setTextColor(TFT_RED, TFT_BLACK);
  snprintf(buf, sizeof(buf), "out %7.2f mAh / %7.2f mWh",
           q_out_mAh, e_out_mWh);
  spr.drawString(buf, 6, ROW_OUT_Y, 2);

  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(buf, sizeof(buf), " in %7.2f mAh / %7.2f mWh",
           q_in_mAh, e_in_mWh);
  spr.drawString(buf, 6, ROW_IN_Y, 2);

  spr.pushSprite(0, 0);
}

static void printHelp() {
  Serial.println("commands: r=reset  ?=help");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n[juicemeter] boot");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // ~108 KB at 16bpp. Fail loud so we don't silently drop to flickering draws.
  if (!spr.createSprite(tft.width(), tft.height())) {
    Serial.println("sprite alloc failed");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("sprite alloc failed", 4, 60, 4);
    while (true) delay(1000);
  }
  spr.fillSprite(TFT_BLACK);
  spr.pushSprite(0, 0);
  digitalWrite(TFT_BL, HIGH);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  if (!ina.begin()) {
    Serial.println("INA226 not found");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("INA226 not found", 4, 60, 4);
    while (true) delay(1000);
  }

  // Fast single-sample conversions so short transients land in the peak-hold;
  // software averaging over the display window smooths low-current readings.
  ina.setAverage(INA226_1_SAMPLE);
  ina.setBusVoltageConversionTime(INA226_140_us);
  ina.setShuntVoltageConversionTime(INA226_140_us);

  applyRange(range_idx);
  Serial.printf("INA226 ready. shunt=%.3f ohm  initial range %s\n",
                SHUNT_OHMS, RANGES[range_idx].label);

  resetAccumulators();

  webui::begin();

  printHelp();
}

void loop() {
  webui::loop();
  if (webui::consumeResetRequest()) {
    resetAccumulators();
    Serial.println("[reset] (web)");
  }

  while (Serial.available()) {
    int c = Serial.read();
    switch (c) {
      case 'r': case 'R':
        resetAccumulators();
        Serial.println("[reset]");
        break;
      case '?': case 'h': case 'H':
        printHelp();
        break;
      default: break;
    }
  }

  if (ina.isConversionReady()) {
    const uint32_t now = millis();
    const float v_bus   = ina.getBusVoltage();
    const float v_shunt = ina.getShuntVoltage();   // signed, V(IN+) - V(IN-)
    const float i_mA    = ina.getCurrent() * 1000.0f;
    // Battery-side voltage so mAh/mWh reflect what the cell gave/absorbed
    // (excludes shunt + wiring I^2*R losses).
    const float v_bat   = batTerminalV(v_bus, v_shunt, i_mA);
    const float p_mW    = v_bat * i_mA;

    maybeAutoRange(i_mA, now);

    // Peak by magnitude, storing the signed value.
    if (fabsf(i_mA) > fabsf(i_peak_mA)) i_peak_mA = i_mA;
    if (fabsf(p_mW) > fabsf(p_peak_mW)) p_peak_mW = p_mW;

    const double dt_h = (now - last_sample_ms) / 1000.0 / SECS_PER_HOUR;
    const double dq_mAh = (double)fabsf(i_mA) * dt_h;
    const double de_mWh = (double)fabsf(p_mW) * dt_h;
    if (i_mA >= 0.0f) { q_out_mAh += dq_mAh; e_out_mWh += de_mWh; }
    else              { q_in_mAh  += dq_mAh; e_in_mWh  += de_mWh; }

    disp_v_sum      += v_bus;
    disp_i_sum      += i_mA;
    disp_p_sum      += p_mW;
    disp_i_abs_sum  += fabsf(i_mA);
    disp_vshunt_sum += v_shunt;
    disp_n++;

    last_sample_ms = now;
  }

  static uint32_t last_disp_ms = 0;
  const uint32_t now = millis();
  if (now - last_disp_ms >= DISPLAY_MS) {
    last_disp_ms = now;
    if (disp_n == 0) return;

    const float v       = (float)(disp_v_sum      / disp_n);
    const float i_mA    = (float)(disp_i_sum      / disp_n);
    const float p_mW    = (float)(disp_p_sum      / disp_n);
    const float i_abs   = (float)(disp_i_abs_sum  / disp_n);
    const float v_shunt = (float)(disp_vshunt_sum / disp_n);
    disp_v_sum = disp_i_sum = disp_p_sum = disp_i_abs_sum = disp_vshunt_sum = 0.0;
    disp_n = 0;

    // Use mean |I| rather than |mean I|, so symmetric charge/discharge
    // swings don't drop the display into hi-res/IDLE falsely.
    const bool hires = i_abs < LOW_I_THRESH_MA;

    Serial.printf("V=%.3f I=%.3f mA P=%.3f mW | pk I=%.3f P=%.3f | out %.4f mAh / %.4f mWh  in %.4f mAh / %.4f mWh\n",
                  v, i_mA, p_mW, i_peak_mA, p_peak_mW,
                  q_out_mAh, e_out_mWh, q_in_mAh, e_in_mWh);

    drawScreen(v, v_shunt, i_mA, p_mW, hires);

    webui::Sample ws{};
    ws.v_bus       = v;
    ws.v_bat       = batTerminalV(v, v_shunt, i_mA);
    ws.v_dev       = devTerminalV(v, i_mA);
    ws.i_mA        = i_mA;
    ws.p_mW        = p_mW;
    ws.i_peak_mA   = i_peak_mA;
    ws.p_peak_mW   = p_peak_mW;
    ws.q_out_mAh   = q_out_mAh;
    ws.q_in_mAh    = q_in_mAh;
    ws.e_out_mWh   = e_out_mWh;
    ws.e_in_mWh    = e_in_mWh;
    ws.elapsed_s   = (millis() - start_ms) / 1000;
    ws.range_label = RANGES[range_idx].label;
    ws.state       = hires ? "IDLE" : (i_mA > 0.0f ? "DISCHARGE" : "CHARGE");
    webui::publish(ws);
  }
}
