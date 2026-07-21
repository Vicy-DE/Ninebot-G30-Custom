---
description: Post-flash hardware verification over UART at 115200 8N1
argument-hint: "[COMport]"
---

Verify the board after flashing. Reference: `docs/guides/DEBUG.md` §6.

1. Open the monitor: `python -m serial.tools.miniterm <COM> 115200`.
2. Check the boot banner (`[BOOT] Secure Bootloader …`, target ID, signature status).
3. Read a protocol register to confirm comms, e.g. BLE version: `ninebot_flasher.py --port <COM> --read-register 0x21 0x17`.
4. Functional checks per board: throttle/brake/display/BLE (BLE), cell voltages (BMS), VESC motor response, advertising (nRF51).
5. Confirm rollback path still works (can re-enter IAP/NBU).
6. Record observations — they feed `/document`. Report each check as OK/FAIL with the observed output.
