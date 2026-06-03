# USB-UART Wiring — BLE Dashboard Development

## Overview

This guide explains how to connect a USB-to-UART adapter for BLE dashboard firmware development and debugging. All communication uses 115200 baud, 8N1, 3.3V TTL.

**WARNING: The BLE dashboard operates at 3.3V logic. Never use a 5V UART adapter — it will damage the STM32.**

---

## Method 1: Via VESC USB (Recommended)

The easiest connection — no soldering, no opening the dashboard.

```
PC ──USB──► VESC ──UART──► BLE Dashboard (STM32 USART2: PB6/PB7)
```

### Setup

1. Connect PC to VESC via USB
2. Open a serial terminal at 115200 baud
3. Send `UPDATE BLE\n` to enter passthrough mode
4. VESC transparently bridges USB ↔ BLE Dashboard UART

### Wiring

No additional wiring — uses existing scooter UART bus.

### Limitations

- Requires VESC firmware with passthrough support
- Cannot monitor raw bus traffic (VESC filters)
- Must exit passthrough to communicate with VESC again

---

## Method 2: Direct UART Tap on BLE Board

Tap directly into the UART lines between the dashboard and the ESC/VESC connector.

```
USB-UART         BLE Dashboard PCB            VESC
Adapter          ┌──────────────────┐
                 │  STM32F103C8T6   │
GND ────────────►│  GND             │
TX  ────────────►│  PB7 (USART2_RX) ├─────── VESC TX
RX  ◄────────────│  PB6 (USART2_TX) ├─────── VESC RX
                 │                  │
                 └──────────────────┘
```

### Wiring Table

| USB-UART Adapter | BLE Dashboard | Wire Color (suggested) |
|---|---|---|
| **GND** | **GND** (any ground pad) | Black |
| **TX** | **PB7** (USART2_RX, pin 43) | Green |
| **RX** | **PB6** (USART2_TX, pin 42) | White |
| 3.3V (optional) | 3.3V pad (only if not bus-powered) | Red |

### Access Points

The UART lines are accessible at:
- **4-pin ESC connector** on the BLE board (carries TX, RX, 5V, GND)
- **Test pads** on the PCB back (if populated on your revision)
- **Solder points** directly on PB6/PB7 pads

### Notes

- The ESC connector uses the same UART lines — you can tap in parallel
- Half-duplex: the bus is shared, so your adapter sees both directions
- If VESC is also connected, you see all bus traffic (dashboard + VESC)

---

## Method 3: SWD Debug (Programming Only)

For initial bootloader flashing or recovery. Not for UART communication.

```
ST-Link V2       BLE Dashboard PCB
                 ┌──────────────────┐
                 │  STM32F103C8T6   │
GND ────────────►│  GND             │
SWDIO ──────────►│  PA13 (pin 34)   │
SWCLK ──────────►│  PA14 (pin 37)   │
3.3V ───────────►│  3.3V            │
                 └──────────────────┘
```

### SWD Wiring Table

| ST-Link V2 | BLE Dashboard | Description |
|---|---|---|
| **GND** | **GND** | Ground reference |
| **SWDIO** | **PA13** (pin 34) | SWD data |
| **SWCLK** | **PA14** (pin 37) | SWD clock |
| **3.3V** | **3.3V** (optional) | Power supply (only if board not powered) |

### SWD Access

- SWD pads are on the PCB back
- Some revisions have a 4-pin header (SWDIO, SWCLK, GND, 3.3V)
- Others require soldering to exposed pads

---

## Compatible USB-UART Adapters

| Adapter | Chip | Voltage | Notes |
|---|---|---|---|
| **CP2102** | SiLabs CP2102 | 3.3V | Recommended — reliable, native 3.3V |
| **CH340G** | WCH CH340G | 3.3V/5V | Use 3.3V mode only |
| **FT232RL** | FTDI FT232RL | 3.3V/5V | Use 3.3V jumper setting |
| **PL2303** | Prolific PL2303 | 3.3V | Older, driver issues on Win10+ |

---

## Serial Terminal Configuration

| Parameter | Value |
|---|---|
| Baud rate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| Line ending | CR+LF |

### Terminal Commands

```powershell
# Python miniterm (recommended)
python -m serial.tools.miniterm COM3 115200

# With logging
python -m serial.tools.miniterm COM3 115200 --raw | Tee-Object -FilePath "debug_log.txt"
```

---

## Safety Checklist

- [ ] Verify adapter is set to **3.3V** (not 5V)
- [ ] GND connected before any data lines
- [ ] TX/RX correctly crossed (adapter TX → board RX, adapter RX ← board TX)
- [ ] Do not power board from adapter and ESC bus simultaneously
- [ ] Disconnect SWD before powering from bus (if both connected)
