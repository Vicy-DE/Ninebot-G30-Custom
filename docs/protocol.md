# Ninebot G30 Max — Communication Protocol Reference

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
| **Length (bLen)** | 1 byte | Number of bytes from SrcAddr to end of Payload (inclusive). `bLen = 2 + 1 + 1 + payload_length` |
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
06           # Length: 4 bytes (SrcAddr, DstAddr, Cmd, Arg) + 2 payload = 6
3E           # Source: App (0x3E)
20           # Destination: ESC (0x20)
01           # Command: Read
10           # Argument: Register 0x10 (serial number)
0E 00        # Payload: Read 14 bytes
XX XX        # Checksum (2 bytes, little-endian)
```

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

## ESC Register Map (DRV)

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

## Firmware Update Protocol

Firmware updates over-the-air follow a specific multi-step process:

1. **Initiate update**: Send command to enter IAP (In-Application Programming) mode
2. **Enter bootloader**: Target board resets into bootloader
3. **Erase flash**: Erase application area
4. **Send firmware blocks**: Transfer firmware in chunks (typically 64-128 bytes)
5. **Verify**: Checksum verification of written data
6. **Reset**: Restart into new firmware

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
