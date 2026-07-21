# Ninebot G30 Max — Communication Protocol Reference

> ✅ **Firmware-verified (2026-06-02).** The `5A A5` header framing, the device address scheme
> (`0x20`/`0x21`/`0x22`/`0x3E`), the `sum ^ 0xFFFF` checksum, and 115200 8N1 were confirmed by
> re-disassembling the stock dumps — the identical header state machine appears in the ESC (`DRV`,
> 3×), BMS (`BMS_1.7.4.5`, 1×) and nRF51 BLE (1×) firmware. The register-map tables below are
> documented/community-sourced and only **partially** reconstructed from firmware. Evidence:
> [`firmware/decompiled/RE_FINDINGS.md`](../firmware/decompiled/RE_FINDINGS.md).
>
> ✅ **Hardware-verified (2026-06-15).** The bus was captured on the **live G30 scooter** with a
> NUCLEO-C542RC software-UART logic analyzer: 115200 8N1, the `5A A5` framing with **LEN = payload
> byte count**, the `sum(LEN..payload) ^ 0xFFFF` checksum, and addresses `0x20`/`0x21` are all
> confirmed on real silicon. A captured, checksum-valid frame and the live runtime `0x64`/`0x65`
> dashboard↔ESC conversation are documented below. Source:
> [`boards/ble-dashboard/C542_BUS_CAPTURE.md`](../boards/ble-dashboard/C542_BUS_CAPTURE.md).

## Overview

The Ninebot G30 Max uses a proprietary serial protocol for communication between all boards (ESC, BLE, BMS) and with external devices (phone app, PC tools). This protocol is shared across many Ninebot/Segway products (ES, Max, F-series, etc.) with minor variations.

## Physical Layer

| Property | Value |
|---|---|
| **Interface** | UART (TTL 3.3V logic) |
| **Baud Rate** | 115200 |
| **Data Bits** | 8 |
| **Parity** | None |
| **Stop Bits** | 1 |
| **Mode (ESC↔BLE)** | Half-duplex (single wire) |
| **Mode (ESC↔BMS)** | Full-duplex (TX/RX pair) |

## Packet Format

Every packet follows this structure:

```
┌────────┬────────┬──────┬─────────┬─────────┬──────┬──────┬───────────┬────────────┬────────────┐
│ Header │ Header │ Len  │ SrcAddr │ DstAddr │ Cmd  │ Arg  │ Payload   │ Checksum_L │ Checksum_H │
│ 0x5A   │ 0xA5   │ byte │ byte    │ byte    │ byte │ byte │ 0..N byte │ byte       │ byte       │
└────────┴────────┴──────┴─────────┴─────────┴──────┴──────┴───────────┴────────────┴────────────┘
```

### Field Descriptions

| Field | Size | Description |
|---|---|---|
| **Header** | 2 bytes | Always `0x5A 0xA5` — start of packet marker |
| **Length (bLen)** | 1 byte | **Payload byte count only.** Firmware-verified against `buildPacket`/`parseProtocolByte` in DRV_1.6.13: `LEN = payload_length`; full frame = `LEN + 9` bytes (2 header + LEN + 1 len + 4 SRC/DST/CMD/ARG + 2 checksum), and the parser's expected body after the header = `LEN + 7`. See `firmware/decompiled/DECOMPILATION.md`. |
| **Source Address** | 1 byte | Address of the sending device |
| **Destination Address** | 1 byte | Address of the receiving device |
| **Command (bCmd)** | 1 byte | Command type (read, write, etc.) |
| **Argument (bArg)** | 1 byte | Register or sub-command index |
| **Payload** | 0-N bytes | Data payload (variable length) |
| **Checksum** | 2 bytes | 16-bit checksum (little-endian) |

### Checksum Calculation

The checksum is a simple 16-bit sum of all bytes from **Length** through **Payload** (inclusive), then `XOR 0xFFFF`:

```python
def calculate_checksum(data):
    """
    data = bytes from bLen through end of payload
    """
    checksum = 0
    for byte in data:
        checksum += byte
    checksum ^= 0xFFFF
    checksum &= 0xFFFF
    return checksum  # little-endian: low byte first, high byte second
```

### Example Packet

Reading ESC serial number (register 0x10) from app:

```
5A A5        # Header
02           # Length = payload byte count = 2 (the "0E 00" below). Frame total = 2 + 9 = 11 bytes.
3E           # Source: App (0x3E)
20           # Destination: ESC (0x20)
01           # Command: Read
10           # Argument: Register 0x10 (serial number)
0E 00        # Payload (2 bytes): request 14 (0x0E) bytes
7C FF        # Checksum (2 bytes, little-endian) = ~(02+3E+20+01+10+0E+00) = 0xFF7C
```

> The LEN byte counts **only the payload** (here `0E 00` → `02`), not SRC/DST/CMD/ARG. This is the
> firmware-verified convention (a previous revision of this doc incorrectly wrote `06`).
>
> ⚠️ Any doc that states `LEN = 4 + payload` or `LEN = SrcAddr..Payload` is **WRONG** — the live-bus
> capture proves `LEN = payload byte count` (see the hardware-captured frame below).

### Hardware-captured frame (live G30, checksum VALID, 2026-06-15)

Reproduced across 3 captures on the actual scooter — the dashboard polling the ESC:

```
5A A5 05 21 20 65 00 04 28 22 02 00 04 FF
└hdr┘ │  │  │  │  │  └──── payload[5] ────┘ └ CK ┘
     LEN SRC DST CMD ARG
      05  21  20  65  00
```

- **SRC `0x21`** (BLE/dashboard) → **DST `0x20`** (ESC), **CMD `0x65`**, **LEN = 5** (the five payload
  bytes `04 28 22 02 00`), checksum `04 FF` valid.
- This is the **dashboard acting as bus master** on this wire, polling the ESC every cycle. It confirms
  the framing, the `LEN = payload count` convention, the checksum, and the addresses on real hardware.

## Device Addresses

| Address | Device | Description |
|---|---|---|
| `0x20` | **ESC** | Motor controller |
| `0x21` | **BLE** | Dashboard (BLE module) |
| `0x22` | **BMS** | Battery management system |
| `0x23` | **BMS2** | External battery BMS (if present) |
| `0x3E` | **App** | Phone application (via BLE) |
| `0x3F` | **PC** | PC tools (via serial adapter) |

## Command Types

| Cmd | Hex | Description |
|---|---|---|
| **Read** | `0x01` | Read register(s) from target |
| **Write** | `0x02` | Write register(s) to target |
| **Read Response** | `0x01` | Response to a read command (same cmd byte, payload contains data) |
| **Write Response** | `0x02` | Acknowledgment of write command |

### Stock app command dispatch (firmware-disassembled)

> Disassembled from `DRV_1.2.6` / `BMS_1.7.4.5`: the application command handler
> (`App_to_ESC_handler @0x08005624`) dispatches the `CMD` byte through a `tbb` jump table at
> `0x08005650`. The register file is **16-bit words in SRAM @`0x200007D6`, indexed by the `ARG` byte**
> (see [`REGISTER_MAP.md`](REGISTER_MAP.md)). The opcodes:

| Cmd | Hex | Action |
|---|---|---|
| **READ** | `0x01` | Read `regfile[ARG..]` (response cmd `0x04`) |
| **WRITE** | `0x02` | Write `regfile[ARG..]` + response |
| **WRITE** | `0x03` | Write `regfile[ARG..]`, no response |
| **Subscribe / stream** | `0x07`–`0x0A` | Subscribe / stream register updates |
| **Calibration** | `0x18` | Calibration (needs sub-cmd `0x12` + `"N4G"` magic) — **NOT a reset** |
| **Data-block write** | `0x50` | Firmware data-block write (IAP) |
| **Enter firmware update** | `0x57` / `0x59` | Enter bootloader — **UID-password-gated** (see below) |
| **Erase / begin-flash** | `0x58` | Erase application area / begin flashing |
| **Param / seed write** | `0x5C` | Parameter / seed write |

> ⚠️ Writing "reg `0x78`" does **NOT** trigger a reset — that convention is unverified. The real
> enter-update path is **CMD `0x57`/`0x59`**, which is authenticated (next section).

### Enter-bootloader is UID-authenticated (the real flashing wall)

CMD **`0x57`** carries a password derived from the STM32 **96-bit chip UID @`0x1FFFF7E8`** (the
firmware references that address at vma `0x08005478`):

```
CMD 0x57 payload = ~(UID0 + UID1 + UID2)  ‖  ~(UID0 · UID1 · UID2)
                   └──── 32-bit LE ─────┘    └──── 32-bit LE ─────┘
```

i.e. the bitwise-NOT of the **sum** of the three 32-bit UID words, concatenated with the bitwise-NOT of
their **product**, both little-endian. On a valid password the firmware sets a RAM flag; the main loop
then writes a **`0x5A5A` "stay in IAP" magic** to a flash marker page (DRV `0x0801C000`, BMS
`0x0800F000`) and issues `NVIC_SystemReset` (`AIRCR = 0x05FA0004`). The 4 KB stock bootloader checks
that magic at boot and stays in IAP. Because the password is a per-board secret (the chip UID, not
exposed on the bus), the dashboard cannot be put into update mode over the wired bus without it.

## ESC Register Map (DRV)

> 📓 **Authoritative map: [`REGISTER_MAP.md`](REGISTER_MAP.md)** (sourced from etransport/ninebot-docs,
> mechanism firmware-confirmed). The table below is a simplified legacy view and contains known errors
> for the G30 — e.g. `0x3A` is *session operation time* (not battery voltage; voltage is `0x48`),
> `0x7B` is *KERS level* (not speed limit; speed limits are `0x73/0x74`). Prefer REGISTER_MAP.md.

### Read-Only Registers

| Register | Size | Description | Unit |
|---|---|---|---|
| `0x10` | 14 | Serial number | ASCII |
| `0x1A` | 2 | Firmware version | BCD |
| `0x20` | 2 | Error code | Bitmask |
| `0x21` | 2 | Warning code | Bitmask |
| `0x22` | 2 | Flags / status | Bitmask |
| `0x24` | 2 | Remaining range | 0.01 km |
| `0x25` | 2 | Remaining battery % | % |
| `0x26` | 2 | Current speed | 0.001 km/h |
| `0x29` | 4 | Trip distance | m |
| `0x2A` | 2 | Uptime | s |
| `0x2B` | 2 | Frame temperature | 0.1 °C |
| `0x34` | 4 | Total distance | m |
| `0x3A` | 2 | Battery voltage | 0.01 V |
| `0x3B` | 2 | Battery current | 0.01 A (signed) |
| `0xB0` | 2 | Speed limit (current) | km/h |
| `0xB9` | 2 | Motor hall speed | RPM |

### Read-Write Registers

| Register | Size | Description | Default |
|---|---|---|---|
| `0x31` | 1 | Lock state (0=unlocked, 1=locked) | 0 |
| `0x72` | 1 | Cruise control (0=off, 1=on) | 0 |
| `0x73` | 1 | Tail light always on | 0 |
| `0x75` | 1 | Riding mode (0=Eco, 1=D, 2=Sport) | 0 |
| `0x7B` | 2 | Speed limit setting | model-dependent |

## BMS Register Map

> 📓 **Authoritative map: [`REGISTER_MAP.md`](REGISTER_MAP.md).** The legacy table below mis-places
> several fields for the G30: the **cell-voltage block is `0x40–0x49`** (not `0x30–0x39`), pack
> **voltage is `0x34`** (×10 mV), **current `0x33`**, **SoC% `0x32`**, remaining mAh `0x31`. Prefer
> REGISTER_MAP.md.

| Register | Size | Description | Unit |
|---|---|---|---|
| `0x10` | 2 | BMS status | Bitmask |
| `0x17` | 2 | Temperature sensor 1 | 0.1 °C |
| `0x18` | 2 | Temperature sensor 2 | 0.1 °C |
| `0x22` | 2 | Remaining capacity | mAh |
| `0x24` | 2 | Remaining capacity | % |
| `0x25` | 2 | Battery current | mA (signed) |
| `0x26` | 2 | Battery voltage | mV |
| `0x30`–`0x39` | 2 each | Cell voltages 1-10 | mV |
| `0x40` | 2 | Manufacture date | Packed date |
| `0x66` | 2 | Full charge capacity | mAh |
| `0x67` | 2 | Cycle count | Count |

## BLE Register Map

| Register | Size | Description |
|---|---|---|
| `0x10` | 14 | BLE serial number |
| `0x17` | 2 | BLE firmware version |
| `0x68` | 6 | BLE MAC address |
| `0x69` | 16 | Scooter model string |
| `0x79` | 1 | BLE password |

## Error Codes

| Bit | Error | Description |
|---|---|---|
| 0 | Phase A overcurrent | Motor phase A short/overcurrent |
| 1 | Phase B overcurrent | Motor phase B short/overcurrent |
| 2 | Phase C overcurrent | Motor phase C short/overcurrent |
| 3 | Bus overvoltage | Supply voltage too high |
| 4 | Bus undervoltage | Supply voltage too low |
| 5 | Hall sensor error | Hall sensor signal abnormal |
| 6 | MOS gate driver error | Gate driver fault |
| 7 | Throttle error | Throttle signal out of range |
| 8 | Controller temp high | ESC overtemperature |
| 9 | Motor temp high | Motor overtemperature |
| 10 | Communication error | Lost link to BLE or BMS |

## Runtime dashboard↔ESC frames (hardware-confirmed 2026-06-15)

The live dashboard↔ESC conversation (reverse-engineered from `vesc-lisp/g30_dash.lisp`, then confirmed
on the live scooter via the C542 rig) uses two periodic head-I/O frames:

| Frame | Dir | Format |
|-------|-----|--------|
| **0x65** | dashboard → ESC | `5A A5 05 21 20 65 00 \| ... throttle brake ... \| CK` — **throttle = payload byte at frame offset 5, brake = byte 6** (hall levels) |
| **0x64** | ESC → dashboard | `5A A5 06 20 21 64 00 \| mode batt light beep speed error \| CK` — telemetry |

**0x64 telemetry payload (6 bytes):**

| Offset | Field | Meaning |
|--------|-------|---------|
| 0 | `mode` | riding mode (eco/drive/sport) |
| 1 | `batt` | battery % |
| 2 | `light` | headlight state |
| 3 | `beep` | beeper request |
| 4 | `speed` | km/h while riding (battery % when idle) |
| 5 | `error` | fault code — **`0` = no fault** |

**Comm-fault clear (verified on hardware):** the dashboard faults with an "ESC missing" comm error when
the ESC never answers its `0x65` polls. Emulating the ESC and replying with a valid **`0x64` (error =
0)** to each poll cleared the fault on the live scooter — the dashboard then started emitting its own
`0x64` frames (`5A A5 07 21 20 64 00 …`), the richer conversation it only has with a healthy ESC.

## Firmware Update Protocol

Firmware updates over-the-air follow a specific multi-step process:

1. **Initiate update**: enter IAP via **CMD `0x57`/`0x59`** — *UID-password-gated* (see
   [Enter-bootloader is UID-authenticated](#enter-bootloader-is-uid-authenticated-the-real-flashing-wall))
2. **Enter bootloader**: firmware writes the `0x5A5A` marker + `NVIC_SystemReset`; bootloader stays in IAP
3. **Erase flash**: CMD `0x58` erases the application area
4. **Send firmware blocks**: CMD `0x50` writes firmware in chunks (typically 64-128 bytes)
5. **Verify**: Checksum verification of written data
6. **Reset**: Restart into new firmware

> The bootloader cannot be entered over the wired bus without the chip-UID password (CMD `0x57`). For
> the dashboard, flashing is gated **two ways by design**: the wired ESC bus needs the chip-UID
> password, and the BLE channel needs the Xiaomi MiIO registration token — both are per-device secrets
> (see [`BLE_PROTOCOL_VERIFIED.md`](BLE_PROTOCOL_VERIFIED.md) and
> [`../Documentation/VERIFICATION_REPORT.md`](../Documentation/VERIFICATION_REPORT.md)).

## Tools for Protocol Analysis

| Tool | Description |
|---|---|
| **[ScooterHacking Utility](https://utility.cfw.sh/)** | Android app for reading scooter data and flashing firmware |
| **[Ninebot Flasher](https://github.com/scooterhacking/ninebot-flasher)** | PC tool for serial communication |
| **[XiaoFlasher](https://play.google.com/store/apps/details?id=com.xf.xiaoflasher)** | Android app for advanced protocol operations |
| **Logic Analyzer** | Use Saleae or similar with UART decoder at 115200,8N1 |
| **Serial Bridge** | USB-TTL adapter (3.3V) tapped into UART lines for sniffing |

## Sniffing Protocol Traffic

To monitor live protocol traffic between boards:

1. Open the scooter deck to access the ESC board
2. Identify the UART lines between ESC↔BLE and ESC↔BMS
3. Connect a USB-TTL serial adapter (RX only for passive sniffing)
4. Use a serial terminal at 115200 8N1 to capture raw packets
5. Parse packets using the format described above

**Warning**: Do not connect TX lines when passive sniffing — it may corrupt communication.
