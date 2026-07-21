# Bootloader Migration Apps (stock 4 KB → custom 16 KB layout)

Migrating from the **stock 4 KB bootloader** (app @ `0x08001000`) to the **custom
16 KB secure bootloader** (app @ `0x08004000`) has a chicken-and-egg problem: the
stock IAP flashes apps at `0x08001000`, which **overlaps** the new 16 KB
bootloader's region — so you can't erase/install the new bootloader from there.

The fix (RC-Servo `bl_updater` model) is two apps, packed into one image:

```
  stock 4K BL @0x08000000 ──boots──► trampoline @0x08001000 ──jumps──► updater @0x08004000
                                                                          │ runs ABOVE the BL region
                                                                          ▼  (.ramfunc, from RAM)
                                              erase+write the new 16 KB bootloader @0x08000000
```

| App | Runs at | Job |
|-----|---------|-----|
| **trampoline** (`trampoline.cpp`, 376 B) | `0x08001000` | the stock BL boots it; it jumps to `0x08004000` (lets the old BL "start the new format") |
| **bl_updater** (`updater.cpp`, ~7 KB incl. embedded BL) | `0x08004000` | above the BL region → safely erases+writes the new 16 KB bootloader at `0x08000000`; verify-before-erase + read-back |
| **migrate_big.bin** (`pack.py`, ~19 KB) | flashed @`0x08001000` | the two packed into one image for the stock IAP |

**Brick-avoidance (RC-Servo model):** the flash erase/program primitives
([`flash_rt.c`](src/flash_rt.c)) run **from RAM** (`.ramfunc`, copied at startup),
with interrupts disabled — so erasing/writing `0x08000000` never fetches
instructions from the flash being programmed. The embedded bootloader is
integrity-checked **before** erase and **read-back-verified** after write.

Memory map (RC-Servo-aligned; fills the F103C8 64 KB) — see
[`docs/BOOTLOADER_V2_CONCEPT.md`](../../docs/BOOTLOADER_V2_CONCEPT.md):
`16K BL @0x08000000 · 40K app @0x08004000 · 4K factorydata @0x0800E000 · 4K userdata @0x0800F000`.

## Build + simulate
```bash
make                               # -> build/{trampoline,updater,migrate_big}.bin (embeds the built BL)
python ../../tools/renode_migration.py   # runs the migration in Renode + asserts
```
**Verified in Renode:** the trampoline jumps, the updater installs the new bootloader at `0x08000000`
(result cookie = BL_INSTALLED; `0x08000000` byte-identical to the embedded bootloader).

## Real-hardware flow (after dumping the stock BL — see `firmware/bootloader-dumper/`)
1. Dump + back up the stock 4 KB bootloader.
2. Flash `migrate_big.bin` via the stock IAP at `0x08001000`.
3. Power-cycle → trampoline → updater installs the new bootloader → it resets.
4. The new bootloader then receives the real signed app to `0x08004000` over NBU
   (framed half-duplex; the one-wire bus rules out a byte-stream like XMODEM)
   (production: the updater sets the new BL's forced-update flag before reset).

> ⚠️ This replaces the bootloader — the highest-risk step. Do it only after the
> stock BL is dumped/backed up and the SWD recovery path is tested. The updater
> currently checks the embedded image's CRC; production should verify an ECDSA
> trailer (the new BL is already signed-capable — `docs/SECURE_BOOT_PLAN.md`).
> The Renode model doesn't emulate the F103 FLASH *controller* (only the memory),
> so it validates the migration data-flow/addresses/decision logic, not the
> register-level program timing — do a bench SWD dry-run before trusting it.
