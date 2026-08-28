# Electrical and Mechanical Interface Control

## Electronics architecture

Use a 3S protected battery pack, appropriately rated fuse, BTS7960/IBT-2 class H-bridge, TTGO LoRa32 node, AS5600, and a small I2C GPIO expander such as MCP23017 for redundant discrete inputs.

| Interface | Required implementation |
|---|---|
| Motor | 12 V 5840-31ZY, two power conductors, stall current measured before fuse/driver release |
| BTS7960 | RPWM/LPWM and both enable lines; hardware manual interlock removes enable |
| AS5600 | I2C, mounted on final output shaft, not motor shaft |
| OPEN/CLOSED reeds | Independent dry contacts through protected digital inputs |
| OPEN/CLOSED hard limits | Normally-closed contacts that interrupt the hazardous control direction in hardware and are also monitored |
| Manual mode | Normally-closed hardware driver-disable plus monitored auxiliary contact |
| Battery | 3S1P, 12.6 V maximum; BMS and fuse sized from measured current |
| LoRa | Antenna separated from motor, steel shaft, and high-current conductors |

## Firmware incompatibility

The existing actuator firmware uses a 30 ms TB6612 pulse and one binary feedback input. This mechanical actuator requires a continuous-drive state machine with:

- direction-specific PWM;
- AS5600 angle feedback and magnet diagnostics;
- independent OPEN/CLOSED reed inputs;
- independent normally-closed OPEN/CLOSED limits;
- manual-mode input and hardware inhibit;
- movement timeout, stall/current limit, and no-motion detection;
- calibrated open/closed angles stored in nonvolatile memory;
- commanded, measured, and confirmed state reported separately.

Until that firmware and the corresponding PCB/harness are implemented and tested, the motor must remain disconnected from the current actuator node.

## Harness rules

- Use a separately fused motor supply branch and a regulated logic branch.
- Join grounds at the driver power-entry star point.
- Twist the motor pair and add motor-terminal suppression appropriate to the driver.
- Use shielded or twisted I2C/sensor wiring where cable length requires it.
- Add reverse-polarity, surge, ESD, and undervoltage protection.
- Keep all external inputs within the TTGO board's 3.3 V limits.

## Mechanical hold interfaces

| Interface | Prototype assumption | Required final measurement |
|---|---:|---|
| Motor body | 58 mm reference envelope | diameter, gearbox length, mounting pattern |
| Motor shaft | 8.1 mm D-bore placeholder | diameter, flat depth, projection, tolerance |
| Valve stem socket | 14.3 x 10.3 mm placeholder | profile, across-flats, corner radius, height |
| 50 mm valve neck opening | 72 mm placeholder | neck/body OD and clamp landing width |
| 63 mm valve neck opening | 88 mm placeholder | neck/body OD and clamp landing width |
| Electronics modules | reference envelopes | exact board outline, holes, connectors, heatsink |

