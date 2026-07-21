---
description: Flash firmware via stock IAP or NBU (asks for port/target)
argument-hint: "[ble|bms|nrf51] [--nbu]"
---

Flash firmware to the target board. Reference: `docs/guides/DEBUG.md` §3. **Confirm the COM port and
target with the user before running** — flashing is hardware-affecting and not auto-approved.

- Stock IAP (reversible, Phase 1): `python tools/flasher/ninebot_flasher.py --port <COM> --target <t> --firmware <build>.bin`
- Custom bootloader NBU (framed half-duplex; the bus is one-wire half-duplex): first
  `python tools/signing/sign_firmware.py --input <build>.bin --output <build>.sfw --target <t>`,
  then `python tools/flasher/nbu_send.py --port <COM> --target <ble-stm32|bms-stm32|nrf51822> --file <build>.sfw`
- SWD is for recovery/initial bootstrap only.

After flashing, proceed to `/verify-hw`. Never flash custom firmware to the BMS board.
