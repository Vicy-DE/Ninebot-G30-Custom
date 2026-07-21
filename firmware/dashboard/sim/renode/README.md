# Renode — Instruction-Accurate Dashboard Simulation

This runs the **real compiled firmware** (`dashboard_app.elf`) on
[**Renode**](https://renode.io) (Antmicro's open-source MCU emulator), executing
the actual ARM Thumb opcodes on an emulated **STM32F103 (Cortex-M3)** with a
modeled **IWDG**. It complements the functional simulator in the parent folder:

| | functional sim (`../`) | Renode (here) |
|---|---|---|
| runs | the `DashApp` C++ logic via the HAL | the real `.elf` opcodes |
| models | peripherals + time (hand-written) | full Cortex-M3 + STM32F103 peripherals |
| speed | instant (CI unit test) | ~15–45 s per scenario |
| catches | logic/watchdog-discipline bugs | also register-sequence / toolchain bugs |

## What it proves
Using `[BOOT]` banners printed on USART2 (the `DASH_DEBUG` build prints one at
every boot):

- **`dashboard_healthy.resc`** — ADC modeled healthy → the firmware keeps feeding
  the IWDG → **exactly 1 `[BOOT]`** in 13 s (the watchdog never fires).
- **`dashboard_watchdog.resc`** — ADC left faulted → `adc_read()` fails → the
  watchdog supervisor stops feeding → the **5000 ms IWDG resets the Cortex-M3**,
  which reboots → **~3 `[BOOT]`s in 13 s** (a reset roughly every 5 s).

## UART protocol confirmation (`uart_protocol.resc`)
Injects real Ninebot frames into the running firmware and checks the response —
confirming the documented protocol on the actual binary:
- on **USART2**: a `0x64` display frame `5A A5 06 20 21 64 00 00 50 01 00 0A 00 F9 FE`
  (sets battery = 0x50 = 80 in the synthetic ESC register image);
- on **USART1**: a phone-app read `5A A5 01 3E 20 01 22 02 7B FF` (READ ESC reg 0x22).

Captured response on USART1 (verified byte-for-byte):
```
5A A5 02 20 3E 04 22 50 00 29 FF
 │     │  │  │  │  │  └ 0x0050 = 80 (battery, from the 0x64 frame)
 │     │  │  │  │  └ ARG 0x22         └ checksum (~Σ)
 │     │  │  │  └ CMD 0x04 = read-response
 │     │  │  └ DST 0x3E (App)
 │     │  └ SRC 0x20 (ESC, synthesized)
 │     └ LEN 0x02 (payload bytes)
 └ 5A A5 header
```
This validates framing, `LEN=payload`, the address map, `READ`/`READ_RESPONSE`,
the `~Σ` checksum, and the end-to-end VESC→dashboard→app telemetry path.
> Note: a stray leading `0xAB` (nRF "normal mode") appears because Renode doesn't
> model the PB12 pull-up, so the firmware reads the button as pressed — a sim
> artifact, not a firmware issue.

## Install + run
```bash
winget install Renode.Renode            # one-time (Windows); or https://renode.io
python tools/renode_dashboard.py        # builds DASH_DEBUG fw + runs both scenarios + verdict
```
Manual (from the Renode `bin/` dir):
```
Renode.exe --disable-xwt --console -P 0 -e "i @<repo>/firmware/dashboard/sim/renode/dashboard_watchdog.resc"
```

## Files
| File | Role |
|------|------|
| `dash_overlay.repl` | adds the **IWDG** (`STM32_IndependentWatchdog` @0x40003000, 40 kHz LSI) the stock platform lacks, plus RCC ready-bit tags the firmware busy-waits on |
| `dash_adc.repl` | models ADC1 as silent memory (so the firmware's polling loops don't flood Renode with warnings) |
| `dashboard_healthy.resc` | healthy scenario (1 boot) |
| `dashboard_watchdog.resc` | watchdog-reset scenario (multiple boots) |
| `usart2_*.log` | captured UART output (gitignored) |

## Modeling notes
- The built-in `platforms/cpus/stm32f103.repl` has no IWDG and doesn't model the
  RCC clock-ready bits; `dash_overlay.repl` supplies both so `clock_init()` and
  the watchdog work.
- After a watchdog reset the Cortex-M3 re-fetches SP/PC from `0x00000000`. On the
  real board the stock 4 KB bootloader lives there; here the `.resc` mirrors the
  app's reset vector to `0x0` (the app then sets `VTOR=0x08001000` itself).
  `tools/renode_dashboard.py` passes the actual SP/reset read from the `.bin`.
- ADC fault is injected by leaving `ADC1_SR.EOC = 0`; healthy pre-sets it to `0x2`.
