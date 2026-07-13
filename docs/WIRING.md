# Wiring Diagram

## 1. Central Gateway — ESP32-S3 + RFM95W

```
RFM95W (SX1276):
  RST  ── GPIO 10
  NSS  ── GPIO 5
  SCK  ── GPIO 6
  MOSI ── GPIO 7
  MISO ── GPIO 8
  DIO0 ── GPIO 2
  DIO1 ── NC

SPI bus (shared):
  SCK  → 6
  MISO → 8
  MOSI → 7
  CS   → 5

Power:
  RFM95W VCC  → 3.3V
  RFM95W GND  → GND
```

## 2. Central Gateway — TTGO LoRa32 v2.1

```
Onboard LoRa (SX1276) — no external wiring needed.

SPI pins (board default):
  SCK  = 5
  MISO = 19
  MOSI = 27
  CS   = 18
  RST  = 14 (wired, not board default 23)
  IRQ  = 26
```

## 3. Sensor Node — TTGO LoRa32 v2.1

```
Onboard LoRa — same as above.

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

## 4. Actuator Node — TTGO LoRa32 v2.1 + TB6612FNG + Latching Valve

```
Onboard LoRa — same as sensor.

TB6612FNG Motor Driver:
  ┌──────────────────────────────────────┐
  │  TB6612FNG     │  GPIO / Connection  │
  ├────────────────┼──────────────────────┤
  │ AIN1           │  GPIO 2             │
  │ AIN2           │  GPIO 4             │
  │ PWMA           │  GPIO 23            │
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

Valve feedback (optional):
  Signal ── GPIO 3 (INPUT_PULLUP)

Battery (single Li-ion 18650):
  BAT+ ── GPIO 35 (same divider as sensor: 100k+100k)
  BAT- ── GND

  Note: TB6612 VMOT is also powered from battery (4.5-6V range).
  Use a boost converter if battery voltage drops below 4.5V.
```

## 5. Battery Voltage Divider (all nodes)

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

For TTGO boards, the divider is pre-soldered (R1=100k, R2=100k).
For bare ESP32-S3, add external 100k+100k divider.
```

## 6. LoRa Antenna

```
All nodes:
  Use a 1/4-wave whip antenna tuned for 868 MHz (~8.6 cm).
  SMA or u.FL connector depending on board variant.
  Place antennas at least 30cm apart during testing.
```
