# ToDo — Secure Boot on Every Possible Chip

**Feature:** lock the scooter so only owner-signed firmware boots (Req 5/6/7/12).
**Plan:** [`docs/SECURE_BOOT_PLAN.md`](../../docs/SECURE_BOOT_PLAN.md). **Conditional:** start only after
the dashboard firmware (`firmware/dashboard/`, 5000 ms watchdog) is hardware-validated.

## Gate (must pass first)
- [ ] Dashboard app runs reliably on HW: throttle, modes, display, app pairing, **watchdog** verified
      (`Target/dashboard_watchdog_test.py`, `verify-hw`).
- [ ] Confirm `tools/signing/` genkey → sign → verify works end-to-end on a `.sfw`.

## STM32 dashboard (can secure-boot ✅)
- [ ] Generate project ECDSA-P256 keypair offline; back up the private key; embed the public key.
- [ ] Build `bootloader/stm32` (BOARD_BLE_STM32) with the public key; sign `dashboard_app.bin` → `.sfw`.
- [ ] Phase 2: bootloader-as-app @ `0x08001000`, NBU the signed app — validate verify+jump (reversible).
- [ ] Phase 3 (SWD pogo): bootloader @ `0x08000000` + signed app @ `0x08004000`; confirm it **rejects a
      tampered image** (flip one byte → must refuse to boot).
- [ ] Set **WRP** on bootloader pages; set **RDP Level 1** (NOT Level 2 — permanent/irreversible).

## nRF51822 (can secure-boot ✅)
- [ ] Locate the nRF51 SWD pads (open item — pinout research §B2).
- [ ] Flash `bootloader/nrf51` DFU/bootloader @ `0x0003C000` + signed app; set `UICR.BOOTLOADERADDR`.
- [ ] Confirm unsigned-image rejection; set **APPROTECT** (recoverable via ERASEALL).

## BMS / VESC (secure-boot N/A)
- [ ] BMS: kept stock or replaced by Daly (sealed MCU) — no action.
- [ ] VESC: open firmware, no crypto secure boot — set a VESC Tool app password + lock BLE pairing.

## Decisions to confirm with the owner
- [ ] **RDP policy**: recommend Level 1 (recoverable). Level 2 permanently disables SWD — confirm intent.
- [ ] Anti-rollback: enable firmware-version downgrade protection in the `.sfw` header? (supported)
- [ ] Single keypair for the whole scooter vs per-chip keys.
