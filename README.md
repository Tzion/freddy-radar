# Freddy Radar

Flash firmware + web page (`data/`):

    pio run -t upload      # firmware (src/)
    pio run -t uploadfs    # web page (data/)

Then join WiFi `freddy-galea` and open http://192.168.4.1 (remember to turn off mobile-data on iPhone)

## Wiring

| LD2410C | Radar 0 | Radar 1 |
|---|---|---|
| VCC | 5V (VBUS) | 5V (VBUS) |
| GND | GND | GND |
| TX  | GPIO16 | GPIO11 |
| RX  | GPIO17 | GPIO12 |
| OUT | GPIO18 | GPIO13 |

- TX/RX cross over: radar TX → ESP RX pin, radar RX → ESP TX pin.
- 100µF + 100nF across each radar's VCC/GND. No level shifter needed.
- Radar 1 is optional; it shows as offline if not connected.
