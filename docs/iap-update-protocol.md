# Ninebot G30 Max — IAP Firmware Update Protocol

## Overview

The Ninebot G30 Max uses In-Application Programming (IAP) to update firmware on all three boards (ESC, BLE, BMS) over the internal UART bus. This document describes the complete update protocol, including bootloader behavior, packet sequences, and hidden data on the boards.

## Memory Layout

All three boards use STM32F103 MCUs with the following flash memory layout:

```
┌─────────────────────────────────────────────────────────────────┐
│ 0x08000000  ┌──────────────────────────────────────────┐        │
│             │          BOOTLOADER (4 KB)               │        │
│             │     Cannot be overwritten by IAP         │        │
│             │     Handles firmware update reception    │        │
│ 0x08001000  ├──────────────────────────────────────────┤        │
│             │                                          │        │
│             │     APPLICATION FIRMWARE                 │        │
│             │     (28-32 KB for ESC, 14-34 KB others) │        │
│             │                                          │        │
│             │     Vector table at 0x08001000           │        │
│             │     Code entry: Reset_Handler            │        │
│             ├──────────────────────────────────────────┤        │
│             │     UNUSED / PADDING (0xFF)              │        │
│             ├──────────────────────────────────────────┤        │
│             │     UPDATE STAGING AREA (optional)       │        │
│             │     ESC: ~0x0800E800                     │        │
│             ├──────────────────────────────────────────┤        │
│             │     CONFIG / CALIBRATION BLOCK           │        │
│             │     (last few KB of flash)               │        │
│ 0x0801FFFF  └──────────────────────────────────────────┘  ESC   │
│ 0x0800FFFF  └──────────────────────────────────────────┘ BLE/BMS│
└─────────────────────────────────────────────────────────────────┘
```

### Per-Board Flash Details

| Board | MCU | Flash Size | Bootloader | App Region | Staging Area |
|-------|-----|-----------|------------|------------|--------------|
| **ESC** | STM32F103CBT6 | 128 KB | 0x08000000–0x08000FFF | 0x08001000–0x0800DFFF | ~0x0800E800 |
| **BLE** | STM32F103C8T6 | 64 KB | 0x08000000–0x08000FFF | 0x08001000–0x0800FFFF | — |
| **BMS** | STM32F103C8T6 | 64 KB | 0x08000000–0x08000FFF | 0x08001000–0x0800FFFF | — |

### Page Size

STM32F103 medium-density devices use **1 KB flash pages**. The bootloader occupies pages 0–3 (4 pages). Application firmware starts at page 4.

## Bootloader

### Hidden Data — Not In Firmware Images

The 4 KB bootloader at `0x08000000`–`0x08000FFF` is **NOT included** in the distributed `.bin` firmware files. It is factory-programmed and persists across firmware updates. The bootloader:

1. Executes on power-up / reset
2. Checks an **update flag** in flash (last page or SRAM marker)
3. If no update pending → jumps to application at `0x08001000`
4. If update pending → enters IAP receive mode
5. Receives firmware blocks via UART using the Ninebot protocol
6. Writes each block to flash starting at `0x08001000`
7. Verifies integrity (checksum)
8. Clears update flag and resets into the new application

### Bootloader Recovery

If the bootloader is corrupted or the chip's read protection is set, the **only** recovery method is SWD/JTAG via ST-Link. This requires a full-chip erase (which destroys the bootloader) and reflashing with a complete dump that includes the bootloader region.

### Update Flag Mechanism

The application firmware sets an **update control block** before resetting to enter the bootloader. This is stored in one of:
- The last flash page (persistent across resets)
- A specific SRAM address (lost if power cycled before bootloader runs)
- Backup registers (RTC domain, persistent with VBAT)

Evidence from firmware analysis:
- ESC firmware references `0x0800E800` (staging area address)
- BMS firmware references `0x08001000` (app base — used for self-validation)
- Flash control register (`0x40022000`) accessed 8–12 times per firmware

## Firmware Update Protocol — Complete Sequence

### Bus Topology During Update

```
┌─────────┐     UART (115200 8N1)     ┌─────────┐     UART      ┌─────────┐
│  Phone  │◄──── BLE ────────────────►│   ESC   │◄─────────────►│   BMS   │
│  App    │  (or USB-TTL adapter)     │ (hub)   │               │         │
└─────────┘                           └─────────┘               └─────────┘
   0x3E                                  0x20                      0x22

   The ESC acts as a bus router. All update commands flow through it.
   To update BLE: App → ESC → BLE (ESC forwards to USART2)
   To update BMS: App → ESC → BMS (ESC forwards to USART3)
   To update ESC: App → ESC directly
```

### Protocol Packet Format Reference

```
[0x5A] [0xA5] [LEN] [SRC] [DST] [CMD] [ARG] [PAYLOAD...] [CHK_LO] [CHK_HI]
```

- **LEN** = number of bytes from SRC through end of PAYLOAD = 4 + payload_length
- **Checksum** = `~(sum of bytes from LEN through end of PAYLOAD) & 0xFFFF`

### Step-by-Step Update Sequence

#### Phase 1: Initiation

The host (phone app or PC tool) initiates the update by writing to the target board:

```
Step 1: Send IAP Start Command
────────────────────────────────
Packet: Write to target register 0x07
  SRC  = 0x3E (App) or 0x3F (PC)
  DST  = target board (0x20=ESC, 0x21=BLE, 0x22=BMS)
  CMD  = 0x02 (Write)
  ARG  = 0x07 (IAP mode register)
  DATA = [firmware_size_L, firmware_size_H, version_L, version_H]
         (4 bytes: uint16 firmware size in bytes, uint16 version)

Example — initiate ESC update with 33388-byte firmware:
  5A A5 08 3E 20 02 07  6C 82 0D 06  XX XX
  │  │  │  │  │  │  │   │     │  │    └ Checksum
  │  │  │  │  │  │  │   │     │  └ Version 0x060D = 6.13
  │  │  │  │  │  │  │   │     └ Size high
  │  │  │  │  │  │  │   └ Size low: 0x826C = 33388
  │  │  │  │  │  │  └ Register 0x07 = IAP start
  │  │  │  │  │  └ CMD = Write
  │  │  │  │  └ DST = ESC
  │  │  │  └ SRC = App
  │  │  └ LEN = 4 + 4 = 8
  │  └ Header
  └ Header
```

The target board responds with a write acknowledgment, then sets the update flag and resets into its bootloader.

#### Phase 2: Bootloader Ready

After the target board resets, its bootloader initializes:
1. Configures UART at 115200 baud
2. Erases the application flash region (pages 4+)
3. Sends a ready signal (or the host polls for responsiveness)
4. Waits for firmware data blocks

The host should wait **500–1000 ms** after sending the IAP start command before sending data blocks, to allow the target to erase flash.

#### Phase 3: Data Transfer

Firmware data is sent in blocks of **64–128 bytes** (configurable, but 64 is most common). Each block is sent as a write command:

```
Step 2–N: Send Firmware Data Blocks
────────────────────────────────────
Packet: Write to target register 0x08
  SRC  = 0x3E (App)
  DST  = target board
  CMD  = 0x02 (Write)
  ARG  = 0x08 (IAP data block)
  DATA = [block_num_L, block_num_H, data_0, data_1, ... data_N]
         Block number (uint16, 0-indexed) + firmware bytes

Example — send first 64-byte block of firmware:
  5A A5 46 3E 20 02 08  00 00 [64 bytes of firmware] XX XX
  │     │              │  │
  │     │              │  └ Block number = 0
  │     │              └ Register 0x08 = IAP data
  │     └ LEN = 4 + 2 + 64 = 70 = 0x46
  └ Header

Block size = 64 bytes per block (community standard)
Total blocks = ceil(firmware_size / 64)
Last block may be shorter than 64 bytes
```

The bootloader writes each received block to flash at the appropriate offset:
- Block 0 → `0x08001000`
- Block 1 → `0x08001040` (if 64-byte blocks)
- Block N → `0x08001000 + (N × block_size)`

#### Phase 4: Verification

After all blocks are sent, the host sends a verify/finalize command:

```
Step N+1: Verify / Finalize
────────────────────────────
Packet: Write to target register 0x09
  SRC  = 0x3E (App)
  DST  = target board
  CMD  = 0x02 (Write)
  ARG  = 0x09 (IAP verify)
  DATA = [checksum_L, checksum_H, size_L, size_H]
         (4 bytes: uint16 checksum of entire firmware, uint16 size)

The bootloader computes its own checksum over the written data and
compares it with the received checksum. If they match, the update
is marked as successful.
```

#### Phase 5: Reset to Application

```
Step N+2: Reset into New Firmware
─────────────────────────────────
Packet: Write to target register 0x0A
  SRC  = 0x3E (App)
  DST  = target board
  CMD  = 0x02 (Write)
  ARG  = 0x0A (IAP reset)
  DATA = [] (empty or [0x01])

The bootloader clears the update flag and performs a system reset.
The new application firmware begins execution from 0x08001000.
```

### Complete Sequence Diagram

```
    Host (App/PC)                    Target Board
         │                                │
         │──── IAP Start (reg 0x07) ─────►│
         │     [size, version]             │
         │                                │
         │◄──── Write ACK ────────────────│
         │                                │
         │        (target resets to        │
         │         bootloader, erases     │
         │         flash — wait 500ms)    │
         │                                │
         │──── Data Block 0 (reg 0x08) ──►│
         │     [block=0, 64 bytes]        │──► Flash write @ 0x08001000
         │                                │
         │◄──── Write ACK ────────────────│
         │                                │
         │──── Data Block 1 (reg 0x08) ──►│
         │     [block=1, 64 bytes]        │──► Flash write @ 0x08001040
         │                                │
         │◄──── Write ACK ────────────────│
         │                                │
         │          ... repeat ...         │
         │                                │
         │──── Data Block N (reg 0x08) ──►│
         │     [block=N, remaining]       │──► Flash write @ last offset
         │                                │
         │◄──── Write ACK ────────────────│
         │                                │
         │──── Verify (reg 0x09) ─────────►│
         │     [checksum, size]            │──► Checksum validation
         │                                │
         │◄──── Verify ACK ───────────────│
         │                                │
         │──── Reset (reg 0x0A) ──────────►│
         │                                │──► Clear flag, NVIC_SystemReset()
         │                                │
         │     (target boots new firmware) │
         │                                │
```

### Timing and Reliability

| Parameter | Value | Notes |
|-----------|-------|-------|
| Post-IAP-start delay | 500–1000 ms | Allow flash erase |
| Inter-block delay | 20–50 ms | Wait for flash write + ACK |
| Block size | 64 bytes | Standard; some tools use 128 |
| ACK timeout | 2000 ms | Retry or abort if no ACK |
| Max retries | 3 | Per block |
| Total time (30 KB fw) | ~30–60 seconds | Depends on inter-block delay |

## Signature Verification Analysis

### Finding: NO Cryptographic Signature Verification

Binary analysis of all 6 firmware images reveals:

| Check | DRV_1.2.6 | DRV_1.6.13 | BLE_1.1.0 | BLE_1.1.7 | BMS_1.3.4 | BMS_1.7.4.5 |
|-------|-----------|-----------|-----------|-----------|-----------|-------------|
| RSA constants | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| ECC constants | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| SHA-256 init | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| AES S-box | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| CRC32 poly | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| TEA delta | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ |
| MD5 init | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| Flash unlock keys | ✅ | ✅ | ❌ | ❌ | ❌ | ✅ |

### What IS Present

1. **TEA/XTEA delta constant (0x9E3779B9)**: Found in ESC, BLE, and BMS_1.7.4.5 firmware. This is used for **XiaoTEA encryption/decryption** of firmware files during OTA transfer, NOT for signature verification. XiaoTEA is a transport-level cipher.

2. **MD5 init value (0x67452301)**: Found only in BLE firmware. Used for the **Xiaomi MiIO authentication protocol** (BLE pairing), not for firmware verification.

3. **Flash unlock keys (0x45670123, 0xCDEF89AB)**: Present in ESC and BMS firmware. These are the STM32 flash memory controller unlock keys — standard constants required to write to flash. Their presence confirms IAP capability.

### Security Implications

| Layer | Protection | Bypassable? |
|-------|-----------|-------------|
| **OTA Transport** | XiaoTEA encryption (.bin.enc) | Yes — known key, tools available |
| **BLE Pairing** | MiIO token authentication | Yes — after initial pairing |
| **IAP Flash Write** | 16-bit checksum (sum XOR 0xFFFF) | Yes — trivial to compute |
| **Code Signing** | **NONE** | N/A — not present |
| **Bootloader Lock** | Write-protected pages 0–3 | Not bypassed by IAP |
| **Read Protection** | STM32 RDP (optional, not factory-set) | Full erase via SWD |

**Conclusion**: Custom firmware can be flashed via serial IAP without needing to bypass any signature verification. The only protection is XiaoTEA encryption on OTA (BLE) transfers, which uses a known key.

## BLE Firmware — Special Note

The BLE `.bin` files (BLE_1.1.0, BLE_1.1.7) have an unusual characteristic: their reset handler points to address `0x00018155`, which is in the **nRF51822 address space** (flash starts at `0x00000000`), not the STM32 range (`0x08000000+`). This indicates the distributed BLE firmware binaries may be **nRF51822 SoftDevice+Application images**, not STM32 firmware.

The BLE dashboard has two MCUs:
- **STM32F103C8T6**: Handles throttle ADC, dashboard LEDs, UART to ESC
- **nRF51822**: Handles Bluetooth Low Energy, phone app communication

The BLE `.bin` files appear to be the nRF51822 firmware. The STM32 on the BLE board may receive its updates separately or be pre-programmed.

## BMS_1.3.4 — Encrypted/Obfuscated Image

The BMS_1.3.4 firmware has an invalid vector table (first word `0xF9C10082` is not a valid stack pointer) and moderate entropy (5.09 bits/byte). This suggests the binary is either:
1. **XiaoTEA-encrypted** (distributed as `.bin` but actually encrypted)
2. **Offset/scrambled** (needs a different base address)
3. **A different binary format** (bootloader-wrapped image with header)

The newer BMS_1.7.4.5 is a clean, unencrypted application image with a valid vector table. This suggests BMS_1.3.4 may have been distributed in encrypted form.

## XiaoTEA Encryption Details

For OTA (Bluetooth) firmware distribution, files are encrypted using XiaoTEA:

- **Algorithm**: Modified TEA (Tiny Encryption Algorithm)
- **Block size**: 8 bytes (64 bits)
- **Key size**: 16 bytes (128 bits)
- **Key derivation**: Based on "Ninebot Scooter" + model identifier
- **Mode**: ECB-like (each 8-byte block encrypted independently)
- **File extension**: `.bin.enc` (encrypted), `.bin` (plain)

XiaoTEA is used **only for BLE transport** (over-the-air updates via the phone app). Direct serial IAP and ST-Link SWD programming use **plain, unencrypted** `.bin` files.

Online decryption tool: [https://tools.scooterhacking.org/xiaotea/](https://tools.scooterhacking.org/xiaotea/)

## References

- [docs/protocol.md](protocol.md) — Base protocol format
- [docs/firmware-flashing.md](firmware-flashing.md) — Flashing methods overview
- [ESC-MotorController/PINOUT.md](../ESC-MotorController/PINOUT.md) — ESC pin assignments
- [BLE-Dashboard/PINOUT.md](../BLE-Dashboard/PINOUT.md) — BLE pin assignments
- [BMS-BatteryManagement/PINOUT.md](../BMS-BatteryManagement/PINOUT.md) — BMS pin assignments
- Community: [ScooterHacking Wiki](https://wiki.scooterhacking.org/)
