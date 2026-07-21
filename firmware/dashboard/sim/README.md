# Dashboard Chip Simulator — No-Brick Verification

A **functional STM32F103C8 simulator** that runs the *exact* dashboard firmware
logic (`DashApp`, the same source that ships to the target) against a modeled
chip, so you can prove the firmware **won't brick the hardware before you flash
it**.

## What it is (and isn't)
- **Is:** a peripheral-/time-accurate model (`sim_chip.h`) of the parts the
  firmware uses — **IWDG (register-accurate, real 5000 ms timeout)**, USART1/2,
  ADC, GPIO/LEDs/button, clock, and a flash **brick audit**. The firmware's
  `DashApp` runs through the same `dash::hal` interface, with the HAL implemented
  against the model (`sim_dash_hal.cpp`) instead of real registers.
- **Isn't:** a cycle-accurate instruction-set simulator. It does not execute the
  ARM Thumb opcodes of the `.elf`. For that, run the linked image under
  **Renode** or **QEMU** (notes below).

## What it verifies
Running `dashboard_sim` exercises four scenarios and asserts no-brick invariants:

| Scenario | Proves |
|----------|--------|
| 1. Healthy ride | the 5000 ms watchdog **stays fed**, no spurious reset, app reaches RUN and answers a phone-app register read |
| 2. Missing ADC | the watchdog **fires** (≈5 s cadence — the exact IWDG timeout) and the board **recovers** when the ADC returns |
| 3. VESC link drops in RUN | the watchdog **resets** and the firmware **recovers to a safe sleep state** (no boot loop) |
| 4. No-brick audit | firmware does **no flash writes**, never touches the bootloader/option-byte regions, never sets RDP, and the built `.bin` has a **valid vector table** |

A true **brick** would be: writing the 4 KB bootloader region, writing the
option bytes / setting RDP Level 2, or a dead clock — all of which the model flags
as `bricked`. A watchdog **boot loop** (e.g. a permanently missing subsystem) is
reported as *recoverable*, since the chip can still be reflashed via IAP/SWD.

> The simulator already earned its keep: it caught a watchdog design bug — the
> `CLOCK` subsystem was kicked only once at init but required with a 2000 ms
> staleness, which would have falsely reset healthy hardware. Fixed before flashing.

## Run it
```bash
# one-shot (builds the target .bin + the sim, runs the scenarios):
python tools/dashboard_sim.py

# or via the test suite:
cd firmware/decompiled && cmake -B build_sim -S . -G Ninja && cmake --build build_sim
ctest --test-dir build_sim --output-on-failure        # firmware_tests + dashboard_sim

# or directly:
./build_sim/dashboard_sim[.exe] firmware/dashboard/build/dashboard_app.bin
```
Exit code 0 = all checks pass + "NO BRICK RISK DETECTED".

## Files
| File | Role |
|------|------|
| `sim_chip.h` | STM32F103 peripheral model: time, IWDG, USART, ADC, GPIO, flash brick audit, reset |
| `sim_dash_hal.cpp` | `dash::hal` implemented against `SimChip` (replaces `dash_hal.cpp`) |
| `sim_main.cpp` | scenario harness + no-brick verdict |
| `../include/dash_app.h` | the shared firmware logic both the target and the sim run |

## Instruction-exact simulation — Renode (implemented)
For opcode-accurate execution of the real `.elf`, the [`renode/`](renode/) folder
runs `dashboard_app.elf` on **Renode**'s emulated STM32F103 (Cortex-M3) with a
modeled IWDG — proving the 5000 ms watchdog on the actual binary (healthy → 1
boot; ADC-fault → resets every ~5 s). Run it with:
```bash
winget install Renode.Renode      # one-time
python tools/renode_dashboard.py  # builds DASH_DEBUG fw + runs both scenarios
```
The two simulators are complementary: this functional sim is instant and runs in
CI (catches logic/watchdog-discipline bugs); Renode runs the real opcodes (also
catches register-sequence/toolchain bugs). See [`renode/README.md`](renode/README.md).
