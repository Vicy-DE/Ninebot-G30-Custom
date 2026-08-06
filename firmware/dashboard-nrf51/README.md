# Custom G30 dashboard firmware — nRF51822

Replaces the stock dashboard application on the board's **only** MCU, an nRF51822-QFAA (Cortex-M0).
There is **no STM32** on this board — see
[`boards/ble-dashboard/MCU_IDENTIFICATION.md`](../../boards/ble-dashboard/MCU_IDENTIFICATION.md).

Requirements: [`Documentation/Requirements/dashboard-nrf51.md`](../../Documentation/Requirements/dashboard-nrf51.md) ·
RE source of truth: [`firmware/decompiled/nrf51822/RE_NRF51_DASHBOARD.md`](../decompiled/nrf51822/RE_NRF51_DASHBOARD.md)

## Build

```bash
make            # -> build/dashboard.bin  (Cortex-M0, links at 0x18000)
make test       # host tests, no hardware
make size
```

Current: **~5.6 KB** of the 144 KB app slot; SP `0x20004000`, reset vector inside the slot.

## Layout

| File | What |
|---|---|
| `include/dash_hal.h` | chip-independent HAL + the **recovered pin map** |
| `src/tm1637.cpp` | TM1637 display driver — the stock sequence, byte for byte |
| `src/nb_protocol.cpp` | Ninebot `5A A5` codec (LEN = payload, `sum ^ 0xFFFF` LE) |
| `src/dashboard.cpp` | state machine: telemetry → display, button → mode/light/power |
| `src/nrf51_hal.cpp` | nRF51 registers (GPIO, UART0, RTC1, WDT) — target only |
| `src/startup_nrf51.cpp` | vector table, `.data`/`.bss` init, freestanding C++ stubs |
| `nrf51822_app.ld` | app slot `0x18000`, RAM `0x20002000` (above the SoftDevice) |
| `sim/test_dashboard.cpp` | 35 host checks |

## Why the tests are meaningful

They check against **ground truth recovered from the stock firmware and the live scooter**, not
against themselves:

- the decoder accepts the frame captured off the real bus
  (`5A A5 05 21 20 65 00 04 28 22 02 00 04 FF`) and **re-encodes it byte-for-byte**;
- the TM1637 driver's waveform is sniffed by a simulated TM1637 and must produce exactly the stock
  9-byte sequence — `0x40` · `0xC0` + 6 grids · `0x88|brightness` — in three START/STOP phases;
- the font must equal the table extracted from the stock image at VMA `0x2046F`.

## Hardware facts baked in

| | Value | Source |
|---|---|---|
| Display | TM1637, 6 grids, **P0.04 = DIO, P0.05 = CLK**, LSB-first + ACK | `tm1637_write_byte` @`0x18E20` |
| Bus | 115200 8N1, **P0.15 / P0.20**, half-duplex by swapping PSELTXD/PSELRXD | `uart_init` @`0x1FDB4` |
| Checksum | `(~Σ) & 0xFFFF`, little-endian | `mvns`/`uxth` @`0x184B0` |
| Button | GPIOTE **PORT** + `PIN_CNF.SENSE` (wake-from-SYSTEMOFF) | helper @`0x1FC44` |
| Variant strap | **P0.08** input + pull-up, read once at boot | @`0x1A158` |

## Flashing

```bash
python ../../tools/nrf51/nrf51_swd.py dump          # BACK UP FIRST
make flash                                          # SWD, app slot 0x18000
```

Wiring: [`docs/WIRING_NRF51_SWD.md`](../../docs/WIRING_NRF51_SWD.md) ·
OTA route: [`docs/WIRING_OTA_UPDATE.md`](../../docs/WIRING_OTA_UPDATE.md)

## Not done yet

- BLE: the SoftDevice isn't driven yet (no advertising / NUS). The stock app uses S110 with 2 services
  + 3 characteristics; that work is next, along with the VESC-App-over-BLE bridge (R4.4/R5).
- The button's exact GPIO number is still a placeholder in `Dashboard::poll()` — the sensed pin comes
  from a runtime struct in the stock image. Read `PIN_CNF[0..31]` over SWD on a live board (the sensed
  pin has `SENSE` set) or trace the dash cable's green wire, then set it in `dash_hal.h`.
- Odometer persistence via `sd_flash_write` (R4.5).
