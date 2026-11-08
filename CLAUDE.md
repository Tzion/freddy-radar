# Horse Head Presence Sensor — ESP32-S2 mini + LD2410C

## Goal
Interactive installation (horse doll head) detecting people at a few meters, day and night.
ESP32-S2 mini reads HLK-LD2410C 24GHz radar(s) and serves a web page to a nearby phone.

## Hardware
- MCU: Wemos/Lolin ESP32-S2 mini v1.0.0 (native USB → `Serial` = USB CDC)
- Sensor: HLK-LD2410C (5V power, 3.3V UART logic, 256000 baud 8N1, ±60° cone, ~5–6m, 0.75m gates)
- Phase 1: TWO radars on hardware UARTs (`Serial1`, `Serial0`)

## Wiring (phase 1)
| LD2410C | Radar 0 (`Serial1`) | Radar 1 (`Serial0`) |
|---|---|---|
| VCC | VBUS/5V | VBUS/5V |
| GND | GND | GND |
| TX | GPIO16 (RX) | GPIO11 (RX) |
| RX | GPIO17 (TX) | GPIO12 (TX) |
| OUT | GPIO18 | GPIO13 |
| ESP output (mirrors OUT) | GPIO34 | GPIO7 |
- On/off button (momentary): switch GPIO38↔GND (INPUT_PULLUP), LED ring GPIO37↔GND (non-polarized). Single press = off, double = on; off holds GPIO34/7 LOW.
- 100µF + 100nF across radar VCC/GND. No level shifter.
- Avoid GPIO0/45/46 (strapping), 19/20 (USB), 15 (onboard LED).

## Stack
- PlatformIO, Arduino framework, `board = lolin_s2_mini`
- Radar lib: `MyLD2410` (supports LD2410C)
- Web: `ESPAsyncWebServer` + `AsyncTCP` (mathieucarbou forks)
- Web page: `data/index.html` served from LittleFS (`pio run -t uploadfs`, separate from `-t upload`)
- `Serial1.begin(256000, SERIAL_8N1, 16, 17);`

## Tasks
1. Read radar frames; print state (moving/stationary, distances, energies) to USB Serial.
2. Read OUT pin as backup presence signal.
3. WiFi soft-AP (SSID `freddy-galea`, pass `midbarXpress`), web server:
   - `/` mobile page, live status via WebSocket `/ws` (JSON push, 10 Hz)
   - Optional: config endpoints (max gate, sensitivity, no-one timeout) sent to radar via UART
4. Keep radar reading non-blocking; WiFi must not stall parsing.

## Future (design for it now)
- Up to 4 radars. S2 has only 2 hardware UARTs:
  - Radars 1–2: `Serial0`/`Serial1` (any pins via GPIO matrix)
  - Radars 3–4: lower baud in HLKRadarTool app (≤115200) + EspSoftwareSerial RX-only, or OUT pin only
- Abstract radar as a class/array so adding sensors is config-only.
- Total 5V draw ~0.5A with 4 radars.

## Notes
- Radar sees through plastic/fabric, not metal. Mount rigidly (head movement = false motion).
- Initial config can be done via Bluetooth (HLKRadarTool app).
- User prefers concise output and high-level overview before details.
