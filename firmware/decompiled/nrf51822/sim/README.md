# nRF51822 Bluetooth Firmware — BLE Session Simulator

Runs the real `Nrf51Firmware` (the reconstructed BLE bridge) on the host
`SimNrf51Hardware` and drives a **full BLE session**, so the Bluetooth firmware
can be tested without a phone, a radio, or the chip.

## Why functional, not Renode/QEMU
The dashboard STM32 firmware runs opcode-accurate in Renode. The **BLE firmware
can't**: Renode has no nRF51 platform (only nRF52840), and real BLE needs Nordic's
**proprietary SoftDevice** binary that no emulator can run meaningfully. So the
BLE side is simulated at the **firmware/HAL boundary** — `SimSoftDevice`,
`SimNrfUart`, `SimPsm` (`sim_nrf51.h`) model the SoftDevice/UART/storage, and the
**actual firmware logic** runs on top. (The nRF51 *core* logic that doesn't need
the SoftDevice — the UART bridge — could additionally be run on a real nRF51 core
via QEMU's `microbit` machine; not set up here.)

## What it exercises (`ble_sim`, 14 checks)
| Step | Proves |
|------|--------|
| 1. Advertising | advertises `NBScooter0001` (the name the phone app scans for) |
| 2. Connect | phone connects → advertising stops → MiIO auth starts |
| 3. MiIO pairing | full handshake → `FLASH_REGISTERED`, session token persisted to PSM |
| 4. **End-to-end read** | phone reads battery (reg 0x22): `5A A5 01 3E 20 01 22 02 …` relayed verbatim to the STM32, STM32 replies, phone is **notified** with `5A A5 02 20 3E 04 22 50 00 29 FF` (battery=80) |
| 5. Haystack | STM32 `0xAA` → HAYSTACK; a well-formed Apple FindMy advertisement is built; `0xAB` → NORMAL |
| 6. VESC tunnel | a VESC Tool packet frames + unframes (CRC ok) for the 2nd NUS |

Step 4's response frame is **identical** to the one the Renode dashboard test
produced — the two simulators corroborate each other on the wire format.

## Run it
```bash
python tools/ble_sim.py                       # build + run, prints the transcript
# or via the test suite:
cd firmware/decompiled && cmake -B build -S . -G Ninja && cmake --build build
ctest --test-dir build -R ble_sim --output-on-failure
```
Exit 0 on all-pass. Module-level unit coverage lives in
[`../../tests/test_nrf51822.cpp`](../../tests/test_nrf51822.cpp) (40+ tests:
relay, MiIO, PSM, checksum equivalence) and
[`../../tests/test_new_modules.cpp`](../../tests/test_new_modules.cpp)
(haystack / mode_ctrl / vesc_tunnel); `ble_sim` is the integrated scenario.
