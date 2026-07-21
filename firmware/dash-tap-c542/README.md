# Dashboard-plug tap & bootloader-dump bridge — NUCLEO-C542RC

Use a **NUCLEO-C542RC** (STM32C542RCT6, Cortex-M33; the board behind the RC-Servo
project's "stm32c542") as a USB bridge that taps the **two signal wires of the
dashboard's internal plug** — one to the **BT** chip (nRF51822), one to the
**dashboard** MCU (STM32F103) — on the Arduino header, **figures out which wire is
which**, and bridges the active one to the PC so the existing dump/flash tools work.

## Why this board / these pins (verified)

| Fact | Source |
|------|--------|
| STM32C5 = Cortex-M33 @144 MHz family; Nucleo-64 **NUCLEO-C542RC** has on-board ST-LINK + Arduino Uno V3 headers | [ST STM32C5](https://www.st.com/en/microcontrollers-microprocessors/stm32c5-series.html) |
| **A0 = PA0**, **A2 = PA4** on the NUCLEO-C542RC Arduino header | board devicetree ([Zephyr](https://docs.zephyrproject.org/latest/boards/st/nucleo_c542rc/doc/index.html)): `ARDUINO_HEADER_R3_A0 → &gpioa 0`, `A2 → &gpioa 4` |
| Dashboard link = 115200 8N1, Ninebot `5A A5 | LEN | SRC | DST | CMD | ARG | … | CK` framing, `CK=Σ(LEN..payload)^0xFFFF` | this repo (RE_FINDINGS, protocol verified) |
| Internal STM32↔nRF link is a 2-wire UART (STM32 USART1 PA9/PA10 ↔ nRF UART0) | this repo PINOUT/architecture |

## Wiring

```
NUCLEO-C542RC                         Dashboard internal plug
  A0 (PA0) ───────── tap ───────────  plug wire #1  (BT / nRF  ── or ──  dashboard / STM32)
  A2 (PA4) ───────── tap ───────────  plug wire #2  (the other one)
  GND     ───────────────────────────  GND   (REQUIRED — common ground)
```

You do **not** need to know which wire is which — the firmware finds out. 3.3 V TTL only.
Keep the tap wires short. Power the dashboard as usual (it must be awake and chatting).

## Build (STM32CubeIDE + STM32CubeC5)

The classification brain — [`wire_finder.c`](wire_finder.c) — is plain, host-tested logic.
[`main.c`](main.c) is the board glue using the Cube **HAL**:

1. New STM32CubeIDE project for **NUCLEO-C542RC** (installs the STM32CubeC5 package).
2. In CubeMX, configure three UARTs @ **115200 8N1**:
   - **LINE0** on **PA0** (A0), RX (use single-wire **HDSEL** if a line is one-wire),
   - **LINE1** on **PA4** (A2), RX,
   - **VCP** on the USART wired to the on-board ST-LINK (CubeMX marks it).
   CubeMX picks the correct USART instance + alternate-function for PA0/PA4 from the
   STM32C542 pin map — the few spots that depend on it are tagged `<<< CONFIRM IN CUBEMX >>>`
   in `main.c`.
3. Add `wire_finder.c`/`.h` and `main.c` to the project; build + flash via the on-board ST-LINK.

## Use

1. Connect A0/A2/GND as above; open the C542's ST-LINK **Virtual COM Port** on the PC.
2. On reset the firmware sniffs both taps for ~4 s and prints a report, e.g.:
   ```
   [FIND] A0/PA0: 412 bytes, 7 frames (0 bad) — transmits SRC 0x21 -> BLE/nRF (BT) side
   [FIND] A2/PA4: 0 bytes, 0 frames (0 bad) — idle (no traffic)
   [FIND] bridging A0/PA0 (most Ninebot frames).
   ```
   then transparently bridges that wire to the VCP.
3. Run the dump through the bridge:
   ```powershell
   python tools/dump_bootloader_c5.py --port COM7
   ```
   The C542 is only the **transport**; reading the target's flash still uses the
   on-target dumper app (`firmware/bootloader-dumper/`, flashed via the stock IAP —
   see `docs/DASHBOARD_DUMP_C542.md`). Once the right wire is bridged, every existing
   UART tool (`dump_bootloader.py`, `ninebot_flasher.py`, `nbu_send.py`) works through
   the same COM port.

## Verify without hardware

```powershell
python tools/dash_tap_sim.py
```
Builds + runs the wire-finder logic test (`sim/test_wire_finder.cpp`) and the PC
parser selftest (`dump_bootloader_c5.py --selftest`). The board glue in `main.c` is
built/flashed separately in CubeIDE.

## What is verified vs. needs the bench

- **Verified (host):** the wire-finder classification (which tap carries Ninebot
  traffic + the transmitting SRC address), and the report/dump parsing end-to-end.
- **Needs your board/datasheet:** the exact USART instance + AF for PA0/PA4 and the
  VCP (resolved by CubeMX for the STM32C542), and the live capture.
