#pragma once

#include <Arduino.h>

// Async WiFi + HTTP + WebSocket frontend. Starts as soon as STA joins; survives
// a missing/unreachable AP without blocking the INA sampler.
namespace webui {

struct Sample {
  float    v_bus;        // raw INA bus voltage
  float    v_bat;        // compensated battery-terminal voltage
  float    v_dev;        // compensated DUT-terminal voltage
  float    i_mA;         // signed (>=0 discharge, <0 charge)
  float    p_mW;         // signed, same convention as i_mA
  float    i_peak_mA;    // signed peak-by-magnitude
  float    p_peak_mW;
  double   q_out_mAh;
  double   q_in_mAh;
  double   e_out_mWh;
  double   e_in_mWh;
  uint32_t elapsed_s;
  const char *range_label;
  const char *state;     // "IDLE" | "CHARGE" | "DISCHARGE"
};

void begin();
void loop();
bool consumeResetRequest();
void publish(const Sample &s);

// Short status string for the TFT header: dotted-quad IP when associated,
// "wifi..." otherwise. Points at a static buffer — do not free.
const char *statusLabel();

}  // namespace webui
