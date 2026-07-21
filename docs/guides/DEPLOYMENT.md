# Deployment Strategy — Ninebot G30 Max Custom Firmware

## Overview

This document defines the **safe, incremental deployment strategy** for transitioning from stock firmware to custom firmware on the Ninebot G30 Max. The strategy minimizes brick risk by using reversible steps and validating each stage before proceeding.

## Target Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                   Ninebot G30 Max — Custom Build                        │
│                                                                         │
│  ┌──────────────┐    UART     ┌──────────────────────────────────┐     │
│  │   BMS Board  │◄──────────►│         VESC (replaces ESC)      │     │
│  │  STM32F103C8 │  115200    │                                   │     │
│  │  Custom FW   │  8N1       │  Benjamin Vedder ESC             │     │
│  │              │            │  USB + UART + CAN                │     │
│  └──────────────┘            └──────────┬────────────────────────┘     │
│                                         │ UART 115200 8N1              │
│                                         ▼                              │
│                              ┌──────────────────────┐                  │
│                              │   BLE Dashboard      │                  │
│                              │   STM32F103C8T6      │                  │
│                              │   Custom FW          │                  │
│                              │         ┌────────────┴──┐               │
│                              │         │  nRF51822     │               │
│                              │         │  Custom FW    │               │
│                              │         │  VESC App BLE │               │
│                              │         └───────────────┘               │
│                              └─────────────────────────┘               │
│                                                                         │
│  Features:                                                              │
│  - Original feature set (display, throttle, brake, lights)             │
│  - VESC App over BLE (nRF51822)                                        │
│  - No speed cap                                                         │
│  - VESC motor control (FOC, configurable)                              │
│  - Stock BMS with custom monitoring                                     │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## Deployment Phases

### Phase 0: Preparation (No Hardware Changes)

**Goal:** Establish tooling, backup everything, verify reverse engineering assumptions.

- [ ] Dump all stock firmware via SWD (full flash including bootloader region)
  - `boards/ble-dashboard/firmware/ble_full_dump.bin` (64 KB)
  - `boards/bms-battery/firmware/bms_full_dump.bin` (64 KB)
  - `boards/esc-motor/firmware/esc_full_dump.bin` (128 KB)
- [ ] Document stock bootloader behavior (IAP registers 0x07, 0x08, 0x09, 0x0A)
- [ ] Verify UART wiring between boards matches documented pinouts
- [ ] Test IAP flashing with **stock firmware** to confirm the update path works
- [ ] Install VESC hardware and verify basic motor operation with VESC Tool
- [ ] Generate ECDSA signing keys (`tools/signing/generate_keys.py`)
- [ ] Build and test all PC-side tools (flasher, signer, analysis)

**Exit criteria:** Can flash stock firmware via IAP and restore scooter to working state.

---

### Phase 1: Deploy Custom App via Stock OTA/IAP (Reversible)

**Goal:** Flash custom application firmware using the stock 4 KB bootloader's IAP mechanism. The stock bootloader at `0x08000000-0x08000FFF` remains untouched.

```
Memory layout during Phase 1:
0x08000000 ┌──────────────────────────┐
           │ STOCK BOOTLOADER (4 KB)  │  ← UNTOUCHED
0x08001000 ├──────────────────────────┤
           │ CUSTOM APP FIRMWARE      │  ← Deployed via stock IAP
           │ (up to 60 KB)            │
           │ - Ninebot protocol       │
           │ - VESC UART bridge       │
           │ - Throttle/brake/display │
           │ - Debug UART output      │
0x0800FFFF └──────────────────────────┘
```

**Steps:**
1. Build custom application firmware targeting `0x08001000` (stock app base)
2. Flash via stock IAP protocol:
   ```powershell
   python tools/flasher/ninebot_flasher.py --port COM3 --target ble --firmware build/ble_app_phase1.bin
   ```
3. Verify via UART monitor:
   - Custom app boots and prints identification banner
   - Throttle/brake ADC reads correctly
   - Display shows custom dashboard
   - VESC UART communication works
   - Ninebot protocol responses are correct (BLE registers)
   - nRF51822 BLE advertising works

**Validation checklist:**
- [ ] App boots from `0x08001000` via stock bootloader
- [ ] UART protocol responses match expected register map
- [ ] Throttle → VESC motor response works
- [ ] Display shows speed, battery, mode
- [ ] BLE phone connection works
- [ ] Can re-flash via stock IAP (reversibility confirmed)
- [ ] BMS communication works (cell voltages, temperature)

**Rollback:** Flash stock firmware via IAP — stock bootloader is still intact.

---

### Phase 2: Deploy Custom Bootloader to App Area (Reversible)

**Goal:** Test the custom 16 KB secure bootloader by placing it in the application area. The stock bootloader still handles initial boot, then jumps to our "app" which is actually the custom bootloader, which then jumps to the real app at a nested offset.

```
Memory layout during Phase 2:
0x08000000 ┌──────────────────────────────┐
           │ STOCK BOOTLOADER (4 KB)      │  ← UNTOUCHED - boots into 0x08001000
0x08001000 ├──────────────────────────────┤
           │ CUSTOM BOOTLOADER (16 KB)    │  ← Deployed as "app" via stock IAP
           │ - ECDSA signature verify     │
           │ - NBU receiver               │
           │ - SHA-256. Flash programming │
0x08005000 ├──────────────────────────────┤
           │ CUSTOM APP FIRMWARE (43 KB)  │  ← Deployed via custom bootloader NBU
           │ (vector table @ 0x08005000)  │
0x0800F800 ├──────────────────────────────┤
           │ CONFIG / FLAGS (2 KB)        │
0x0800FFFF └──────────────────────────────┘
```

**Steps:**
1. Build the custom bootloader **relocated into the app slot** (links it at `0x08001000`; its
   vector table/VTOR, app region `0x08005000` and flash bounds all derive from the base — see
   *Relocated "test-before-overwrite" build* below):
   ```powershell
   make -C bootloader/stm32 TARGET=ble BL_BASE=0x08001000
   python tools/verify_reloc_build.py --board ble --base 0x08001000   # assert it's correct + safe
   ```
2. Build custom app firmware with vector table at `0x08005000`
3. Flash bootloader-as-app via stock IAP:
   ```powershell
   python tools/flasher/ninebot_flasher.py --port COM3 --target ble --firmware build/ble_at_0x08001000/bootloader_ble_stm32_at_0x08001000.bin
   ```
4. Power cycle — stock bootloader jumps to 0x08001000 (custom bootloader)
5. Custom bootloader detects no valid app at 0x08005000 → enters NBU mode
6. Flash custom app via NBU:
   ```powershell
   python tools/flasher/nbu_send.py --port COM3 --target ble-stm32 --file build/ble_app_phase2.sfw
   ```
7. Custom bootloader verifies ECDSA signature → flashes app → reboots

**Validation checklist:**
- [ ] Stock bootloader chains to custom bootloader at 0x08001000
- [ ] Custom bootloader prints banner on UART
- [ ] NBU receive works (transfers .sfw file)
- [ ] ECDSA signature verification passes for valid firmware
- [ ] ECDSA signature verification rejects tampered firmware
- [ ] Custom app boots from 0x08005000 and functions correctly
- [ ] Update trigger (button hold) enters NBU mode
- [ ] Full feature set works with nested layout

**Rollback:** Flash stock firmware via stock IAP at 0x08001000 — overwrites custom bootloader.

#### Relocated "test-before-overwrite" build (`BL_BASE`)

The bootloader normally links at `0x08000000`. To test a bootloader build **before** committing it to
`0x08000000`, link it into an application slot instead and let the already-installed bootloader launch it
there — so a bad build can never brick the device, because `0x08000000` is never written:

```powershell
make -C bootloader/stm32 TARGET=ble BL_BASE=0x08004000   # link at +16 KB (app slot of the custom BL)
make -C bootloader/stm32 TARGET=ble BL_BASE=0x08001000   # link at +4 KB  (app slot of the stock 4 KB BL, Phase 2)
python tools/verify_reloc_build.py --board ble --base 0x08004000
```

Outputs go to `build/ble_at_<base>/bootloader_ble_stm32_at_<base>.{elf,bin,hex}`. From the base, the
Makefile + `bootloader_config.h` derive everything: the linker `ORIGIN`, the runtime `SCB->VTOR`
(so SysTick/exceptions vector into the relocated table), the app region (`base + 16 KB`), and the flash
erase/jump bounds. Because the flash driver refuses to erase anything below `APP_START_ADDR`, a build
relocated to `0x08004000` **physically cannot erase the real bootloader at `0x08000000`** — nor itself.
`tools/verify_reloc_build.py` asserts all of this (vector table location, launchable reset vector, 16 KB
fit, no overlap with `0x08000000`) and that the default build still vectors at `0x08000000`.

- `BL_BASE=0x08001000` → launched by the **stock 4 KB** bootloader (Phase 2, app at `0x08005000`).
- `BL_BASE=0x08004000` → launched by the **installed custom 16 KB** bootloader (validate a new BL build,
  app at `0x08008000`), before re-linking at `0x08000000` for the permanent Phase 3 flash.

---

### Phase 3: Flash Custom Bootloader to Bootloader Area (Permanent)

**Goal:** Replace the stock 4 KB bootloader with the custom 16 KB secure bootloader at `0x08000000`. This is the final layout. **This step is harder to reverse** — requires SWD to restore stock bootloader.

```
Memory layout during Phase 3 (FINAL):
0x08000000 ┌──────────────────────────────┐
           │ CUSTOM BOOTLOADER (16 KB)    │  ← Write-protected pages 0-15
           │ - ECDSA-P256 signature verify│
           │ - SHA-256 hash               │
           │ - NBU receiver               │
           │ - UART driver                │
           │ - Flash programming          │
           │ - Public key embedded        │
0x08004000 ├──────────────────────────────┤
           │ CUSTOM APP FIRMWARE (46 KB)  │  ← Signed + verified on boot
           │ - Vector table @ 0x08004000  │
           │ - Full application logic     │
0x0800F800 ├──────────────────────────────┤
           │ CONFIG / FLAGS (2 KB)        │
0x0800FFFF └──────────────────────────────┘
```

**⚠️ PREREQUISITES before Phase 3:**
1. Phase 2 is fully validated — custom bootloader works correctly
2. Stock firmware dumps are safely backed up
3. SWD recovery path is tested (can restore stock bootloader via ST-Link)
4. Custom bootloader has been running stably for multiple power cycles

**Steps:**
1. Build custom bootloader with final addresses (`APP_START_ADDR = 0x08004000`)
2. Build custom app firmware with vector table at `0x08004000`
3. The Phase 2 custom bootloader (running from app area) will self-relocate:
   ```powershell
   # From Phase 2 running state, use NBU to send the bootloader-flasher image
   python tools/flasher/update_bootloader.py --port COM3 --bootloader build/ble_bootloader_final.bin --app build/ble_app_final.sfw
   ```
4. The update tool:
   a. Sends a special "bootloader update" .sfw package
   b. Phase 2 bootloader erases pages 0-15 (stock bootloader area)
   c. Writes new 16 KB bootloader to 0x08000000
   d. Writes new app firmware to 0x08004000
   e. Sets write protection on bootloader pages
   f. Reboots

**Validation checklist:**
- [ ] Custom bootloader boots from 0x08000000
- [ ] App firmware runs from 0x08004000
- [ ] ECDSA signature verification works
- [ ] NBU update path works for future app updates
- [ ] Bootloader pages are write-protected
- [ ] Full scooter operation: throttle, brake, display, BLE, BMS, VESC

**Rollback:** SWD only — connect ST-Link, full chip erase, reflash stock bootloader + stock firmware.

---

### Phase 4: nRF51822 Firmware (After STM32 is Stable)

**Goal:** Deploy custom BLE firmware on the nRF51822 for VESC App and uncapped speed.

1. Custom BLE firmware provides:
   - VESC App BLE service (GATT characteristics for VESC Tool mobile)
   - Standard Ninebot BLE protocol (backward compatibility with phone apps)
   - Speed limit removal (no cap enforcement)
   - Real-time telemetry (speed, battery, motor temp via VESC)
2. Update via STM32 relay: STM32 on BLE board proxies NBU to nRF51 UART

---

### ~~Phase 5: BMS Custom Firmware~~ *(removed — BMS stays stock)*

BMS custom firmware is out of scope. The BLE firmware must support the stock BMS Ninebot protocol and optionally a Daly BMS via its own UART protocol.

---

## Deployment Summary

| Phase | Target | Area | Method | Reversible | Risk |
|-------|--------|------|--------|------------|------|
| 0 | All | — | Backup + tooling | Yes | None |
| 1 | BLE STM32 | App (0x08001000) | Stock IAP | Yes — reflash stock | Low |
| 2 | BLE STM32 | App (0x08001000) | Stock IAP | Yes — reflash stock | Low |
| 3 | BLE STM32 | Bootloader (0x08000000) | Custom BL self-update | SWD only | Medium |
| 4 | nRF51822 | App (0x00018000) | STM32 relay NBU | Via bootloader | Low |

---

## UART Debug Workflow Summary

Every flash operation follows this sequence:

```
1. BUILD    → cmake --build <target>
2. SIGN     → python tools/signing/sign_firmware.py (if using custom bootloader)
3. FLASH    → python tools/flasher/ninebot_flasher.py (stock IAP)
              OR python tools/flasher/nbu_send.py (custom bootloader)
4. MONITOR  → python -m serial.tools.miniterm COM3 115200
5. VERIFY   → Check boot messages, protocol responses, functional tests
6. DOCUMENT → Update CHANGE_LOG.md, PROJECT_DOC.md
7. TEST     → Run automated test scripts, save reports
8. COMMIT   → git add + git commit (never push)
```
