# Requirements — custom nRF51822 dashboard firmware

**Target:** the G30 Max dashboard's **only** MCU, an nRF51822-QFAA (Cortex-M0, 256 KB flash, 16 KB RAM).
Supersedes every "dashboard STM32 app" requirement — that chip does not exist on the board
([`MCU_IDENTIFICATION.md`](../../boards/ble-dashboard/MCU_IDENTIFICATION.md)).

**Context:** the stock ESC is replaced by a **VESC**. The dashboard must therefore stop being a
Ninebot-protocol *peer of a Ninebot ESC* and become a **translator**: stock-looking UI on one side,
VESC on the other.

Priority: **M** = must, **S** = should, **C** = could.

---

## R1 — Do not brick, always recoverable

| # | Pri | Requirement | How it is fulfilled |
|---|-----|-------------|---------------------|
| R1.1 | M | A full, verified backup of stock flash exists before any write | `tools/nrf51/nrf51_dump.py` dumps SoftDevice+app+bootloader+UICR over SWD and verifies by re-read; `/verify-safe` gate |
| R1.2 | M | Any custom image can be reverted to stock | SWD reflash of the verified dump (`nrf51_flash.py --restore`) |
| R1.3 | M | Never write UICR/APPROTECT in a way that locks debug access | Flasher refuses to program `UICR.APPROTECT`; dump tool reads it and warns if already set |
| R1.4 | M | Watchdog recovers a hung dashboard | reuse stock WDT (`0x40010000`); feed only from the main loop |

## R2 — Restore the stock user interface

| # | Pri | Requirement | How it is fulfilled |
|---|-----|-------------|---------------------|
| R2.1 | M | Drive the stock display | **TM1637** driver, **P0.04=DIO / P0.05=CLK**, LSB-first + ACK, 6 grids, `0x40`/`0xC0`/`0x88\|brightness` — all recovered byte-exact |
| R2.2 | M | Show speed as digits | recovered 7-seg font `3F 06 5B 4F 66 6D 7D 07 7F 6F` (+`A-F`) |
| R2.3 | M | Show battery level, ride mode, headlight, Bluetooth state | remaining grids/segments of the same 6-byte frame |
| R2.4 | S | Brightness control | `0x88\|brightness` (0-7) already parameterised in `tm1637_update()` |
| R2.5 | M | Power button: short press = lights/mode, long press = power off | GPIOTE edge input (stock uses `0x40006000`) |

## R3 — Speak the Ninebot bus (stock-compatible)

| # | Pri | Requirement | How it is fulfilled |
|---|-----|-------------|---------------------|
| R3.1 | M | `5A A5` framing, `LEN`=payload, checksum `sum^0xFFFF`, 115200 8N1 | firmware- **and** bus-confirmed; `BAUDRATE=0x01D7E000` |
| R3.2 | M | Address as dashboard `0x21`; talk to ESC `0x20`, app `0x3E` | address immediates recovered from stock |
| R3.3 | M | Half-duplex turnaround on one wire pair | mirror stock: swap `PSELTXD`/`PSELRXD` between **P0.15/P0.20** |
| R3.4 | M | Emit `0x65` (throttle/brake) and consume `0x64` (telemetry) | matches live capture + `vesc-lisp/g30_dash.lisp` |
| R3.5 | S | Answer register reads (`0x01`) for serial/version so the stock app still works | reuse stock register map |

## R4 — VESC integration (the actual goal)

| # | Pri | Requirement | How it is fulfilled |
|---|-----|-------------|---------------------|
| R4.1 | M | Translate VESC telemetry → dashboard display | VESC packet client (`COMM_GET_VALUES`) → speed/battery → `tm1637_update()` |
| R4.2 | M | No stock ESC present must not produce a fault | dashboard is the *master* of its own UI; no comm-fault state when VESC answers |
| R4.3 | M | **Remove the speed cap** | no clamp on displayed/commanded speed; limits live in VESC config |
| R4.4 | S | Expose the **VESC App protocol over BLE** so VESC Tool can connect | BLE NUS ⇄ VESC UART bridge with VESC packet framing (`0x02 len payload crc16 0x03`) |
| R4.5 | S | Persist lifetime odometer across power cycles | flash page via SoftDevice `sd_flash_write` (stock does the same); wear-levelled append records |

## R5 — BLE

| # | Pri | Requirement | How it is fulfilled |
|---|-----|-------------|---------------------|
| R5.1 | M | Advertise + Nordic UART Service | S110 SoftDevice; `sd_ble_uuid_vs_add` + 1 service + 2 chars (RX write, TX notify) |
| R5.2 | S | Keep the stock app usable (`Encryption2` handshake) | implement `0x5B/0x5C/0x5D` with `SHA1(name‖…)` + AES-CCM as documented in `tools/ble/` |
| R5.3 | C | Optional open mode (no pairing) for VESC Tool | build-time flag; default **off** |

## R6 — Build / verify

| # | Pri | Requirement | How it is fulfilled |
|---|-----|-------------|---------------------|
| R6.1 | M | Builds with `arm-none-eabi-g++` for Cortex-M0, links above the SoftDevice at `0x18000` | `firmware/dashboard-nrf51/` linker script + Makefile |
| R6.2 | M | Core logic testable on the host (no hardware) | HAL is an interface; `sim/` builds the same C++ natively and asserts protocol/display output |
| R6.3 | M | Doxygen comment per function, `@sideeffects` on side-effecting ones | per `docs/guides/CODING.md` |
| R6.4 | M | `/verify-safe` passes before any flash | `tools/verify_firmware_safe.py` |

---

## Constraints & non-goals

- **16 KB RAM, 256 KB flash**, and the S110 SoftDevice occupies `0x00000000–0x00017FFF` — the app must
  stay above `0x18000` and leave the SoftDevice intact.
- **Do not flash the BMS.** Out of scope; always energised.
- The dashboard's stock **update path is auth-gated** (chip-UID password); custom firmware is installed
  over **SWD**, not over the bus — so SWD access must never be lost (R1.3).
