# Security: physical provisioning and AES-128-GCM

## Provisioning model

There is no over-the-air registration handshake. Each sensor or actuator receives a
role-bound identity over its physical USB serial connection. The gateway receives a
matching record through its authenticated dashboard.

```text
Physical USB console                    Gateway dashboard
--------------------                    -----------------
Generate a unique 16-byte PSK
PROVISION 0001 <32 hex>                 Add node 0x0001
Node stores role + ID + raw PSK         Select matching type
in dev_ident NVS                        Enter the same raw PSK
Radio starts only after validation      Gateway stores node in node_db NVS
```

Use `STATUS` to inspect role, state, ID, and chip identity. The PSK is never printed.
`0000`, `FFFF`, an all-zero key, and an all-`FF` key are rejected.

```text
STATUS
PROVISION 0001 <32-hex-PSK>
UNPROVISION 0001 CONFIRM
ERASE CORRUPT CONFIRM
```

`PROVISION` only works when the identity is empty. `UNPROVISION` requires the
current ID and explicit confirmation. The final command is accepted only for a
corrupt or role-mismatched record. Identity records include a CRC to detect storage
corruption; the CRC is not a secret or an at-rest encryption mechanism.

The gateway refuses duplicate node IDs rather than overwriting a PSK and resetting
replay counters. Reprovisioning requires explicit removal at the gateway,
`UNPROVISION` over the physical node console, and a newly generated PSK. A newly
registered node has no freshness credit; the gateway blocks OPEN until it accepts a
fresh authenticated packet from the physical actuator.

The node and gateway store the raw per-node PSK in NVS. Restrict physical access,
disable debug access for any later production design, and never include PSKs in
logs, tickets, screenshots, or source control.

## Per-packet authenticated encryption

| Parameter | Value |
|---|---|
| Algorithm | AES-128-GCM using mbedTLS |
| Key | Unique 16-byte PSK per field node |
| Nonce/IV | 12 bytes |
| AAD | node ID (2) + packet type (1) + sequence (4) |
| Authentication tag | Full 16 bytes |

The transmitted nonce is:

```text
[packet_type | 0x00 | node_id_hi | node_id_lo | sequence in 8-byte field]
```

The current counter is 32 bits and occupies the low bytes of the nonce's sequence
field. Packet type separates telemetry, command, ACK, and heartbeat nonce spaces.
Node ID and type are also authenticated as AAD with the sequence.

Do not mix this release with older firmware that transmitted a 4-byte tag. All
gateway, sensor, and actuator firmware must be updated together.

## Replay and nonce rules

- Each transmitter reserves sequence ranges in `lora_seq` before using them.
- Gateway replay state is independent for telemetry, heartbeat, and ACK streams.
- The actuator persists the last accepted command sequence.
- Packets at or below the stored sequence for that stream are dropped.
- A stream must stop and receive a new PSK before the 32-bit counter is exhausted.

NVS writes are not assumed successful. Sequence-block reservations and replay
checkpoints are read back before use. A reservation failure suppresses that
transmission; an actuator replay-load failure keeps radio disabled; a replay-save
failure rejects the command before movement; and a gateway replay-save failure
rolls state back and rejects the packet.

Identity removal deliberately does not erase `lora_seq`. Do not restore an old NVS
backup, clone NVS to another board, or clear sequence state while retaining a PSK.
After a full flash erase, NVS replacement, or uncertain sequence history, generate
a fresh PSK and update both node and gateway.

## Cloud and local access

The optional cloud client accepts only an `https://` base URL and requires both an
API key and a PEM CA certificate. It sends the key in `X-API-Key` and validates the
server certificate against the supplied CA. The supervised pilot rejects cloud
`VALVE_ON` with a negative ACK and permits only remote `VALVE_OFF`. Cloud idempotency
uses command ID together with node ID and requested state. Identifiers are validated
before narrowing: `command_id` must be an integer from 1 through `UINT32_MAX`, and
`node_id` must be an integer from 1 through 65534. Negative, zero, non-integer, and
out-of-range values are rejected instead of wrapping to a different identifier. Only
an exact completed tuple returns a cached ACK; a delayed completion with a different
node/state is ignored, and a fast ACK cannot downgrade a completed tuple to pending.
Other pre-radio rejections also produce negative ACKs. This cache is bounded and
volatile, so the server must issue unique command IDs and retain outcomes.

The commissioning AP uses WPA2 and a generated password printed on physical serial.
The farm-network dashboard uses HTTP Basic authentication. Place it on an isolated
pilot network. Browser OTA and local MQTT are disabled by default in the supervised
pilot build.

These controls provide packet confidentiality/integrity, replay rejection, and
basic local/cloud access control. They do not provide signed firmware, secure boot,
flash encryption, hardware key storage, or proof that a physical valve moved.
