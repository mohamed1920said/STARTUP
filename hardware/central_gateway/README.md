# AMR Central Gateway PCB

KiCad 10 carrier board for the AMR central gateway firmware. The board accepts an
ESP32-S3-DevKitC-1-N8R8, adds a soldered RFM95W 868 MHz radio, protected sensor
interfaces, regulated 7-18 V input, an enclosure-button connector, service UART,
and test points.

## Status

- Board revision: `v1.1-prototype`
- Board size: 130 mm x 77 mm, including the ESP32 antenna clearance notch
- Copper layers: 4
- Modular jack footprint: Wuerth Elektronik `615006138421`, manufacturer geometry
- Manufacturing outputs: `manufacturing/central_gateway-fabrication.zip`

The saved DRC result and manufacturing files must be regenerated after every
generator change. This remains a prototype, not a certified production product.
Before sale or outdoor deployment, complete an independent electrical review, RF
tuning, EMC/pre-compliance, surge/EFT/ESD, temperature, and enclosure-ingress tests.

## Physical connector architecture

- J2 is a six-position RJ11-compatible modular jack for the tipping-bucket rain gauge.
- J3 is one six-position RJ11-compatible modular jack for the combined wind cable: wind vane
  plus cup-anemometer reed switch.
- J4 is a three-pin screw terminal for the DHT sensor.
- J5 is a four-pin screw terminal for the BMP280/I2C sensor.
- J6 is a two-pin screw terminal for a passive LDR.
- U2, the RFM95W LoRa module, is soldered directly to the PCB. Its SPI signals are
  intentionally not routed through a screw terminal. J9 is its SMA antenna port.
- The setup/reset button is mounted in the enclosure and wired to J10. There is no
  pushbutton mounted on the PCB.

## Main design choices

- Replaceable ESP32-S3 DevKitC-1 for prototype programming and repair.
- RFM95W-868S2 with a short SMA feed for Tunisia's 868 MHz LoRa deployment.
- 7-18 V input with resettable fuse, SMBJ18A surge clamp, SS34 reverse-polarity
  protection, and R-78E5.0-1.0 5 V regulator.
- AP2112K 3.3 V rail for radio and sensors.
- ESD protection and series resistance on all external sensor signals.
- 100 kOhm / 100 kOhm battery divider matching `WeatherStation.cpp`.
- Dedicated inner ground plane plus bottom ground pour.
- No carrier PCB beneath the ESP32-S3 module antenna.

## Layer stack

| Layer | Use |
|---|---|
| F.Cu | Components, local signals, RF feed |
| In1.Cu | Continuous GND plane |
| In2.Cu | DevKit fan-out and crossing signals |
| B.Cu | Power/signal routing with GND pour |

Order a 1.6 mm, four-layer FR-4 board with at least 1 oz external copper. The
0.45 mm antenna trace is only a prototype starting width. Recalculate the 50 ohm
RF geometry using the fabricator's actual stack-up before production.

## Connector pinout

Pin 1 is the square pad. Modular-jack pin numbers below are PCB contact numbers,
not cable colors; verify the purchased cable continuity before connecting it.

| Connector | PCB pin order | Purpose |
|---|---|---|
| J1 | 1 `VIN_RAW`, 2 `GND` | 7-18 V DC input |
| J2 | 1 NC, 2 NC, 3 `GND`, 4 `RAIN_RAW`, 5 NC, 6 NC | Rain gauge; center pair only |
| J3 | 1 NC, 2 `VANE_RAW`, 3 `GND`, 4 `WIND_RAW`, 5 `GND`, 6 NC | Combined vane + anemometer 6P4C cable |
| J4 | 1 `3V3`, 2 `DHT_RAW`, 3 `GND` | DHT11/DHT22 |
| J5 | 1 `3V3`, 2 `SDA`, 3 `SCL`, 4 `GND` | BMP280 / I2C sensor |
| J6 | 1 `LDR_RAW`, 2 `GND` | Passive LDR; PCB provides 10 kOhm pull-up |
| J7 | 1 `BAT+`, 2 `GND` | 0-6.0 V measurement input only |
| J8 | 1 `3V3`, 2 `GND`, 3 `UART_TX`, 4 `UART_RX` | 3.3 V service UART |
| J9 | center RF, shell `GND` | SMA for tuned 868 MHz antenna |
| J10 | 1 `SETUP_RESET`, 2 `GND` | Enclosure-mounted normally-open button |

Do not power the system through J7 and never apply more than 6.0 V to it. J8 is
3.3 V logic and is not 5 V tolerant. Never apply external voltage to J10: the
panel button must only short pins 1 and 2.

## Weather-kit electrical behavior

- Rain: each bucket tip closes the reed switch and represents 0.2794 mm.
- Wind speed: one closure per second represents approximately 2.4 km/h, or
  0.667 m/s.
- Wind direction: the vane exposes a 16-position resistor network. The firmware
  lookup is calculated for the PCB's 3.3 V, 10 kOhm pull-up; verify and calibrate
  the ADC centers on assembled production hardware.

## Firmware-to-board map

| Function | ESP32-S3 GPIO | PCB net |
|---|---:|---|
| LoRa NSS | 18 | `LORA_NSS` |
| LoRa DIO0 | 2 | `LORA_DIO0` |
| LoRa reset | 14 | `LORA_RST` |
| LoRa SCK / MOSI / MISO | 12 / 11 / 13 | `LORA_SCK` / `LORA_MOSI` / `LORA_MISO` |
| Rain | 6 | `RAIN_GPIO` |
| Wind pulse | 3 | `WIND_GPIO` |
| Wind vane ADC | 10 | `VANE_ADC` |
| DHT data | 7 | `DHT_GPIO` |
| LDR ADC | 9 | `LDR_ADC` |
| Battery ADC | 8 | `BATT_ADC` |
| I2C SDA / SCL | 4 / 5 | `I2C_SDA_GPIO` / `I2C_SCL_GPIO` |
| Wi-Fi setup reset | 0 | `SETUP_RESET` |

Hold the enclosure button for five seconds to request the Wi-Fi setup reset.
GPIO0 is also a boot-strapping pin, so do not hold the button while applying
power or resetting the DevKit unless firmware-download mode is intended.

## Assembly and verification

- TP1: +5 V
- TP2: +3.3 V peripheral rail
- TP3: GND
- TP4: LoRa DIO1

Before fitting U1 or U2, power the carrier from a current-limited bench supply and
verify TP1 and TP2. Fit the ESP32 module, then fit the RFM95W and connect its
antenna before transmitting. Continuity-test both weather cables to confirm their
contact order before plugging them into J2/J3.

Observe diode polarity. D1 cathode connects to `VIN_FUSED`; D2 anode connects to
`VIN_FUSED` and cathode to `VIN_PROTECTED`. For D3-D10, confirm the purchased
part's polarity and marking against the schematic and datasheet.

## Regeneration and verification

From the repository root:

```powershell
python hardware\central_gateway\generate_kicad.py
& 'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe' pcb upgrade hardware\central_gateway\central_gateway.kicad_pcb
& 'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe' pcb drc --refill-zones --save-board --severity-all --format report --output hardware\central_gateway\central_gateway-drc.rpt hardware\central_gateway\central_gateway.kicad_pcb
```

The routed PCB and deterministic generator are authoritative. The legacy
schematic is a compact circuit-review reference and is converted by KiCad on
first open.

## Production checklist

1. Verify the exact DevKit, Wuerth jacks, screw terminals, JST header, and panel
   button against purchased samples and the final enclosure.
2. Confirm cable pinout and add keying/labels so rain and wind plugs cannot be
   exchanged in the field.
3. Tune RF geometry for the chosen stack-up and test the installed antenna/VNA
   performance inside the final enclosure.
4. Validate regulator temperature and brownout margin at maximum Wi-Fi/LoRa load.
5. Test all external ports for the required IEC ESD, EFT, and surge environment.
6. Use a UV-resistant IP65/IP67 enclosure, strain relief, suitable glands or
   sealed panel couplers, conformal coating where appropriate, and fused power.
7. Run a pilot build and production functional-test fixture before volume manufacture.
