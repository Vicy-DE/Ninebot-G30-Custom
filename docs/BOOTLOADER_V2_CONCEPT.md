# Bootloader Concept v2 — RC-Servo-aligned layout + in-field bootloader update

**Date:** 2026-06-14
**Reference:** the **RC-Servo** project (`C:/Users/Layer/Documents/RC-Servo`) — a
multi-MCU secure bootloader with a `bl_updater` that reflashes the bootloader in
the field. This doc adopts its memory layout + updater mechanism for the Ninebot
STM32F103 bootloader, and defines the **stock→custom migration**.

---

## 1. Memory layout (matches RC-Servo; fills the F103C8 64 KB exactly)

| Region | Origin | Size | Purpose |
|--------|--------|------|---------|
| **Bootloader** | `0x08000000` | **16 KB** | ECDSA-P256 secure boot (`bootloader/`) |
| ↳ version (u64 ms) | `0x08003FB8` | 8 B | monotonic build timestamp (anti-rollback) |
| ↳ signature (r‖s) | `0x08003FC0` | 64 B | BL self-signature (verified by `bl_updater`) |
| **Application** | `0x08004000` | **40 KB** | signed app (and where `bl_updater` runs) |
| ↳ app signature | `0x0800DFC0` | 64 B | app ECDSA-P256 signature trailer |
| **Factory data** | `0x0800E000` | **4 KB** | UID-bound, signed; survives updates |
| **User data** | `0x0800F000` | **4 KB** | app runtime store — **lifetime odometer** (`odometer_store.h`) |

This is identical to RC-Servo's small-page (CH32/L432) variant and matches our
existing Phase-3 deployment (16 KB BL + app @ `0x08004000`). The two additions
vs. today's bootloader are the **factory-data** page (device-identity binding)
and the **user-data** page (where the always-on dashboard persists hours/km —
[`PROTOCOL_ODOMETER.md`](PROTOCOL_ODOMETER.md)).

> **Format note:** our current bootloader uses a **256-byte `SFW0` header** (now
> verified, `docs/SECURE_BOOT_PLAN.md`). RC-Servo uses a lighter **trailer**
> (version+signature at the end of each partition, no header). Both are valid;
> keep `SFW0` for the app (already implemented + tested) and add the **trailer
> version+signature on the bootloader image** so `bl_updater` can anti-rollback
> it. The reserved bytes `0x08003FB8..0x08003FFF` are inside the 16 KB BL region.

---

## 2. In-field bootloader update (`bl_updater`) — RC-Servo model

The bootloader region can't be reprogrammed by code running inside it, so the
updater runs from the **app slot** (`0x08004000`, above the BL) and:
1. embeds the new signed bootloader (`.incbin` → `.bl_image`, read-only, in the app region);
2. **verifies** the embedded image (CRC now; ECDSA-P256 trailer in production) **before** erasing;
3. checks **anti-rollback** (embedded version > flashed version, both inside the signed range);
4. erases + writes `0x08000000..0x08003FFF` using **`.ramfunc` flash ops that execute from RAM**
   with IRQs disabled (so it never fetches instructions from the flash being erased);
5. **read-back-verifies** the whole 16 KB; sets the new BL's forced-update flag and resets.

This is implemented for the F103 in [`firmware/migration/`](../firmware/migration/README.md)
(`flash_rt.c` = the `.ramfunc` half-word programming) and **Renode-verified**.

---

## 3. Versioning + anti-rollback
- Bootloader version = **`uint64_t` ms-since-epoch**, written by the signer into the trailer
  (inside the signature-covered bytes → tamper-evident).
- `bl_updater` refuses to install if the flashed BL has a valid signature and `embedded ≤ flashed`.
- It **will** install over an unsigned/blank bootloader regardless of version (first migration).
- (App-level anti-rollback is optional; RC-Servo binds the app to the chip UID via factory-data instead.)

---

## 4. Migration: stock 4 KB → custom 16 KB (the two apps)
The stock IAP flashes apps at `0x08001000`, which overlaps the 16 KB BL region.
Solved with **two packed apps** (`firmware/migration/`, Renode-verified):

```
 stock 4K BL ─► trampoline @0x08001000 ─► bl_updater @0x08004000 ─► installs new 16K BL @0x08000000
```
1. **Dump + back up** the stock bootloader (`firmware/bootloader-dumper/`).
2. Flash `migrate_big.bin` via stock IAP @ `0x08001000`.
3. Power-cycle → trampoline → updater installs the new BL → resets into it.
4. The new BL receives the real signed app to `0x08004000` over NBU (framed half-duplex; the one-wire
   bus rules out a byte-stream like XMODEM) — Phase 3 of `guides/DEPLOYMENT.md`.

**Reversible intermediate:** flashing **only** the trampoline lets the old 4 KB BL boot a
new-format app at `0x08004000` without replacing the bootloader (fully reflash-to-stock reversible).

---

## 5. Startup / VTOR (lesson from RC-Servo's `bootloader_startup_fix`)
RC-Servo hit a cold-boot vector/GP bug on **RISC-V** (flash physically at `0x00000000`). The
**STM32F103 doesn't have this** — BOOT0/BOOT1 map `0x00000000`→main flash in hardware, and the M3
has `SCB->VTOR`. The portable lesson kept here: **test on a true power-on reset, not just under the
debugger** (the debugger sets PC and masks reset-path bugs) — which is why the migration is validated
in Renode from the reset vector, and why each app sets its own `VTOR` in startup.

---

## 6. Status
- ✅ 16 KB secure bootloader builds (STM32 + nRF51) and is crypto-verified (`docs/SECURE_BOOT_PLAN.md`).
- ✅ `bl_updater` migration (trampoline + updater + packed image) built + Renode-verified
  (`tools/renode_migration.py`).
- ✅ User-data odometer store implemented + host-tested (`firmware/decompiled/ble/include/odometer_store.h`).
- ⬜ Add the **version+signature trailer** to the bootloader image + signer; switch the updater's CRC
  check to ECDSA. ⬜ Factory-data UID binding. ⬜ Bench SWD dry-run before a real bootloader replace.
