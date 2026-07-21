# NUCLEO-C542RC ↔ G30 dashboard — programmer/tap schematic (hardware-verified 2026-06-15)

Bench rig that taps the dashboard's plug with a **NUCLEO-C542RC** (STM32C542RCT6, Cortex-M33; on-board
ST-LINK/V3) and drives a **bit-banged software UART** on the Ninebot bus. Everything below is measured on
a live G30 unless tagged *(inferred)*. Companion firmware/tools: [`firmware/dash-tap-c542/`](../firmware/dash-tap-c542/),
findings log: [`boards/ble-dashboard/C542_BUS_CAPTURE.md`](../boards/ble-dashboard/C542_BUS_CAPTURE.md).

## Wiring

```
            PC (Windows)
        ┌──────────────────┐
        │  STM32CubeProgrammer (SWD: flash C542, read SRAM mailbox @0x20000000)
        │  COM9 = ST-LINK VCP (optional console)
        │  Bluetooth ──────────────────────────────────┐  (2nd channel, see below)
        └─────┬────────────┘                            │
              │ USB                                     │ BLE  "G30LD"
        ┌─────┴───────────────────────────┐            │  D8:68:BA:16:A0:33
        │      NUCLEO-C542RC               │            │  NUS 6e400001 + MiIO 0xfe95
        │  ┌────────────┐  ┌────────────┐  │            │
        │  │ ST-LINK/V3 │──│  STM32C542 │  │            │
        │  │ (SWD+VCP)  │  │  (M33,48MHz)│ │            │
        │  └────────────┘  └─────┬──────┘  │            │
        │     Arduino hdr:  A0=PA0  A2=PA4  GND          │
        └───────────┬─────────┬─────────┬──┘            │
                    │ PA0     │ PA4     │ GND            │
                    │(quiet)  │(LIVE)   │                │
   ╔════════════════╪═════════╪═════════╪════════════════╪═════════════╗
   ║ G30 dashboard plug (3 used wires)  │                │             ║
   ║                │         │         │                │             ║
   ║      ┌─────────┴──┐   ┌──┴─────────┴──┐         ┌────┴──────┐      ║
   ║      │ nRF51822   │   │  STM32F103C8  │         │ nRF51822  │      ║
   ║      │ UART0  ◄───┘   │  USART2 (PA2) ─┼──┐      │  BLE radio│      ║
   ║      │ (internal  ────┤  USART1(PA9/10)│  │      └───────────┘      ║
   ║      │  link, idle)   │  = dash MCU    │  │   (same nRF51 — BLE side)║
   ║      └────────────┘   └────────────────┘  │                         ║
   ╚═══════════════════════════════════════════╪═════════════════════════╝
                                                │ Ninebot bus, 115200 8N1
                                                ▼
                                         (ESC/VESC — absent; bus otherwise idle)
```

## Signal table (measured)

| C542 pin | Arduino | Wire | Carries | State |
|----------|---------|------|---------|-------|
| **PA4** | **A2** | dashboard signal wire | STM32F103 **USART2** = the Ninebot **ESC bus** (`0x21↔0x20`, 115200 8N1) | **LIVE** — 7012 edges/2 s; decoded `5A A5 05 21 20 65 00 …` (CK ok) |
| PA0 | A0 | "BT" signal wire | nRF51 ↔ STM32 internal link *(inferred)* | quiet (1 edge/2 s — no active BLE session) |
| GND | GND | plug ground | common reference | **required** |

> The wire-finder firmware identifies which is which automatically (it names a line by the SRC address
> it transmits — `0x21` ⇒ dashboard side). Do **not** assume A0 vs A2; let it report.

## Electrical notes (verified the hard way)

- **TX = push-pull, not open-drain.** The C542 internal pull-up is too weak for the bus capacitance;
  open-drain release rises too slowly for 115200 (the dashboard mis-reads it). Drive PA4 **push-pull**,
  only in an idle gap. (`xcvr_main.c`)
- **Half-duplex turnaround:** PA4 is OUTPUT only during TX, INPUT (pull-up) for RX. Leaving it driving
  high after TX blinds the receiver.
- **RX = 4× oversample + on-chip UART decode.** 1× real-time sampling drifts and mis-frames; capture the
  waveform at bit/4 and decode with per-byte start-edge re-sync. (`esc_faker_main.c`, `cap_raw_main.c`)
- **Core clock 48 MHz** (HSI, read live via `SystemCoreClockUpdate`); bit period self-calibrates to
  `SystemCoreClock/115200 ≈ 416 cyc`. No external clock config needed.
- **Host link = SWD**, not the VCP: the firmware exposes an SRAM mailbox at `0x20000000`; the PC pokes
  frames / reads replies with `STM32_Programmer_CLI -r32/-w32` while the firmware runs (hotplug). pyocd /
  OpenOCD-0.12 do **not** support STM32C5 — use STM32CubeProgrammer.

## Roles the same rig plays (all firmware-flashed via ST-LINK)

| Firmware | Role |
|----------|------|
| `cap_raw_main.c` + `decode_raw.py` | logic-analyzer capture of the bus |
| `wire_finder.c` | identify the live wire / SRC address |
| **`esc_faker_main.c`** | **emulate the ESC** — reply `0x64` telemetry (error=0) to clear the dashboard comm-fault; SWD inject/read mailbox (`esc_inject.py`) |
| `main_programmer.c` (`soft_uart` + `nbu_prog`) | software-UART NBU/IAP programmer |
| `tools/ble_ninebot.py` | the **BLE** channel (PC Bluetooth → `G30LD`) |

## Two channels, two locks (why the bootloader dump isn't done)

```
   wired ESC bus (this rig) ──► STM32F103 dashboard ◄── BLE (PC Bluetooth)
        │                                                   │
   enter-update = CMD 0x57                            NUS relay
   gated by chip UID @0x1FFFF7E8                      gated by MiIO token
   (~Σ ‖ ~Π of UID words)                             (Xiaomi cloud / Mi Home)
        └──────────── both are device secrets, not on the wire ───────────┘
```

Supply either secret (the **MiIO token**, the **chip UID**, or a **captured real app-update** that carries
the real CMD 0x57 password) and the rest — TX, RX, ESC emulation, programmer, dumper, IAP/ECDSA — is built
and proven to finish the dump. See `boards/ble-dashboard/C542_BUS_CAPTURE.md`.
