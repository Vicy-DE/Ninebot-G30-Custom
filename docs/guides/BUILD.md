# Build Guide — Ninebot G30 Max Custom Firmware

## Tools

| Tool | Source |
|------|--------|
| arm-none-eabi-gcc | ARM cross compiler (Cortex-M0 / Cortex-M3) |
| CMake + Ninja | Build system for bootloader and firmware |
| Python 3.9+ | For signing, flashing, and analysis scripts |
| STM32CubeProgrammer | For SWD recovery only (optional) |

---

## Architecture

Three independent firmware targets built from a shared codebase:

| Target | Board | MCU | Flash | Compiler Flags |
|--------|-------|-----|-------|---------------|
| `ble` | BLE Dashboard | STM32F103C8T6 | 64 KB | `-DTARGET_BOARD=BOARD_BLE_STM32` |
| `bms` | BMS Battery | STM32F103C8T6 | 64 KB | `-DTARGET_BOARD=BOARD_BMS_STM32` |
| `nrf51` | BLE Bluetooth | nRF51822 | 256 KB | `-DTARGET_BOARD=BOARD_NRF51` |

The ESC board is replaced by a **VESC** motor controller — no custom ESC firmware is built.

---

## 1. First-Time Setup

```powershell
# Install ARM toolchain (if not already installed)
# Windows: download from https://developer.arm.com/downloads/-/gnu-rm
# Or via scoop/chocolatey:
scoop install gcc-arm-none-eabi
# Or:
choco install gcc-arm-embedded

# Verify toolchain
arm-none-eabi-gcc --version

# Install Python dependencies
pip install -r tools/signing/requirements.txt
pip install pyserial
```

---

## 2. Build — Bootloader

### Configure (once per target)

```powershell
# BLE STM32 bootloader
cmake -B bootloader/build/ble -S bootloader -G Ninja -DTARGET_BOARD=BOARD_BLE_STM32

# BMS STM32 bootloader
cmake -B bootloader/build/bms -S bootloader -G Ninja -DTARGET_BOARD=BOARD_BMS_STM32

# nRF51822 bootloader
cmake -B bootloader/build/nrf51 -S bootloader -G Ninja -DTARGET_BOARD=BOARD_NRF51
```

### Build

```powershell
# Build specific target
cmake --build bootloader/build/ble
cmake --build bootloader/build/bms
cmake --build bootloader/build/nrf51

# Or build all
cmake --build bootloader/build/ble ; cmake --build bootloader/build/bms ; cmake --build bootloader/build/nrf51
```

### Build Outputs

| File | Location | Description |
|------|----------|-------------|
| BLE bootloader | `bootloader/build/ble/ble_bootloader` | ELF binary |
| BMS bootloader | `bootloader/build/bms/bms_bootloader` | ELF binary |
| nRF51 bootloader | `bootloader/build/nrf51/nrf51_bootloader` | ELF binary |

---

## 3. Build — Application Firmware

```powershell
# BLE application firmware
cmake -B firmware/decompiled/build -S firmware/decompiled -G Ninja
cmake --build firmware/decompiled/build
```

---

## 4. Sign Firmware

All firmware images must be signed before deployment:

```powershell
# Generate signing keys (once)
python tools/signing/generate_keys.py

# Sign a firmware binary
python tools/signing/sign_firmware.py --input build/ble_app.bin --output build/ble_app.sfw --target ble

# Verify signature
python tools/signing/verify_firmware.py build/ble_app.sfw
```

---

## 5. Memory Maps

### STM32F103C8T6 (BLE / BMS) — 64 KB Flash

```
Stock layout:                    Custom layout:
0x08000000 ┌──────────────┐     0x08000000 ┌──────────────┐
           │ Stock BL 4KB │                │ Secure BL 16K│
0x08001000 ├──────────────┤     0x08004000 ├──────────────┤
           │ App (~60 KB) │                │ App (46 KB)  │
0x0800FFFF └──────────────┘     0x0800F800 ├──────────────┤
                                           │ Config 2 KB  │
                                0x0800FFFF └──────────────┘
```

### nRF51822 — 256 KB Flash

```
0x00000000 ┌──────────────┐
           │ MBR (4 KB)   │  ← Nordic factory
0x00001000 ├──────────────┤
           │ SoftDevice   │  ← BLE stack (92 KB)
0x00018000 ├──────────────┤
           │ App (~80 KB) │  ← Custom BLE firmware
0x00030000 ├──────────────┤
           │ Staging 48KB │  ← Update buffer
0x0003C000 ├──────────────┤
           │ Bootloader   │  ← 16 KB secure bootloader
0x0003FC00 ├──────────────┤
           │ Settings 1KB │
0x00040000 └──────────────┘
```

---

## 6. Cross-Board Compilation

Board-specific code uses preprocessor guards:

```c
#if TARGET_BOARD == BOARD_BLE_STM32
    /* BLE-specific: throttle ADC, display, VESC UART bridge */
#elif TARGET_BOARD == BOARD_BMS_STM32
    /* BMS-specific: BQ76940 I2C, cell monitoring */
#elif TARGET_BOARD == BOARD_NRF51
    /* nRF51-specific: SoftDevice BLE, NUS service */
#endif
```

Common code (protocol, XMODEM, crypto) is shared across all targets via `bootloader/common/` and `lib/ninebot-protocol/`.
