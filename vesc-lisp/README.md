# VESC Lisp — G30 Dashboard Integration

## Overview

VESC Lisp script that bridges the Ninebot G30 Max dashboard to the VESC motor controller. The VESC reads throttle/brake inputs from the dashboard via Ninebot protocol and sends display updates back.

**Throttle stays at the dashboard** — the BLE STM32 reads the throttle ADC and sends the value via Ninebot protocol frame `0x65`. The VESC Lisp script reads it and feeds it to the VESC motor controller via `app-adc-override`.

## Protocol

### UART Configuration

- Baud rate: 115200
- Mode: Half-duplex (single wire)
- Ninebot protocol header: `0x5AA5`

### Frame Format

```
Offset  Size  Description
0       2     Header: 0x5A 0xA5
2       1     Payload length (bytes from offset 3 to end of payload)
3       1     Source address
4       1     Destination address
5       1     Frame code (command)
6..     N     Payload data
N+1     2     CRC-16 (XOR 0xFFFF of sum of bytes from offset 2 to end of payload, little-endian)
```

### Frame 0x65 — Dashboard → VESC (Throttle/Brake)

Sent by the BLE STM32 dashboard to the VESC.

| Byte Offset | Field | Range | Description |
|---|---|---|---|
| 5 | Throttle | 0–255 | ADC reading (0–3.3V mapped to 0–255, divide by 77.2 for voltage) |
| 6 | Brake | 0–255 | Brake lever ADC reading |

### Frame 0x64 — VESC → Dashboard (Display Update)

Sent by the VESC to the BLE STM32 for dashboard display.

| Byte Offset | Field | Values | Description |
|---|---|---|---|
| 7 | Mode | 1=Drive, 2=Eco, 4=Sport, 16=Off, 32=Lock, +128=Temp warning | Speed mode indicator |
| 8 | Battery | 0–100 | Battery percentage (or motor current % in secret mode) |
| 9 | Light | 0/1 | Headlight state |
| 10 | Beep | 0/1 | Buzzer trigger |
| 11 | Speed | 0–255 | Speed in km/h (battery % shown when idle in secret mode) |
| 12 | Error | 0–255 | Error code (ESC temperature when idle in secret mode) |

### TX Frame Structure

```
Byte:  0     1     2     3     4     5     6     7     8     9    10    11    12    13    14
     [5A]  [A5]  [06]  [20]  [21]  [64]  [00] [mode][batt][light][beep][spd][err][crcL][crcH]

Header: 0x5AA5
Length: 0x06 (6 bytes payload)
Source: 0x20 (ESC)
Dest:   0x21 (BLE)
Code:   0x64
Sub:    0x00
CRC:    XOR 0xFFFF of sum of bytes [2..12], split little-endian
```

### CRC Calculation

```
crc = sum of bytes from offset 2 to offset 12 (inclusive)
crc = crc XOR 0xFFFF
tx_frame[13] = crc & 0xFF        (low byte)
tx_frame[14] = (crc >> 8) & 0xFF (high byte)
```

## Speed Modes

| Mode | Display | Description |
|---|---|---|
| Eco (2) | Leaf icon | Low speed, low power |
| Drive (1) | D icon | Normal riding |
| Sport (4) | S icon | Full power |

Mode cycling: Sport → Eco → Drive → Sport (double-press power button).

## Button Logic

| Action | Condition | Effect |
|---|---|---|
| Single press (off) | Scooter is off | Turn on |
| Single press (on) | Scooter is on | Toggle headlight |
| Double press | No brake | Cycle speed mode |
| Double press + brake | Brake held | Toggle lock |
| Double press + brake + throttle | Both held | Toggle secret mode |
| Long press (6s) | Any | Turn off |

## Dependencies

- VESC firmware 6.x with Lisp scripting support
- `vesc_packages/lib_code_server` for CAN code server (optional, multi-VESC setups)

## Reference Implementations

- [CRZX1337/g30-vesc-dash](https://github.com/CRZX1337/g30-vesc-dash) — G30-specific VESC Lisp script
- [m365fw/vesc_m365_dash](https://github.com/m365fw/vesc_m365_dash) — M365 VESC dashboard script (similar protocol)
- [tonymillion/VescNinebotDash](https://github.com/tonymillion/VescNinebotDash) — Ninebot VESC dashboard script

## Integration with Custom Firmware

The custom BLE STM32 firmware sends the same protocol frames as the stock dashboard:
- Frame `0x65` with throttle/brake ADC values
- Expects frame `0x64` display updates from the VESC

This means the VESC Lisp script works identically with both stock and custom dashboard firmware.
