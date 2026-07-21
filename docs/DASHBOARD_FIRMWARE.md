# STM32 Dashboard Firmware — Design

**Date:** 2026-06-03
**Target:** STM32F103C8T6 (BLE-board main MCU), 64 KB flash, 20 KB SRAM, 72 MHz.
**Role:** the **always-on keeper** (mirrors the stock scooter) + Ninebot⇄VESC bridge + Daly client +
display/inputs. Satisfies Requirements 1, 2, 4, 11, 12, 13 and the power-latch **Solution D**
(`docs/POWER_LATCH_SCHEMATIC.md`).

> ⚠️ Reminder (firmware-verified): the `BLE_*.bin` dumps are the **nRF51**, not this STM32 — there is no
> stock STM32-dashboard dump. This firmware is written from the documented HAL + the verified protocol,
> not reconstructed from a binary. The STM32 pinout is reference-derived (`boards/ble-dashboard/PINOUT.md`).

---

## 1. Responsibilities

| # | Responsibility | Req |
|---|----------------|-----|
| Keeper | STOP-mode sleep (~10 µA); wake on power-button EXTI; command Daly on/off | Power latch D |
| Bridge | half-duplex Ninebot (115200) ⇄ VESC; relay throttle/brake (0x65), render display (0x64) | 1, 14 |
| Inputs | throttle ADC (PA0), brake ADC (PA1), power button (PB12) | 2 |
| Display | speed / battery / mode / light / error | 2 |
| Lights | head/tail via VESC (PPM driver) — dashboard requests, VESC switches | 2 |
| Speed cap | **none** — never clamp speed commands | 4 |
| Daly | soft-UART 9600 on w4: `0xD9` on/off + read SoC/cells (optional) | 13 |
| nRF51 | mode commands `0xAA`/`0xAB` (Haystack), firmware relay | 15 |
| Debug | `[DBG]` UART when `DEBUG_ENABLED` | 11 |

## 2. Cable / pin usage (Solution D — only the original 4 wires leave the board)

| Wire | STM32 pin | Use |
|------|-----------|-----|
| w1 5V | VDD (via always-on rail) | board power, always on |
| w2 GND | VSS | ground |
| w3 DATA | **USART2 (PA2, half-duplex)** | Ninebot 115200 ⇄ VESC |
| w4 BTNCTL | **PB-GPIO soft-UART TX** | bit-banged 9600 → Daly RX + Daly S1 wake |
| (internal) | **PB12** | physical power button (EXTI, active-low) |
| (internal) | **PA0/PA1** | throttle / brake ADC |
| (internal) | **USART1 PA9/PA10** | ⇄ nRF51 (mode cmds, BLE relay) |

> The button is read **internally on PB12** — it is no longer exported on w4, which frees w4 to be the
> Daly control line. This is the key Solution-D move.

## 3. State machine

```
        ┌──────── DEEP_SLEEP (STOP mode) ────────┐
        │ Daly discharge OFF → VESC unpowered     │
        │ STM32 in STOP, PB12 EXTI armed          │
        └───────────────┬─────────────────────────┘
              PB12 press (EXTI wakes core)
                        ▼
        ┌──────────── WAKE ──────────────────────┐
        │ re-init clocks/periph                    │
        │ soft-UART w4: send Daly 0xD9 ON          │
        │  (start-bit edges also pulse Daly S1)    │
        │ → discharge closes → VESC powers         │
        └───────────────┬─────────────────────────┘
            VESC link up (Ninebot frames seen)?
              ├─ yes ─────────────────► RUN
              └─ timeout 4 s ─► Daly 0xD9 OFF ─► DEEP_SLEEP
        ┌──────────────── RUN ───────────────────┐
        │ bridge throttle/brake↔VESC, render dash │
        │ button: short=light, double=mode,       │
        │         double+brake=lock, long=off     │
        └───────────────┬─────────────────────────┘
            long-press (≥1.5 s)
                        ▼
        send VESC "off" (disable output) → soft-UART Daly 0xD9 OFF
        → notify nRF51 0xAA (Haystack) → enter STOP → DEEP_SLEEP
```

## 4. Module map (`firmware/decompiled/ble/`)

| File | Contents |
|------|----------|
| `ble_main.cpp` | init, main loop, state machine, SysTick, EXTI/STOP |
| `dash_keeper.{h,cpp}` *(new)* | sleep/wake, Daly on/off via soft-UART, S1 wake |
| `dash_bridge.{h,cpp}` *(new)* | Ninebot⇄VESC: parse 0x65 (in), build 0x64 (out), register map |
| `daly_soft_uart.{h,cpp}` *(new)* | bit-banged 9600 TX/RX on w4; `0xD9` frames; `0x90-0x98` reads |
| `lib/ninebot-protocol/` | stock core (verified) + `nbx.*` (NB+ v2) |
| `common/include/hal.h` | UART/GPIO/ADC/timer/flash interfaces (host-sim + STM32) |

## 5. Key designs

### 5.1 Soft-UART on w4 (9600, Daly + S1)
- Bit-bang TX at 9600 (104 µs/bit) on the w4 GPIO using a timer.
- The Daly `0xD9 ON`/`OFF` frames (13 B) are emitted; the first start bit (LOW) doubles as the Daly `S1`
  wake pulse (S1 is active-low). If the unit needs a longer hold, prefix a 2 ms LOW break.
- Optional RX (read SoC/cells `0x90-0x98`) shares the line half-duplex or uses w4 as input between TX.

### 5.2 Bridge & register map
- RX from VESC: stock `5A A5` frames. On **0x64** (display) → update the local display fields.
- TX to VESC: **0x65** (throttle byte 5, brake byte 6) from the ADC inputs.
- Answer phone-app register reads relayed by the nRF51 using `docs/REGISTER_MAP.md` (e.g. `0x48` voltage,
  `0x26` speed, `0x22` SoC, `0x1B` fault, `0x75` mode). **Never clamp speed** (Req 4).
- Optional NB+ STREAM (`docs/PROTOCOL_V2.md`) from the VESC for richer telemetry; falls back to polled
  stock reads if the VESC lisp hasn't been upgraded (HELLO negotiation).

### 5.3 Keeper power
- Board powered from the always-on rail (w1) so PB12 EXTI works while everything else is off.
- STOP mode: SRAM retained, EXTI on PB12, RTC optional. Wake re-inits and powers the VESC via Daly.
- Daly cut = the real power-off (`docs/POWER_LATCH_SCHEMATIC.md` Solution D).

### 5.4 nRF51 coordination (Req 15)
- Before STOP: send `0xAA` on USART1 → nRF51 switches to Haystack (FindMy) advertising.
- On wake: send `0xAB` → nRF51 returns to normal (Ninebot + VESC NUS).

## 6. Memory & build
- Linker per deployment phase (Req 12): app @ `0x08001000` (Phase 1) / `0x08004000` (Phase 3).
- `DEBUG_ENABLED` gates `[DBG]` output (Req 11); must not collide with `5A A5`/`5A A6` frames.
- Build via the existing `firmware/decompiled` CMake (host sim/tests) and the bootloader CMake for target.

## 7. Test plan (host-sim first, then HW)
- Sim (`firmware/decompiled/tests`): soft-UART byte timing, Daly `0xD9` frame bytes (golden), bridge
  0x65/0x64 round-trip, register-map answers, state-machine transitions (sleep↔run), speed-uncapped.
- HW (`Target/`): real UART monitor, button press → VESC powers (Daly), long-press → power cut,
  throttle→motion, display correct, Haystack mode switch.
