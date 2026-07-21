# Dashboard Firmware (STM32F103C8T6)

Real, flashable custom firmware for the G30 Max **BLE dashboard** main MCU. Linked at `0x08001000`
(Phase 1 — behind the stock 4 KB IAP bootloader, so it flashes with **no soldering** via the serial-IAP
path: [`../../docs/DASHBOARD_NO_SOLDER_FLASH.md`](../../docs/DASHBOARD_NO_SOLDER_FLASH.md)).

It reuses the **host-tested** header-only modules from [`../decompiled`](../decompiled) (no logic is
duplicated): `dash_bridge` (synthetic-ESC registers + 0x64/0x65 head protocol), `dash_keeper`
(power-latch FSM), `daly_soft_uart` (Daly frames), and `watchdog_supervisor`.

## Hard requirement — 5000 ms watchdog (Req 16)
The STM32 IWDG runs at **5000 ms** (`PR=4, RLR=3124` @ LSI 40 kHz → exactly 5.000 s; computed by
`tools/analysis/iwdg_config.py` / `ninebot::iwdg_params()`). The main loop reloads it **only while every
required subsystem is fresh** (loop, clock, ADC; VESC link while in RUN). If a required subsystem goes
**missing/stalls**, the reload is withheld → the IWDG resets the MCU and re-inits. The IWDG is started
**last** so a failed earlier init can't be cut short.

## Build
```bash
make                  # -> build/dashboard_app.{elf,bin,hex} + size
make DASH_DEBUG=1     # adds the [BOOT] banner on USART2 for the HW watchdog test
make clean
# or, with image validation + optional host tests:
python ../../tools/build_dashboard.py [--debug] [--host-tests]
```
Requires the Arm GNU toolchain (`arm-none-eabi-g++`, tested 14.2.Rel1) + `make`.

Current size: **~3.8 KB** text, well within the 60 KB app region.

## Simulate before flashing (no-brick verification)
A functional STM32F103 simulator runs the **exact** `DashApp` logic (shared via
[`include/dash_app.h`](include/dash_app.h)) against a modeled chip and proves the firmware can't brick
the hardware — the 5000 ms watchdog fires *and recovers*, no boot loop, and no writes to the
bootloader/option-byte flash. See [`sim/README.md`](sim/README.md).
```bash
python ../../tools/dashboard_sim.py        # functional sim: build + run, no-brick verdict
python ../../tools/renode_dashboard.py     # Renode: run the REAL .elf, prove the 5 s watchdog
```
The functional sim runs the app logic (instant, CI); **Renode** runs the actual compiled `.elf`
opcode-accurate on an emulated STM32F103 + IWDG (healthy → 1 boot; ADC-fault → resets every ~5 s).

## Pins (see `boards/ble-dashboard/PINOUT.md` + `docs/DASHBOARD_FIRMWARE.md`)
| Peripheral | Pin(s) | Use |
|-----------|--------|-----|
| USART1 | PA9 / PA10 | nRF51 BLE link (app register protocol) |
| USART2 | PA2 | ESC/VESC cable DATA — **single-wire half-duplex** Ninebot |
| ADC1 | PA0 / PA1 | throttle / brake |
| GPIOB | PB0..PB5 | dashboard LEDs |
| GPIOB | PB12 | power/mode button (active-low) |
| IWDG | — | 5000 ms watchdog |

> ⚠️ The throttle/brake-on-dashboard mapping is **reference-derived/unverified** (community sources say
> the G30 routes throttle/brake to the ESC, not the dash). On a VESC build the VESC reads them on its
> own ADC anyway; the 0x65 path here is used only if your dashboard revision does read them. Verify with
> a multimeter. The USART2 half-duplex pin (PA2) and LED/button pins likewise need an on-unit check.

## Files
| File | Role |
|------|------|
| `src/main.cpp` | init + poll loop, integrates the modules + watchdog supervisor |
| `src/dash_hal.{h,cpp}` | clock / SysTick / IWDG / GPIO / USART / ADC drivers |
| `src/startup_stm32f103.s` | vector table + reset (relocates VTOR to 0x08001000) |
| `include/stm32f103.h` | minimal self-contained register defs |
| `stm32f103c8_app.ld` | linker (app @ 0x08001000, 60 KB) |

## Status
Builds clean; logic host-tested (153/153). **Not yet run on hardware** — bring-up checklist:
confirm pin map by multimeter, flash via serial IAP, watch the `[BOOT]`/protocol over UART
(`Target/dashboard_watchdog_test.py`, `verify-hw`), then exercise throttle/modes/display/app pairing.
Daly control is currently a documented stub (`daly_tx` — bit-bang 9600 on cable w4 is the remaining
target driver).
