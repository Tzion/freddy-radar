// Horse Head presence sensor — ESP32-S2 mini + HLK-LD2410C
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_netif.h>
#include <dhcpserver/dhcpserver.h>
#include <stdarg.h>
#include "radar.h"

static const char *AP_SSID = "freddy-galea";
static const char *AP_PASS = "midbarXpress";

static const uint32_t RADAR_BAUD = 256000;
static const uint32_t WS_PERIOD_MS = 100;  // push to browser at 10 Hz
static const uint32_t LOG_PERIOD_MS = 500; // USB serial log at 2 Hz
static const uint32_t LED_SLOW_BLINK_MS = 500; // half-period: a radar is not connected
static const uint32_t LED_FAST_BLINK_MS = 100; // half-period: a radar detects someone

// On/off button (momentary, to GND): single press = off, double press = on.
static const int BUTTON_PIN = 38;
static const int BUTTON_LED_PIN = 37; // LED ring: on while the system is active
static const uint32_t BUTTON_DEBOUNCE_MS = 30;
static const uint32_t DOUBLE_PRESS_MS = 500; // second press must start within this of the first

// Off = mirror outputs held LOW. Set by the button (loop) and the web page (web task).
static volatile bool systemActive = true;

// Add radars here ({uart, OUT pin, mirror output pin}) and start their UART in setup().
// S2 has 2 hardware UARTs: Serial0 and Serial1.
static Radar radars[] = {
    {Serial1, 18, 34}, // left
    {Serial0, 13, 7},  // right
};
static const size_t NUM_RADARS = sizeof(radars) / sizeof(radars[0]);

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// Worst case per radar is ~380 bytes (flags, settings, distances, 2x9 energies, 2x9 thresholds).
static char json[512 * NUM_RADARS];
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

static void addBytes(const char *key, const byte *values, byte count) {
  add(",\"%s\":[", key);
  for (byte i = 0; i < count; i++) add("%s%u", i ? "," : "", values[i]);
  add("]");
}

static void buildJson() {
  jsonLen = 0;
  add("{\"active\":%d,\"radars\":[", systemActive);
  for (size_t i = 0; i < NUM_RADARS; i++) {
    Radar &r = radars[i];
    MyLD2410 &sensor = r.dev;
    add("%s{\"id\":%u,\"out\":%d,\"fw\":\"%s\",\"hold\":%u,\"res\":%u", i ? "," : "", (unsigned)i, r.out(),
        r.firmware.c_str(), r.holdSeconds, r.resolutionCm);
    add(",\"auto\":%d", r.autoRunning);
    addBytes("mTh", r.movingThresholds, r.thresholdCount);
    addBytes("sTh", r.stationaryThresholds, r.thresholdCount);
    if (!r.online()) {
      add(",\"online\":0}");
      continue;
    }
    add(",\"online\":1,\"presence\":%d,\"moving\":%d,\"mDist\":%lu,\"mSig\":%u"
        ",\"stationary\":%d,\"sDist\":%lu,\"sSig\":%u",
        sensor.presenceDetected(), sensor.movingTargetDetected(), sensor.movingTargetDistance(), sensor.movingTargetSignal(),
        sensor.stationaryTargetDetected(), sensor.stationaryTargetDistance(), sensor.stationaryTargetSignal());
    addGates("mGates", sensor.getMovingSignals(), sensor.inEnhancedMode());
    addGates("sGates", sensor.getStationarySignals(), sensor.inEnhancedMode());
    add("}");
  }
  add("]}");
}

static void logStatus() {
  for (size_t i = 0; i < NUM_RADARS; i++) {
    Radar &r = radars[i];
    MyLD2410 &sensor = r.dev;
    if (!r.online()) {
      Serial.printf("[R%u] offline  OUT=%d\n", (unsigned)i, r.out());
      continue;
    }
    Serial.printf("[R%u] %s %-22s OUT=%d  mov %4lucm e%3u  sta %4lucm e%3u\n", (unsigned)i,
                  sensor.inEnhancedMode() ? "enh  " : "basic", sensor.statusString(), r.out(),
                  sensor.movingTargetDistance(), sensor.movingTargetSignal(), sensor.stationaryTargetDistance(),
                  sensor.stationaryTargetSignal());
  }
}

// Don't advertise the ESP32 as gateway/router in DHCP. Phones then keep mobile
// data for internet and reach 192.168.4.x over WiFi (iOS otherwise routes around it).
static void disableApGateway() {
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  dhcps_offer_t offer = 0; // clear OFFER_ROUTER
  esp_netif_dhcps_stop(ap);
  if (esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &offer, sizeof(offer)) != ESP_OK)
    Serial.println("DHCP: failed to disable gateway offer");
  esp_netif_dhcps_start(ap);
}

// Onboard LED: fast blink = any radar detects, slow blink = a radar is offline, steady = all connected.
// Debounced; a press counts once the double-press window after the first press has closed.
static void updateButton(uint32_t now) {
  static bool reading = false, pressed = false;
  static uint32_t readingChangedMs = 0, firstPressMs = 0;
  static int presses = 0;

  bool raw = digitalRead(BUTTON_PIN) == LOW;
  if (raw != reading) {
    reading = raw;
    readingChangedMs = now;
  }
  if (reading != pressed && now - readingChangedMs >= BUTTON_DEBOUNCE_MS) {
    pressed = reading;
    if (pressed && presses++ == 0) firstPressMs = now;
  }
  if (presses && !pressed && now - firstPressMs >= DOUBLE_PRESS_MS) {
    systemActive = presses >= 2;
    Serial.printf("Button: %s -> %s\n", presses >= 2 ? "double" : "single", systemActive ? "ON" : "OFF");
    presses = 0;
  }
}

static void updateMirrorOutputs() {
  for (Radar &r : radars) digitalWrite(r.mirrorPin, systemActive && r.out());
}

static void updateLed(uint32_t now) {
  bool anyDetecting = false, allOnline = true;
  for (Radar &r : radars) {
    bool online = r.online();
    allOnline &= online;
    anyDetecting |= online && (r.dev.presenceDetected() || r.out());
  }
  bool ledOn = anyDetecting ? (now / LED_FAST_BLINK_MS) % 2
             : !allOnline   ? (now / LED_SLOW_BLINK_MS) % 2
                            : true;
  digitalWrite(LED_BUILTIN, ledOn);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_LED_PIN, OUTPUT);
  while (!Serial && millis() < 1500) delay(10); // let USB CDC enumerate if a host is attached

  // WiFi first: radar begin() can block for seconds if a sensor is missing.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  disableApGateway();
  Serial.printf("AP %s  http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  if (!LittleFS.begin()) Serial.println("LittleFS mount failed - run: pio run -t uploadfs");
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  // POST /api/config?radar=0& one of:
  //   hold=10 | res=20 (reboots the radar) | gate=3&moving=40 | gate=3&stationary=30 | auto=10 (s to leave)
  // POST /api/power?on=0|1
  server.on("/api/power", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("on")) {
      req->send(400, "text/plain", "missing on");
      return;
    }
    systemActive = req->getParam("on")->value().toInt() != 0;
    req->send(202);
  });
  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {
    int id = req->hasParam("radar") ? req->getParam("radar")->value().toInt() : -1;
    if (id < 0 || id >= (int)NUM_RADARS) {
      req->send(400, "text/plain", "bad radar");
      return;
    }
    Radar &r = radars[id];
    if (req->hasParam("hold")) r.requestedHold = constrain(req->getParam("hold")->value().toInt(), 0, 255);
    if (req->hasParam("res")) r.requestedResolutionCm = req->getParam("res")->value().toInt() == 20 ? 20 : 75;
    if (req->hasParam("auto")) r.requestedAutoDelay = constrain(req->getParam("auto")->value().toInt(), 1, 255);
    int gate = req->hasParam("gate") ? req->getParam("gate")->value().toInt() : -1;
    if (gate >= 0 && gate < Radar::GATES) {
      if (req->hasParam("moving"))
        r.requestedMovingThreshold[gate] = constrain(req->getParam("moving")->value().toInt(), 0, 100);
      if (req->hasParam("stationary"))
        r.requestedStationaryThreshold[gate] = constrain(req->getParam("stationary")->value().toInt(), 0, 100);
    }
    req->send(202);
  });
  server.addHandler(&ws);
  server.begin();

  Serial1.begin(RADAR_BAUD, SERIAL_8N1, 16, 17); // radar 0: RX=16, TX=17
  Serial0.begin(RADAR_BAUD, SERIAL_8N1, 11, 12); // radar 1: RX=11, TX=12
  for (Radar &r : radars) r.begin();
}

void loop() {
  for (Radar &r : radars) {
    r.applyRequests();
    r.poll();
  }

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

  updateButton(now);
  updateMirrorOutputs();
  digitalWrite(BUTTON_LED_PIN, systemActive);
  updateLed(now);

  delay(1); // let the idle task run; UART buffer holds ~10ms of radar data
}
