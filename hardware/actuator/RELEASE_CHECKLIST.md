# Prototype Release Checklist

Every item must be closed before production or sale.

## Measurements and design

- [ ] Real 5840-31ZY motor body, mounting, shaft, D-flat, and double-shaft projections measured.
- [ ] Real 50 mm valve body/neck, stem, travel, axial float, and clamp landing measured.
- [ ] Real 63 mm valve measured before releasing its clamp configuration.
- [ ] Valve breakaway/running torque measured in both directions, dry/wet, hot/cold, new/aged.
- [ ] Motor no-load, running, stall, and locked-rotor current measured at 9.0, 11.1, and 12.6 V.
- [ ] Gear tooth contact and 0.25-0.35 mm backlash verified over a full revolution.
- [ ] Clutch calibrated and tamper-marked; slip occurs before every protected failure mode.
- [ ] Shaft key/cross-pin, adapter bolts, bearing retention, and M8 stops added per drawings.

## Electronics and firmware

- [ ] BTS7960 or production-equivalent driver qualified at measured stall current and enclosure temperature.
- [ ] BMS, cell holder, wiring, connector, and fuse qualified for measured current.
- [ ] Continuous-drive actuator firmware implemented; TB6612 pulse firmware is not used.
- [ ] AS5600 plausibility, magnet loss, no-motion, reversed-motion, timeout, and stall faults tested.
- [ ] Both NC hard limits independently interrupt hazardous motion in hardware.
- [ ] Manual override independently disables the motor driver in hardware.
- [ ] Loss of radio, MCU reset, brownout, sensor disconnect, and stuck output tested safely.

## Environmental and production

- [ ] IP65/IP67 ingress test completed on production-intent enclosure, glands, gasket, and vent.
- [ ] Condensation/drainage, UV, salt/corrosion, temperature, vibration, and drop tests completed.
- [ ] LoRa range and EMC pre-compliance tests completed with motor starting and stalled.
- [ ] Printed PA-CF moisture conditioning and dimensional process documented.
- [ ] 500-cycle engineering test and 5,000-cycle pilot endurance test passed.
- [ ] Risk assessment, installation manual, maintenance interval, labels, traceability, and warranty process approved.
- [ ] Applicable Tunisian electrical, radio, battery transport, environmental, and product-safety obligations reviewed by qualified professionals.

