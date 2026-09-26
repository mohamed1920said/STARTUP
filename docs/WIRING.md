# Wiring Diagram

## 1. Central Gateway — ESP32-S3 + RFM95W

```
RFM95W (SX1276):
  RST  ── GPIO 14
  NSS  ── GPIO 18
  SCK  ── GPIO 12
  MOSI ── GPIO 11
  MISO ── GPIO 13
  DIO0 ── GPIO 2
  DIO1 ── NC

SPI bus (shared):
  SCK  → 12
  MISO → 13
  MOSI → 11
  CS   → 18

Power:
  RFM95W VCC  → 3.3V
  RFM95W GND  → GND

Weather station:
  Rain gauge pulse  ── GPIO 6
  Anemometer pulse  ── GPIO 3
  Wind vane analog ── GPIO 10
  DHT11 data        ── GPIO 7
  LDR analog        ── GPIO 9
  Battery divider   ── GPIO 8
  BMP280 SDA        ── GPIO 4
  BMP280 SCL        ── GPIO 5
```

Central PCB connector map (`hardware/central_gateway`, revision v1.1):

| Connector | Connection |
|---|---|
| J2 rain RJ11 | pins 1/2/5/6 NC, pin 3 GND, pin 4 rain signal |
| J3 wind RJ11 | pin 1/6 NC, pin 2 vane, pin 3 GND, pin 4 wind-speed switch, pin 5 GND |
| J4 DHT screw terminal | 3.3 V, data, GND |
| J5 BMP280 screw terminal | 3.3 V, SDA, SCL, GND |
| J6 LDR screw terminal | LDR signal, GND |
| J9 SMA | RFM95W 868 MHz antenna |
| J10 locking 2-pin | enclosure-mounted normally-open setup/reset button |

The RFM95W LoRa module is soldered directly to the PCB; do not extend its SPI
bus through a screw terminal. J2/J3 numbering is the PCB contact numbering, so
continuity-test the supplied modular cables rather than relying on wire colors.
There is no reset switch on the PCB: holding the enclosure button connected to
J10 for five seconds requests Wi-Fi configuration reset.

The pin map above is the authoritative map compiled in
`src/central/main.cpp`. Do not use earlier TTGO gateway diagrams for the
ESP32-S3 central device.

## 2. Sensor Node — TTGO LoRa32 v2.1

```
Onboard LoRa (SX1276) — no external wiring needed.

Capacitive moisture sensor:
  VCC ── 3.3V
  GND ── GND
  SIG ── GPIO 34 (ADC)

DS18B20 temperature:
  VCC ── 3.3V (with 4.7kΩ pull-up to data)
  GND ── GND
  DAT ── GPIO 4

Battery (single Li-ion 18650):
  BAT+ ── GPIO 35 (via voltage divider, see below)
  BAT- ── GND

  Voltage divider (onboard TTGO):
    R1 = 100kΩ (BAT+ ── GPIO 35)
    R2 = 100kΩ (GPIO 35 ── GND)
    Formula: V_bat = ADC_voltage * 2.0
```

## 3. Actuator Node — TTGO LoRa32 v2.1 + TB6612FNG + Latching Valve

```
Onboard LoRa — same as sensor.

TB6612FNG Motor Driver:
  ┌──────────────────────────────────────┐
  │  TB6612FNG     │  GPIO / Connection  │
  ├────────────────┼──────────────────────┤
  │ AIN1           │  GPIO 2             │
  │ AIN2           │  GPIO 4             │
  │ PWMA           │  GPIO 12            │
  │ STBY           │  GPIO 17            │
  │ VMOT           │  Battery + (4.5-6V) │
  │ VCC            │  3.3V               │
  │ GND            │  GND                │
  │ A01, A02       │  Latching valve     │
  └──────────────────────────────────────┘

Latching electrovane:
  Red  wire ── TB6612 A01
  Blue wire ── TB6612 A02
  (Polarity reversed for open/close via 30ms pulse)

Valve-open feedback (required for verified control and AI labels):
  Dry-contact signal ── GPIO 3 (INPUT_PULLUP)
  Contact CLOSED to GND means valve OPEN.
  Contact OPEN means valve CLOSED.

  The default actuator build disables this input and is manual-only. After wiring
  and dry-bench verification, add `-DACTUATOR_VALVE_FB_PIN=3` to the actuator build
  flags to enable measured feedback.

Battery (single Li-ion 18650):
  BAT+ ── GPIO 35 (same divider as sensor: 100k+100k)
  BAT- ── GND

  Note: TB6612 VMOT is also powered from battery (4.5-6V range).
  Use a boost converter if battery voltage drops below 4.5V.
```

### Optional hydraulic sensors on the actuator node

These inputs are compiled but disabled by default so unconnected pins cannot
produce false data:

```
Flow meter pulse       ── GPIO 13
Pressure analog        ── GPIO 34
Tank-level analog      ── GPIO 36
Pump-current analog    ── GPIO 37
```

After the signal-conditioning circuits and sensors are connected, add
`-DENABLE_HYDRAULIC_SENSORS=1` to the `actuator_node` build flags in
`platformio.ini`. Adjust `FLOW_PULSES_PER_LITER`, pressure full scale, tank
calibration, and current-sensor full scale in `src/actuator/main.cpp` to match
the actual sensor datasheets. Analog inputs must never exceed 3.3 V.

GPIO 23 is reserved for the TTGO board's onboard SX1276 reset. GPIO 26 is
the LoRa interrupt, and GPIO 32/33 are the LoRa DIO2/DIO1 signals; do not use
those pins for the valve driver or hydraulic sensors.

## 4. Battery Voltage Divider (all nodes)

```
All nodes use GPIO 35 for battery measurement with the onboard TTGO
voltage divider (or external if using bare ESP32):

      BAT+ (3.7-4.2V)
           │
          ┌┴┐
          │ │ R1 = 100kΩ
          └┬┘
           ├──── GPIO 35 (ADC)
          ┌┴┐
          │ │ R2 = 100kΩ
          └┬┘
           │
          GND

Division ratio: V_GPIO35 = V_bat * R2 / (R1 + R2) = V_bat / 2

ADC formula in code:
  V_bat = (analogRead(35) / 4095.0) * 3.3 * 2.0

Pilot actuator OPEN gate:
  OPEN is rejected below 3.4 V or above an implausible 5.0 V reading.
  CLOSE remains allowed. Validate the divider against a calibrated meter before
  relying on this gate; a wrong divider ratio can produce a misleading reading.

For TTGO boards, the divider is pre-soldered (R1=100k, R2=100k).
For bare ESP32-S3, add external 100k+100k divider.
```

## 5. LoRa Antenna

```
All nodes:
  Use a 1/4-wave whip antenna tuned for 868 MHz (~8.6 cm).
  SMA or u.FL connector depending on board variant.
  Place antennas at least 30cm apart during testing.
```
