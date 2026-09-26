# Actuator: 4.5 V latching solenoid

The actuator uses the PlatformIO `ttgo-lora32-v21` board definition. Its onboard
SX1276 reset is GPIO23, so GPIO23 must never drive the valve.

| Signal | Configured GPIO |
|---|---:|
| LoRa reset | 23 |
| Driver AIN1 | 2 |
| Driver AIN2 | 4 |
| Driver PWMA | 12 |
| Driver STBY | 17 |
| Position feedback | Disabled by default (`0xFF`) |

Confirm the physical PWMA wire is on GPIO12. The former reference mapping with
PWMA on GPIO23 conflicts with radio reset and cannot be used.

The firmware sends a 30 ms polarity-reversing pulse. It does not regulate or measure
the solenoid drive voltage. Verify coil pulse duration, current, supply, driver
rating, and polarity against the actual parts before connecting water.

## Identity and startup

The generic actuator image has no default ID or PSK. On boot it configures safe
outputs and attempts a CLOSE pulse before enabling radio. A missing, corrupt, or
wrong-role identity keeps radio disabled and leaves the physical serial provisioning
console available. See `PILOT_DEPLOYMENT.md` for commands.

Because a latching valve can retain position through controller reset, a boot CLOSE
pulse is only an attempt. Loss of power can prevent a later powered close. Keep an
accessible upstream manual shutoff.

Replay state must load successfully before radio is enabled. Each accepted command's
new replay checkpoint is committed and read back before any valve pulse; failure
rejects the command. Uplink sequence blocks are also verified before ACK/heartbeat
transmission.

## Pilot runtime and feedback behavior

Every valid OPEN carries a timeout from 1 to 300 seconds. The default and absolute
maximum are 300 seconds. The actuator arms the deadline before the first OPEN pulse.
A repeated OPEN pulse does not extend the original deadline. At expiry, it attempts
CLOSE independently of the gateway's inferred state.

The 300-second ceiling is enforced twice: authenticated commands above it are
rejected, and a compile-time assertion rejects `PILOT_MAX_OPEN_S` above 300. Before
an OPEN pulse, the actuator samples its battery. It rejects OPEN below 3400 mV and
also rejects an implausible reading above 5000 mV, which can indicate divider or ADC
misconfiguration. CLOSE bypasses this battery gate so shutdown remains available.

With position feedback enabled, the actuator reads it after every command and before
every 30-second heartbeat. A mismatch sets the fault flag. If feedback reports OPEN
while the command is CLOSED, the safety loop schedules immediate CLOSE and retries
a failed close after five seconds. If an OPEN pulse does not verify as open, the
actuator immediately sends a CLOSE pulse because the feedback fault is ambiguous;
it keeps retrying CLOSE if closure also fails verification.

The armed CLOSE timer is serviced before the main loop checks identity or radio.
Local physical CLOSE retries therefore continue even when the node is unprovisioned,
replay storage prevented radio startup, or LoRa is unavailable. Only the follow-up
heartbeat is skipped while communications are unavailable.

The current default build has no position switch:

- ACK and heartbeat report `feedback_valid=0` and
  `ACT_ERR_FEEDBACK_MISSING`.
- State is the requested/estimated state, not a physical measurement.
- ACK result `0x02` means `PULSE_SENT_UNVERIFIED`; it does not mean the valve moved.
- The gateway UI reports feedback unavailable and blocks automatic OPEN.
- Manual commands remain available for attended bench and pilot tests.

ACK result values are `0x00` verified, `0x01` feedback mismatch, `0x02` pulse sent
without feedback, `0x03` invalid authenticated command, `0x04` OPEN rejected for a
low or implausible battery reading, and `0x05` replay-storage/NVS persistence
failure. Result `0x05` rejects the command before any valve actuation. An
unauthenticated, wrong-type, wrong-length, wrong-node, or replayed radio frame is
dropped.

To use GPIO3 as an active-low dry-contact OPEN signal, wire and bench-test the switch
described in `WIRING.md`, then add this actuator build flag:

```ini
-DACTUATOR_VALVE_FB_PIN=3
```

The feedback signal must be electrically safe for 3.3 V GPIO and must reflect the
physical valve position. Do not enable automatic mode until both OPEN and CLOSE
feedback transitions have passed the deployment checklist.

## Hydraulic inputs

Flow, pressure, tank, and current inputs remain disabled unless built with
`-DENABLE_HYDRAULIC_SENSORS=1`. Disabled inputs use explicit sentinel values and set
the hydraulics-disabled flag. Enabling them requires real signal conditioning and
calibration; analog inputs must not exceed 3.3 V.

## Verification

Build the firmware and perform the dry hardware tests in `PILOT_DEPLOYMENT.md`:

```powershell
pio run -e actuator_node
pio run -e actuator_node -t upload --upload-port COM_PORT
pio device monitor --port COM_PORT --baud 115200
```

Confirm the port and wiring before upload. A successful firmware build or host test
does not certify the valve, driver, power system, enclosure, or hydraulic shutdown.
