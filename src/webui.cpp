#include "webui.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>

#include "secrets.h"
#include "webui_html.h"

namespace webui {

static AsyncWebServer    server(80);
static AsyncWebSocket    ws("/ws");
static volatile bool     reset_requested = false;
static bool              server_started  = false;
static uint32_t          last_wifi_log_ms = 0;

static void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *,
                      AwsEventType, void *, uint8_t *, size_t) {
  // Stateless — server only pushes; nothing to handle from clients.
}

static void startServer() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *r = req->beginResponse(
      200, "text/html; charset=utf-8",
      (const uint8_t *)WEBUI_INDEX_HTML, sizeof(WEBUI_INDEX_HTML) - 1);
    r->addHeader("Cache-Control", "no-cache");
    req->send(r);
  });

  server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
    reset_requested = true;
    req->send(200, "text/plain", "ok");
  });

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "not found");
  });

  server.begin();
  server_started = true;

  if (MDNS.begin("juicemeter")) {
    MDNS.addService("http", "tcp", 80);
  }

  Serial.printf("[webui] http://%s/  (http://juicemeter.local/)\n",
                WiFi.localIP().toString().c_str());
}

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("juicemeter");
  WiFi.setSleep(false);   // TCP latency > power on a bench tool
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[webui] connecting to \"%s\" ...\n", WIFI_SSID);
}

void loop() {
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up && !server_started) {
    startServer();
  } else if (!up && !server_started) {
    const uint32_t now = millis();
    if (now - last_wifi_log_ms > 5000) {
      last_wifi_log_ms = now;
      Serial.printf("[webui] waiting for WiFi (status=%d)\n", (int)WiFi.status());
    }
  }
  if (server_started) ws.cleanupClients();
}

const char *statusLabel() {
  static char buf[24];
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
    return buf;
  }
  return "wifi...";
}

bool consumeResetRequest() {
  if (reset_requested) { reset_requested = false; return true; }
  return false;
}

void publish(const Sample &s) {
  if (!server_started || ws.count() == 0) return;

  char buf[512];
  int n = snprintf(buf, sizeof(buf),
    "{\"t\":%lu,\"state\":\"%s\",\"range\":\"%s\","
    "\"v_bat\":%.4f,\"v_dev\":%.4f,\"v_bus\":%.4f,"
    "\"i_mA\":%.4f,\"p_mW\":%.4f,"
    "\"i_peak_mA\":%.4f,\"p_peak_mW\":%.4f,"
    "\"q_out_mAh\":%.4f,\"q_in_mAh\":%.4f,"
    "\"e_out_mWh\":%.4f,\"e_in_mWh\":%.4f}",
    (unsigned long)s.elapsed_s, s.state, s.range_label,
    s.v_bat, s.v_dev, s.v_bus,
    s.i_mA, s.p_mW,
    s.i_peak_mA, s.p_peak_mW,
    s.q_out_mAh, s.q_in_mAh,
    s.e_out_mWh, s.e_in_mWh);
  if (n > 0 && n < (int)sizeof(buf)) ws.textAll(buf, n);
}

}  // namespace webui
