#pragma once
#include <Arduino.h>
#include <MyLD2410.h>

// One LD2410C. Its serial port must be started before begin().
struct Radar {
  static const uint32_t TIMEOUT_MS = 1000; // no frame for this long = offline
  static const uint32_t REBOOT_MS = 3000;  // time the radar needs to come back after a reboot
  static const uint32_t AUTO_POLL_MS = 1000;      // how often to ask if auto threshold is done
  static const uint32_t AUTO_TIMEOUT_MS = 120000; // give up waiting for auto threshold
  static const uint32_t ENHANCED_RETRY_MS = 5000; // retry enhanced mode this often if it didn't stick
  static const byte GATES = 9;

  int outPin;    // radar's OUT (presence) -> ESP input
  int mirrorPin; // ESP output that copies OUT while the system is active
  MyLD2410 dev;
  String firmware; // read once at begin(); empty if the radar didn't answer

  // Settings as last read from the radar (0 = unknown).
  byte holdSeconds = 0;  // "no-one window": presence is held this long after the last detection
  byte resolutionCm = 0; // gate size: 75 or 20
  byte movingThresholds[GATES] = {};     // per gate, 0..100
  byte stationaryThresholds[GATES] = {};
  byte thresholdCount = 0; // gates with known thresholds (0 until read)
  bool autoRunning = false; // auto threshold in progress

  // Requested by the web page (written from the web server task, applied in loop()).
  volatile int requestedHold = -1;
  volatile int requestedResolutionCm = -1;
  volatile int requestedMovingThreshold[GATES];     // -1 = no request
  volatile int requestedStationaryThreshold[GATES];
  volatile int requestedAutoDelay = -1; // seconds to leave the room before auto threshold scans
  uint32_t reinitAtMs = 0;
  uint32_t autoStartedMs = 0, autoNextPollMs = 0;
  uint32_t enhancedRetryAtMs = 0;

  Radar(Stream &uart, int outPin, int mirrorPin) : outPin(outPin), mirrorPin(mirrorPin), dev(uart) {
    for (byte gate = 0; gate < GATES; gate++) requestedMovingThreshold[gate] = requestedStationaryThreshold[gate] = -1;
  }

  void begin() {
    pinMode(outPin, INPUT_PULLDOWN); // reads 0 if no radar is wired
    pinMode(mirrorPin, OUTPUT);
    if (!dev.begin()) return;
    if (dev.requestFirmware()) firmware = dev.getFirmware();
    readParameters();
    if (dev.requestResolution()) resolutionCm = dev.getResolution();
    dev.enhancedMode(true); // per-gate energies
  }

  // Hold time and per-gate thresholds (one radar query).
  void readParameters() {
    if (!dev.requestParameters()) return;
    holdSeconds = dev.getNoOneWindow();
    copyThresholds();
  }

  // From the library's copy, which it refreshes after every threshold change.
  void copyThresholds() {
    const MyLD2410::ValuesArray &moving = dev.getMovingThresholds();
    const MyLD2410::ValuesArray &stationary = dev.getStationaryThresholds();
    thresholdCount = min(moving.N, stationary.N) + 1; // N = highest gate index
    for (byte gate = 0; gate < thresholdCount; gate++) {
      movingThresholds[gate] = moving.values[gate];
      stationaryThresholds[gate] = stationary.values[gate];
    }
  }

  void poll() {
    while (dev.check() != MyLD2410::FAIL) {}
  }

  // Call from loop(), not from a web handler: each config command can block for up to ~2 s.
  void applyRequests() {
    if (requestedHold >= 0) {
      if (dev.setNoOneWindow(requestedHold)) holdSeconds = dev.getNoOneWindow();
      requestedHold = -1;
    }
    if (requestedResolutionCm > 0) {
      // A new resolution only takes effect after a reboot; begin() re-reads everything after it.
      if (dev.setResolution(requestedResolutionCm == 20) && dev.requestReboot()) reinitAtMs = millis() + REBOOT_MS;
      requestedResolutionCm = -1;
    }
    for (byte gate = 0; gate < GATES; gate++) {
      int moving = requestedMovingThreshold[gate];
      if (moving >= 0) {
        requestedMovingThreshold[gate] = -1;
        if (dev.setMovingThreshold(gate, moving)) copyThresholds();
      }
      int stationary = requestedStationaryThreshold[gate];
      if (stationary >= 0) {
        requestedStationaryThreshold[gate] = -1;
        if (dev.setStationaryThreshold(gate, stationary)) copyThresholds();
      }
    }
    int autoDelay = requestedAutoDelay;
    if (autoDelay >= 0) {
      requestedAutoDelay = -1;
      if (dev.autoThresholds(autoDelay)) {
        autoRunning = true;
        autoStartedMs = millis();
        autoNextPollMs = millis() + AUTO_POLL_MS;
      }
    }
    // Each status query briefly pauses radar data; fine, the room is meant to be empty.
    if (autoRunning && millis() >= autoNextPollMs) {
      autoNextPollMs = millis() + AUTO_POLL_MS;
      if (dev.getAutoStatus() == AutoStatus::COMPLETED || millis() - autoStartedMs > AUTO_TIMEOUT_MS) {
        autoRunning = false;
        readParameters(); // the new thresholds
      }
    }
    // Without enhanced mode there are no per-gate energies; the switch can fail (e.g. right after a reboot).
    if (online() && !dev.inEnhancedMode() && !autoRunning && !reinitAtMs && millis() >= enhancedRetryAtMs) {
      enhancedRetryAtMs = millis() + ENHANCED_RETRY_MS;
      dev.enhancedMode(true);
    }
    if (reinitAtMs && millis() >= reinitAtMs) {
      reinitAtMs = 0;
      begin();
    }
  }

  bool online() {
    unsigned long ts = dev.getTimestamp();
    return ts && millis() - ts < TIMEOUT_MS;
  }

  bool out() const { return digitalRead(outPin); }
};
