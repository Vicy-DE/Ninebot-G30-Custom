# Secure Boot Plan — Lock the Scooter to Your Own Signed Firmware

**Date:** 2026-06-09
**Status:** **plan / next step** — to be executed *after* the dashboard firmware
(`firmware/dashboard/`, with the 5000 ms watchdog) is proven on hardware.
**Goal (as requested):** enable secure boot on **every chip where it is possible**, so the scooter
**only runs firmware you have signed** — i.e. forbid the stock cloud, a thief, or anyone else from
replacing or tampering with your build.

> ## ✅ Verification (2026-06-13) — and the 5 bugs it caught
> The secure boot was **verified end-to-end** (`tools/verify_secureboot.py`, regression test
> `bootloader/tests/`): a genuinely PC-signed image is **accepted** and every tampering (firmware byte,
> signature byte, wrong key, wrong magic, wrong target) is **rejected** — using the bootloader's *own* C
> code. Getting there exposed **five bugs that made the "secure" boot non-functional**, now all fixed:
> 1. **Signer ↔ verifier format mismatch** — `sign_firmware.py` wrote magic `"SFW1"` with a different
>    field layout and no `header_crc32`; the bootloader expects `"SFW0"` (`fw_header.h`). The bootloader
>    would have rejected **every** signed image. → signer rewritten to the exact `sfw_header_t` layout.
> 2. **`bn_mod_mul` dropped the high 256 bits** of the 512-bit product (the reduction was a stub) → the
>    field multiply was wrong → **every ECDSA verify failed**. → replaced with correct bit-serial
>    reduction (`ecdsa.c`).
> 3. **`-Werror` dead code** (`verify_installed_app`, unused `lhs`) → the bootloader **didn't compile**.
> 4. **Missing `mem*`** (`-nostdlib` with no memcpy/memset/memcmp) → it **didn't link**. → `libc_min.c`.
> 5. **nRF51 missing `-lgcc`** (Cortex-M0 64-bit helpers `__aeabi_lmul/llsr`) → nRF51 **didn't link**.
>
> Now both targets build (STM32 ~6.2 KB, nRF51 ~6.5 KB, within 16 KB) and the crypto is sound. Re-run any
> time with `python tools/verify_secureboot.py` (part of the `/verify-safe` gate).
>
> **Stronger-policy note:** the current design is **verify-on-update** (the NBU-received `.sfw` is signature-
> checked before it is written; normal boot does only a fast structural check). For the "lock to my
> firmware even against an SWD flash" goal, add **verify-on-every-boot** (re-check the installed app's
> ECDSA signature before jumping) — the app must embed its `.sfw` header at a known offset. (§4 below.)

> The cryptographic core already exists: [`bootloader/`](../bootloader/) implements an **ECDSA-P256 +
> SHA-256** verified boot with an NBU framed-half-duplex updater and the `.sfw` signed-image format
> (`bootloader/common/`), with STM32 and nRF51 ports. This document is the **rollout + chip-lock plan**
> around it, not new crypto.

---

## 0. ⚠️ Read this first — secure boot can lock *you* out

Secure boot is deliberately hard to undo. Before enabling any chip lock:
- **Keep the ECDSA private key offline and backed up.** Lose it → you can never sign a new update →
  the device only ever runs the last image you signed.
- **STM32 RDP Level 2 is PERMANENT and irreversible** — it disables SWD/JTAG **forever** and freezes the
  option bytes. A bug after RDP2 = a paperweight (no debugger, no reflash). **Do not set RDP2 until the
  firmware is fully validated, and arguably never** — RDP Level 1 already blocks readout and is
  recoverable by mass-erase.
- Always keep a **recovery path** (stock images, an unlocked spare board, the SWD pads) until the very
  last, intentional step.
- These are **outward-facing, hard-to-reverse** changes. Do them one chip at a time, validating between.

---

## 1. Per-chip feasibility

| Chip | Secure boot? | Mechanism | Lock / anti-readout | Reversible? |
|------|--------------|-----------|---------------------|-------------|
| **BLE dashboard STM32F103C8** | ✅ yes | custom ECDSA-P256 bootloader @ `0x08000000` ([`bootloader/stm32`](../bootloader/stm32)) verifies the signed app @ `0x08004000` before jump | **WRP** (write-protect the bootloader pages) + **RDP Level 1** (block debug readout, mass-erase on downgrade) | RDP1/WRP: yes (erases on downgrade). **RDP2: NO — permanent.** |
| **nRF51822 (BLE SoC)** | ✅ yes | custom ECDSA bootloader @ `0x0003C000` ([`bootloader/nrf51`](../bootloader/nrf51)) + `UICR.BOOTLOADERADDR`; verify app above the SoftDevice | **APPROTECT** (UICR) disables SWD read/debug | yes — `ERASEALL` recovers (wipes the chip) |
| **BMS** | ➖ N/A in this build | stock STM32 BMS is **kept stock** (project rule) **or** replaced by the **Daly** (its own sealed MCU, not user-flashable) | — | — |
| **ESC → VESC** | ❌ not applicable | VESC runs open-source firmware with its own (non-cryptographic) bootloader; no signed-boot feature | set a **VESC Tool app password** + BLE pairing instead | n/a |

**Bottom line:** the two chips you can actually secure-boot are the **dashboard STM32** and the
**nRF51**. The BMS is stock/sealed and the VESC is open hardware — neither offers cryptographic secure
boot, so for those you rely on physical access control + VESC's app password, not signing.

---

## 2. Staged rollout (do in order; validate between stages)

This mirrors the deployment phases in [`guides/DEPLOYMENT.md`](guides/DEPLOYMENT.md) and `Req 5/6/7/12`.

1. **Gate:** the dashboard app (`firmware/dashboard/`) runs reliably on hardware — modes, throttle,
   display, app pairing, **and the 5000 ms watchdog** all verified (`Target/dashboard_watchdog_test.py`).
2. **Keys:** generate the project keypair offline — `python tools/signing/genkey.py` (→ private key in a
   safe place, public key embedded into the bootloader build). One keypair for the whole scooter.
3. **Sign:** `python tools/signing/sign_firmware.py --input dashboard_app.bin --output dashboard_app.sfw
   --target ble`. Build the bootloader with the public key:
   `cmake -B bootloader/build/ble -S bootloader -DTARGET_BOARD=BOARD_BLE_STM32 && cmake --build …`.
4. **Phase 2 (reversible):** flash the custom bootloader **as the "app"** at `0x08001000` via stock IAP
   and let it NBU-receive the signed app — proves the verify+jump works **without touching
   `0x08000000`**. Revert by reflashing stock.
5. **Phase 3 (SWD):** flash the custom bootloader at `0x08000000` + signed app at `0x08004000` (no-solder
   pogo SWD — see [`DASHBOARD_NO_SOLDER_FLASH.md`](DASHBOARD_NO_SOLDER_FLASH.md) §3). Confirm it **rejects
   an unsigned/modified image** (flip a byte → boot must refuse).
6. **Lock the STM32:** set **WRP** on the bootloader pages, then **RDP Level 1**. (Stop here. Only
   consider RDP2 if you accept a permanently undebuggable, unreflashable chip.)
7. **nRF51:** flash the custom DFU/bootloader + signed app via SWD, set `UICR.BOOTLOADERADDR`, verify it
   rejects unsigned images, then set **APPROTECT**.
8. **VESC + access control:** set a VESC Tool app password, lock BLE pairing; physically secure the deck.

Each `.sfw` image carries `magic "SFW0" | versions | target id | SHA-256 | ECDSA r||s` (Req 7); the
bootloader checks SHA-256 then the ECDSA-P256 signature against its embedded public key before jumping.

---

## 3. What "forbid what I do" buys you (and what it doesn't)

- **Buys:** only *your-signed* firmware boots on the dashboard + nRF51; the stock OTA/cloud cannot push a
  replacement that boots; a thief can't trivially reflash; readout protection hides your keys/secrets.
- **Doesn't:** it cannot secure-boot the VESC or the (stock/Daly) BMS; it does **not** stop someone with
  full physical access and lab gear from *erasing* the chip (which destroys your firmware too — that's
  the point of the lock, but it's not theft-proof, just tamper-evident + replacement-proof).
- If by "forbid what I do" you meant something different (e.g. anti-rollback, or locking out a specific
  feature), say so — the bootloader supports firmware-version anti-rollback in the `.sfw` header and we
  can wire that in.

---

## 4. Open items before executing
- [ ] Dashboard firmware HW-validated (the gate in §2.1).
- [ ] Decide the **RDP policy** (recommend **Level 1**, not Level 2) — this is irreversible if 2.
- [ ] Locate the **nRF51 SWD pads** (still undocumented — see the pinout research §B2).
- [ ] Confirm `tools/signing/` has a working `genkey`/`sign`/`verify` flow end-to-end.
- [ ] Decide whether to keep the stock BMS (secure-boot N/A) or go Daly (sealed MCU).

See also: [`Documentation/Requirements/requirements.md`](../Documentation/Requirements/requirements.md)
Req 5/6/7/12, [`bootloader/README.md`](../bootloader/README.md), [`guides/DEPLOYMENT.md`](guides/DEPLOYMENT.md).
