// Horse Head presence sensor — ESP32-S2 mini + HLK-LD2410C
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <stdarg.h>
#include "radar.h"

static const char *AP_SSID = "freddy-galea";
static const char *AP_PASS = "midbarXpress";

static const uint32_t RADAR_BAUD = 256000;
static const uint32_t WS_PERIOD_MS = 100;  // push to browser at 10 Hz
static const uint32_t LOG_PERIOD_MS = 500; // USB serial log at 2 Hz

// Add radars here ({uart, OUT pin}) and start their UART in setup().
// S2 has 2 hardware UARTs: Serial0 and Serial1.
static Radar radars[] = {
    {Serial1, 18},
};
static const size_t NUM_RADARS = sizeof(radars) / sizeof(radars[0]);

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// Worst case per radar is ~230 bytes (flags, distances, 2x9 gate energies).
static char json[320 * NUM_RADARS];
static size_t jsonLen;

static void add(const char *fmt, ...) {
  if (jsonLen >= sizeof(json)) return;
  va_list ap;
  va_start(ap, fmt);
  jsonLen += vsnprintf(json + jsonLen, sizeof(json) - jsonLen, fmt, ap);
  va_end(ap);
}

static void addGates(const char *key, const MyLD2410::ValuesArray &v, bool valid) {
  add(",\"%s\":[", key);
  const char *sep = "";
  if (valid) v.forEach([&](byte e) { add("%s%u", sep, e); sep = ","; });
  add("]");
}

static void buildJson() {
  jsonLen = 0;
  add("{\"radars\":[");
  for (size_t i = 0; i < NUM_RADARS; i++) {
    Radar &r = radars[i];
    MyLD2410 &d = r.dev;
    add("%s{\"id\":%u,\"out\":%d", i ? "," : "", (unsigned)i, r.out());
    if (!r.online()) {
      add(",\"online\":0}");
      continue;
    }
    add(",\"online\":1,\"presence\":%d,\"moving\":%d,\"mDist\":%lu,\"mSig\":%u"
        ",\"stationary\":%d,\"sDist\":%lu,\"sSig\":%u",
        d.presenceDetected(), d.movingTargetDetected(), d.movingTargetDistance(), d.movingTargetSignal(),
        d.stationaryTargetDetected(), d.stationaryTargetDistance(), d.stationaryTargetSignal());
    addGates("mGates", d.getMovingSignals(), d.inEnhancedMode());
    addGates("sGates", d.getStationarySignals(), d.inEnhancedMode());
    add("}");
  }
  add("]}");
}

static void logStatus() {
  for (size_t i = 0; i < NUM_RADARS; i++) {
    Radar &r = radars[i];
    MyLD2410 &d = r.dev;
    if (!r.online()) {
      Serial.printf("[R%u] offline  OUT=%d\n", (unsigned)i, r.out());
      continue;
    }
    Serial.printf("[R%u] %-22s OUT=%d  mov %4lucm e%3u  sta %4lucm e%3u\n", (unsigned)i, d.statusString(), r.out(),
                  d.movingTargetDistance(), d.movingTargetSignal(), d.stationaryTargetDistance(),
                  d.stationaryTargetSignal());
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 1500) delay(10); // let USB CDC enumerate if a host is attached

  // WiFi first: radar begin() can block for seconds if a sensor is missing.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("AP %s  http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  if (!LittleFS.begin()) Serial.println("LittleFS mount failed - run: pio run -t uploadfs");
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.addHandler(&ws);
  server.begin();

  Serial1.begin(RADAR_BAUD, SERIAL_8N1, 16, 17); // radar 0: RX=16, TX=17
  for (Radar &r : radars) r.begin();
}

void loop() {
  for (Radar &r : radars) r.poll();

  uint32_t now = millis();

  static uint32_t lastWs = 0;
  if (now - lastWs >= WS_PERIOD_MS) {
    lastWs = now;
    if (ws.count() && ws.availableForWriteAll()) {
      buildJson();
      if (jsonLen < sizeof(json)) ws.textAll(json, jsonLen);
    }
  }

  static uint32_t lastLog = 0;
  if (now - lastLog >= LOG_PERIOD_MS) {
    lastLog = now;
    ws.cleanupClients();
    logStatus();
  }

  delay(1); // let the idle task run; UART buffer holds ~10ms of radar data
}
