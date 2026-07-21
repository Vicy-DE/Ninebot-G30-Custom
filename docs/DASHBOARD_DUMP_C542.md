# Dumping the dashboard bootloader via a NUCLEO-C542RC tap

Procedure for using a **NUCLEO-C542RC** as a USB tap/bridge on the dashboard's
internal plug to dump the stock bootloader. Build/wiring details:
[`firmware/dash-tap-c542/README.md`](../firmware/dash-tap-c542/README.md).

## Hardware verification (sourced)

- **STM32C5** is a real Cortex-M33 @144 MHz family (announced 2026-03); the Nucleo-64
  **NUCLEO-C542RC** (STM32C542RCT6) has an on-board ST-LINK and Arduino Uno V3 headers
  — ST [STM32C5 series](https://www.st.com/en/microcontrollers-microprocessors/stm32c5-series.html).
  It is the board behind the RC-Servo project's "stm32c542".
- On that board, **A0 = PA0** and **A2 = PA4** (Arduino header → GPIO), confirmed from
  the board devicetree (`ARDUINO_HEADER_R3_A0 → &gpioa 0`, `A2 → &gpioa 4`) —
  [Zephyr nucleo_c542rc](https://docs.zephyrproject.org/latest/boards/st/nucleo_c542rc/doc/index.html).
- A0/A2 = PA0/PA4 are GPIO/USART pins (not the ST-LINK SWD pins), so the **C5 MCU itself**
  bridges to the dashboard — consistent with this project's UART-IAP path.

## What the tap can and cannot do

The C542 is the **transport**: it taps the two plug wires, identifies which carries the
Ninebot traffic, and bridges it to the PC. Reading the target's flash still uses the
**on-target dumper app** (`firmware/bootloader-dumper/`), which is flashed via the stock
IAP and emits the 4 KB stock bootloader (`0x08000000–0x08000FFF`) as `==NBDUMP==` framed
hex + CRC32. The tap does not by itself read flash — it carries the bytes.

## Steps

1. **Wire** A0(PA0), A2(PA4), GND to the two plug wires + ground (either order — the tool
   finds out which is which). See the firmware README.
2. **Flash + run** the C542 tap firmware (CubeIDE, STM32CubeC5). On reset it prints the
   `[FIND]` report and bridges the active wire to its ST-LINK VCP.
3. **Identify** the wires from the report (which is BT/nRF, which is dashboard/STM32) —
   useful on its own for RE of the inter-chip link.
4. **Flash the dumper app** onto the dashboard STM32 via the stock IAP through the bridge
   (`tools/flasher/ninebot_flasher.py --port <C542 VCP>`), if not already resident.
5. **Capture** the dump:
   ```powershell
   python tools/dump_bootloader_c5.py --port <C542 VCP>
   ```
   It shows the wire-finder verdict, then writes the CRC-checked `.bin` to
   `boards/ble-dashboard/firmware/BLE_bootloader_dump.bin`.

## Verification status

- **Verified without hardware** (`python tools/dash_tap_sim.py`): the wire-finder
  classifies the two taps (Ninebot frames + transmitting SRC address → BT vs dashboard)
  and the dump parses end-to-end through the bridge text. Logic test 9/9, parser
  selftest 6/6.
- **Needs your bench:** the exact USART instance/AF for PA0/PA4 + the VCP (CubeMX resolves
  these for the STM32C542) and the live capture.

## Software-UART IAP programmer + the 16/32 self-update chain

The C542 can also be the **IAP programmer**: it bit-bangs the Ninebot/NBU protocol on
**PA0 (A0)** with a software UART (no hardware-USART AF needed) and flashes signed
images to the target bootloader. Firmware: `firmware/dash-tap-c542/main_programmer.c`
(glue) over the host-verified cores `soft_uart.c` + `nbu_prog.c`. PC feed:
`tools/flasher/c5_feed.py --port <C542 VCP> --file <image>.sfw`.

This drives the **test-before-overwrite** chain entirely above `0x08000000`:

1. The relocated bootloader at **0x08004000** (16 offset; `make … BL_BASE=0x08004000`)
   receives a signed app over NBU and writes it to the app slot at **0x08008000**
   (32 offset), accepting it **only if the ECDSA signature verifies**.
2. That app is the **installer** (`firmware/migration16/`, runs at 0x08008000): from
   RAM it erases+writes a bootloader back to **0x08004000**, verify-before-erase +
   read-back. The flash guard makes it physically unable to touch `0x08000000`.

### Verify BEFORE flashing (mandatory)

```powershell
python tools/verify_c5_flash.py     # must print: VERDICT: GO
```
This proves the whole flow in simulation with the **real** firmware code:
- software-UART bit codec + framing (`test_wire_finder`, 9/9),
- the C542 software-UART NBU/IAP programmer flashing the real `nbu.c` receiver
  byte-for-byte through the bit-level UART (`test_c5_prog`),
- BL@16 flashing a signed app→32 (tamper **rejected**) and installer@32 writing BL→16
  with `0x08000000` provably untouched (`test_iap_chain`, real ECDSA verify, 11/11),
- the on-target binaries build (relocated BL + installer@32).

Only on **GO** do you flash hardware: feed the installer `.sfw` to the C542 programmer
(`c5_feed.py`), which programs it to 0x08008000 via the relocated BL@0x08004000; the
installer then writes the new BL to 0x08004000.

## Alternative — on-board ST-LINK SWD

If readout protection (RDP) is **off**, the NUCLEO's on-board ST-LINK can dump the whole
STM32F103 flash (bootloader included) directly over **SWD** (PA13/PA14), no app needed —
but that uses the ST-LINK SWD header, not the Arduino A0/A2 pins, and a potted dashboard
PCB rarely exposes SWD. The A0/A2 UART-tap path here is the no-SWD route.
