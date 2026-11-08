# Freddy Radar

Flash firmware + web page (`data/`):

    pio run -t upload      # firmware (src/)
    pio run -t uploadfs    # web page (data/)

Then join WiFi `freddy-galea` and open http://192.168.4.1 (remember to turn off mobile-data on iPhone)

## Wiring

| LD2410C | Radar 0 (left) | Radar 1 (right) |
|---|---|---|
| VCC | 5V (VBUS) | 5V (VBUS) |
| GND | GND | GND |
| TX  | GPIO16 | GPIO11 |
| RX  | GPIO17 | GPIO12 |
| OUT | GPIO18 | GPIO13 |

- TX/RX cross over: radar TX → ESP RX pin, radar RX → ESP TX pin.
- 100µF + 100nF across each radar's VCC/GND. No level shifter needed.
- Radar 1 is optional; it shows as offline if not connected.

### Outputs

| Output | S2 mini | Signal |
|---|---|---|
| Left  | GPIO34 | Copy of radar 0 OUT (3.3V = presence) |
| Right | GPIO7  | Copy of radar 1 OUT |

Both held LOW while the system is OFF.

### On/off button (16 mm, momentary, LED ring)

| Button pin | S2 mini |
|---|---|
| Golden (switch), one pin | GPIO38 |
| Golden (switch), other pin | GND |
| Silver (LED ring), one pin | GPIO37 |
| Silver (LED ring), other pin | GND |

- Golden = switch, silver = LED. Neither pair is polarity-sensitive (the LED lights either way round).
- Single press = OFF, double press = ON (also an ON/OFF button on the web page). Starts ON after power-up.
- LED ring is on while the system is ON.
