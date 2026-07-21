# Ninebot G30 Max — Secure Custom Bootloader Concept

## Hardware Configuration

| Role | Board | Hardware | Bootloader? |
|------|-------|----------|-------------|
| Motor Controller | ESC | **VESC** (3rd party) | No — uses VESC firmware |
| Dashboard | BLE | **Original** (STM32F103C8T6 + nRF51822) | **Yes** — both MCUs |
| Battery | BMS | **Original** (STM32F103C8T6 + BQ76940) | **Yes** — STM32 |

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      Ninebot G30 Max — Secure Boot                     │
│                                                                        │
│    ┌──────────────┐    UART     ┌──────────────────────────────────┐   │
│    │   BMS Board  │◄──────────►│           VESC (ESC)             │   │
│    │  STM32F103C8 │  115200    │                                  │   │
│    │              │  8N1       │  Acts as UART bus hub             │   │
│    │ ┌──────────┐ │            │  Passthrough mode for updates    │   │
│    │ │SecureBoot│ │            │                                  │   │
│    │ │ECDSA+XMO│ │            └──────────┬───────────────────────┘   │
│    │ └──────────┘ │                      │ UART 115200 8N1           │
│    └──────────────┘                      ▼                           │
│                              ┌──────────────────────┐                │
│                              │   BLE Dashboard      │                │
│                              │   STM32F103C8T6      │                │
│                              │  ┌──────────┐        │                │
│                              │  │SecureBoot│  USART1│                │
│                              │  │ECDSA+XMO│◄──────►│                │
│                              │  └──────────┘  ┌─────┴──────┐        │
│                              │                │  nRF51822   │        │
│                              │                │ ┌─────────┐ │        │
│                              │                │ │SecureBoot│ │        │
│                              │                │ │ECDSA+XMO│ │        │
│                              │                │ └─────────┘ │        │
│                              │                └─────────────┘        │
│                              └──────────────────────────────┘        │
│                                                                      │
│  Update Tool (PC)                                                    │
│    USB-UART ──► VESC passthrough ──► target board bootloader         │
│    sign_firmware.py ──► signed .sfw image ──► NBU framed transfer  │
└──────────────────────────────────────────────────────────────────────┘
```

## Security Design

### Firmware Signing

All custom firmware images are signed using **ECDSA-secp256r1** (NIST P-256):

1. Developer builds firmware binary (`.bin`)
2. `sign_firmware.py` computes SHA-256 hash of the binary
3. Signs the hash with the developer's **private key**
4. Produces a `.sfw` (Signed Firmware) file with header + binary + signature

### Boot Verification Flow

```
Power On / Reset
       │
       ▼
┌──────────────┐
│  Bootloader  │
│  starts      │
└──────┬───────┘
       │
       ▼
┌──────────────────┐     Yes    ┌──────────────────┐
│ Update trigger?  │───────────►│ Enter NBU        │
│ (button/flag)    │            │ receive mode     │
└──────┬───────────┘            └──────┬───────────┘
       │ No                            │
       ▼                               ▼
┌──────────────────┐            ┌──────────────────┐
│ Verify app sig   │            │ Receive .sfw via │
│ SHA-256 + ECDSA  │            │ NBU (framed)     │
└──────┬───────────┘            └──────┬───────────┘
       │                               │
  ┌────┴────┐                          ▼
  │         │                   ┌──────────────────┐
 Valid    Invalid               │ Verify header +  │
  │         │                   │ ECDSA signature   │
  ▼         ▼                   └──────┬───────────┘
┌──────┐ ┌──────────┐            ┌────┴────┐
│ Jump │ │ Stay in  │           Valid    Invalid
│ to   │ │ bootload │            │         │
│ app  │ │ (NBU)    │            ▼         ▼
└──────┘ └──────────┘     ┌──────────┐ ┌────────┐
                          │ Flash +  │ │ Reject │
                          │ reboot   │ │ NACK   │
                          └──────────┘ └────────┘
```

### Key Management

| Key | Location | Purpose |
|-----|----------|---------|
| Private key (PEM) | **Developer PC only** — never on device | Signs firmware images |
| Public key (raw) | Embedded in bootloader flash (read-only) | Verifies signatures |

The 64-byte raw public key (uncompressed X,Y coordinates) is compiled into each bootloader binary and lives in the write-protected bootloader flash region.

## Target MCUs & Memory Maps

### 1. BLE Dashboard — STM32F103C8T6 (64 KB Flash)

```
0x08000000 ┌──────────────────────────────────┐
           │   SECURE BOOTLOADER (16 KB)      │  ← Write-protected (pages 0-15)
           │   - Vector table                 │
           │   - ECDSA-secp256r1 verify       │
           │   - SHA-256                       │
           │   - NBU update receiver          │
           │   - UART driver (USART2→VESC)    │
           │   - Flash programming            │
           │   - Public key (64 bytes)        │
0x08004000 ├──────────────────────────────────┤
           │   APPLICATION FIRMWARE (46 KB)   │  ← Signed + verified
           │   - Vector table @ 0x08004000    │
           │   - Dashboard control logic      │
           │   - Throttle/brake ADC           │
           │   - Display driving              │
           │   - UART protocol (VESC + nRF)   │
           │   - nRF51822 relay/update proxy  │
0x0800F800 ├──────────────────────────────────┤
           │   CONFIG / FLAGS (2 KB)          │  ← Update flags, calibration
           │   - Update request flag          │
           │   - Boot counter                 │
           │   - Calibration data             │
0x0800FFFF └──────────────────────────────────┘
```

**UART assignment:**
- USART2 (PB6/PB7) → VESC (update + runtime communication)
- USART1 (PA9/PA10) → nRF51822 (internal bridge)

### 2. BLE Dashboard — nRF51822 (256 KB Flash)

```
0x00000000 ┌──────────────────────────────────┐
           │   MBR (Master Boot Record, 4 KB) │  ← Nordic factory, untouched
0x00001000 ├──────────────────────────────────┤
           │   SoftDevice S110 v8.0 (92 KB)   │  ← BLE stack, untouched
0x00018000 ├──────────────────────────────────┤
           │   APPLICATION FIRMWARE (~80 KB)  │  ← Signed + verified
           │   - BLE advertising/connection   │
           │   - NUS GATT service             │
           │   - Ninebot protocol relay       │
           │   - MiIO authentication          │
0x00030000 ├──────────────────────────────────┤
           │   FREE / STAGING (48 KB)         │  ← Receive buffer for updates
0x0003C000 ├──────────────────────────────────┤
           │   SECURE BOOTLOADER (16 KB)      │  ← Write-protected via UICR
           │   - ECDSA-secp256r1 verify       │
           │   - SHA-256                       │
           │   - NBU update receiver          │
           │   - UART driver (UART0→STM32)    │
           │   - Flash programming (via MBR)  │
           │   - Public key (64 bytes)        │
0x0003FC00 ├──────────────────────────────────┤
           │   BOOTLOADER SETTINGS (1 KB)     │  ← Boot flags, version
0x00040000 └──────────────────────────────────┘

  UICR:
    0x10001014 BOOTLOADERADDR = 0x0003C000
```

**UART assignment:**
- UART0 → BLE STM32 (receives NBU frames relayed from VESC)

### 3. BMS — STM32F103C8T6 (64 KB Flash)

```
0x08000000 ┌──────────────────────────────────┐
           │   SECURE BOOTLOADER (16 KB)      │  ← Write-protected (pages 0-15)
           │   - Vector table                 │
           │   - ECDSA-secp256r1 verify       │
           │   - SHA-256                       │
           │   - NBU update receiver          │
           │   - UART driver (USART2→VESC)    │
           │   - Flash programming            │
           │   - Public key (64 bytes)        │
0x08004000 ├──────────────────────────────────┤
           │   APPLICATION FIRMWARE (46 KB)   │  ← Signed + verified
           │   - Vector table @ 0x08004000    │
           │   - BQ76940 I2C management       │
           │   - Cell balancing               │
           │   - Protection logic             │
           │   - UART protocol (→ VESC)       │
0x0800F800 ├──────────────────────────────────┤
           │   CONFIG / FLAGS (2 KB)          │  ← Update flags, calibration
           │   - Update request flag          │
           │   - Cell calibration offsets     │
           │   - Cycle count                  │
0x0800FFFF └──────────────────────────────────┘
```

**UART assignment:**
- USART2 (PA2/PA3) → VESC (update + runtime communication)
- USART1 (PA9/PA10) → Debug/factory (alternative update port)

## Firmware Update Protocol

### Update Path

```
PC/Laptop                VESC              Target Board
    │                      │                     │
    │   USB-UART 115200    │                     │
    │─────────────────────►│                     │
    │                      │  Enter passthrough  │
    │   "UPDATE BLE\n"     │  mode for target    │
    │─────────────────────►│─────────────────────│
    │                      │                     │
    │   NBU framed transfer of .sfw file        │
    │──────────────────────────────────────────►│  (per-frame request→ACK)
    │                      │                     │  Verify ECDSA
    │                      │                     │  Flash if valid
    │◄──────────────────────────────────────────│  ACK / NACK
    │                      │                     │
```

> The Ninebot bus is a **single half-duplex wire**, so the update transport is **NBU** — framed
> request→ACK turn-taking over the bus's own `5A A5` framing — not a free-running byte stream like
> XMODEM (whose echo and turnaround behave badly on one wire).

### Update Trigger Methods

| Method | Trigger | When |
|--------|---------|------|
| **Hardware** | Hold power button during power-on | Manual recovery |
| **Software** | Set update flag in config page, then reset | App-initiated update |
| **Fallback** | No valid app signature detected | Automatic recovery |

### NBU Update Protocol (framed half-duplex)

NBU reuses the firmware-verified Ninebot frame and a strict request→reply exchange — see
[`common/include/nbu.h`](common/include/nbu.h):

```
5A A5 | LEN | SRC | DST | CMD | ARG | payload[LEN] | CK_lo CK_hi
  LEN = payload byte count;  CK = (sum(LEN..payload)) ^ 0xFFFF, little-endian.
```

Commands (host `0x3F` → board, opcodes aligned with the stock IAP); the bootloader replies with
`CMD 0x06 (ACK)`, `ARG = status` (0 = OK), payload `[acked_cmd, info_lo, info_hi]`:

| CMD | Name | Payload | Bootloader reply |
|-----|------|---------|------------------|
| 0x07 | BEGIN | u32 total .sfw size (LE) | ACK |
| 0x08 | DATA | u16 seq (LE) + data bytes (≤128) | ACK(seq) on accept; NACK(expected) if out of order; duplicate → re-ACK |
| 0x09 | END | (optional) u32 crc32 | ACK; receiver then validates + boots |
| 0x0A | RESET | — | ACK; reboot into app |

```
Sender (PC)                       Receiver (Bootloader)
     │── BEGIN  (size) ───────────────►│
     │◄──────── ACK ───────────────────│
     │── DATA   seq=0 [128 data] ─────►│
     │◄──────── ACK(0) ────────────────│  in order → write to flash
     │── DATA   seq=1 [128 data] ─────►│
     │◄──────── ACK(1) ────────────────│
     │        ... per-block ...         │  bad-checksum frame → dropped → host resends
     │── END ─────────────────────────►│
     │◄──────── ACK ───────────────────│  Transfer complete
     │                                  │  → Verify ECDSA sig
     │                                  │  → Flash valid / stay in bootloader
```

Per-block sequence numbers make the transfer robust on the shared wire: a duplicate (host missed the
ACK and resent) is written once and re-ACKed, and an out-of-order frame is NACKed with the sequence the
receiver expects so the host can resync.

### Signed Firmware Image Format (.sfw)

```
Offset  Size   Field                Description
──────  ─────  ───────────────────  ────────────────────────────────────
0x000   4      magic                0x53465730 ("SFW0")
0x004   4      header_version       Header format version (1)
0x008   4      fw_version           Firmware version (e.g., 0x00010107)
0x00C   1      target_id            Target: 0x01=BLE_STM32, 0x02=BMS, 0x03=NRF51
0x00D   1      crypto_type          0x01 = ECDSA-P256-SHA256
0x00E   2      flags                Bit 0: force update, Bit 1: preserve config
0x010   4      fw_size              Size of firmware binary (bytes)
0x014   4      fw_crc32             CRC-32 of firmware binary (quick check)
0x018   4      header_crc32         CRC-32 of header bytes 0x00–0x17
0x01C   4      reserved             0x00000000
0x020   32     fw_sha256            SHA-256 hash of firmware binary
0x040   64     ecdsa_signature      ECDSA-P256 signature (r[32] || s[32])
0x080   128    padding              Reserved (0xFF), total header = 256 bytes
────────────────────────────────────────────────────────────────────────
0x100   N      firmware_binary      Raw application firmware (.bin)
```

Total `.sfw` file size = 256 (header) + N (firmware binary)

## Bootloader Modules

### Shared Code (all bootloaders)

| Module | Size (est.) | Description |
|--------|-------------|-------------|
| `ecdsa_verify.c` | ~5 KB | ECDSA-secp256r1 signature verification (micro-ecc) |
| `sha256.c` | ~2 KB | SHA-256 hash computation |
| `nbu.c` | ~1 KB | NBU framed half-duplex update receiver (5A A5 frames, per-block ACK) |
| `fw_header.c` | ~0.5 KB | .sfw header parsing and validation |
| `crc32.c` | ~0.3 KB | CRC-32 for quick integrity checks |

### Platform-Specific Code

| Module | Platform | Description |
|--------|----------|-------------|
| `stm32_flash.c` | STM32F103 | Flash unlock, page erase, half-word write |
| `stm32_uart.c` | STM32F103 | USART1/2 init, blocking send/recv |
| `stm32_boot.c` | STM32F103 | Main bootloader logic, app jump |
| `nrf51_flash.c` | nRF51822 | Flash via MBR/SoftDevice calls |
| `nrf51_uart.c` | nRF51822 | UART0 init, blocking send/recv |
| `nrf51_boot.c` | nRF51822 | Bootloader logic with MBR integration |

## Build & Toolchain

| Component | Tool |
|-----------|------|
| Compiler | `arm-none-eabi-gcc` (Cortex-M0 for nRF51, Cortex-M3 for STM32) |
| Linker | GNU ld with custom linker scripts |
| Signing | `sign_firmware.py` (Python 3 + `cryptography` library) |
| Flashing | ST-Link (initial bootloader install) or SWD |
| NBU update | `tools/flasher/nbu_send.py` (a plain terminal can't drive the per-frame ACK handshake) |

```powershell
make -C bootloader/stm32 TARGET=ble                     # final build, links at 0x08000000
make -C bootloader/stm32 TARGET=ble BL_BASE=0x08004000  # test build, links into the app slot (+16 KB)
python tools/verify_reloc_build.py --board ble --base 0x08004000
```

**Test-before-overwrite build (`BL_BASE`):** overriding `BL_BASE` relinks the 16 KB bootloader into an
application slot so the *already-installed* bootloader launches it there — exercising a new build without
ever writing `0x08000000`. The link `ORIGIN`, runtime `SCB->VTOR`, app region (`base + 16 KB`) and flash
bounds all derive from the base; the flash driver's "never erase below `APP_START_ADDR`" guard means a
build relocated to `0x08004000` cannot erase the real bootloader at `0x08000000` (nor itself). See
`docs/guides/DEPLOYMENT.md` → *Relocated "test-before-overwrite" build*.

## Directory Structure

```
bootloader/
├── README.md                              ← This file
├── CMakeLists.txt                         ← Top-level CMake build
├── cmake/                                 ← Toolchain files
│   ├── arm-cortex-m3.cmake                ← STM32 cross-compilation
│   └── arm-cortex-m0.cmake                ← nRF51 cross-compilation
├── common/                                ← Shared code (all platforms)
│   ├── include/
│   │   ├── fw_header.h                    ← .sfw image format definitions
│   │   ├── ecdsa.h                        ← ECDSA verify API
│   │   ├── sha256.h                       ← SHA-256 API
│   │   ├── nbu.h                          ← NBU framed update receiver API
│   │   └── crc32.h                        ← CRC-32 utility
│   └── src/
│       ├── ecdsa.c                        ← secp256r1 verify implementation
│       ├── sha256.c                       ← SHA-256 implementation
│       ├── nbu.c                          ← NBU framed update receiver
│       ├── fw_header.c                    ← Header parse + validate
│       └── crc32.c                        ← CRC-32 computation
├── stm32/                                 ← BLE STM32 + BMS STM32
│   ├── include/
│   │   ├── stm32f1xx.h                    ← Minimal STM32F103 register defs
│   │   ├── stm32_flash.h                  ← Flash programming API
│   │   ├── stm32_uart.h                   ← UART API
│   │   └── bootloader_config.h            ← Board-specific config
│   ├── src/
│   │   ├── startup.s                      ← Cortex-M3 vector table + init
│   │   ├── bootloader_main.c              ← Entry point, main logic
│   │   ├── stm32_flash.c                  ← Flash erase/write
│   │   └── stm32_uart.c                   ← UART driver
│   ├── stm32f103c8_bootloader.ld          ← Linker script (16KB bootloader @ 0x08000000)
│   ├── stm32f103c8_bootloader.ld.in       ← Relocation template (cpp'd for BL_BASE builds)
│   └── Makefile
└── nrf51/                                 ← nRF51822 BLE co-processor
    ├── include/
    │   ├── nrf51.h                        ← Minimal nRF51822 register defs
    │   ├── nrf51_flash.h                  ← NVMC flash API
    │   ├── nrf51_uart.h                   ← UART API
    │   └── bootloader_config.h            ← nRF51 specific config
    ├── src/
    │   ├── startup.s                      ← Cortex-M0 vector table + init
    │   ├── bootloader_main.c              ← Entry point, main logic
    │   ├── nrf51_flash.c                  ← Flash programming
    │   └── nrf51_uart.c                   ← UART driver
    ├── nrf51822_bootloader.ld             ← Linker script (16KB at 0x3C000)
    └── Makefile

tools/ (repository root)
├── signing/
│   ├── generate_keys.py                   ← Generate ECDSA-P256 keypair
│   ├── sign_firmware.py                   ← Sign .bin → .sfw
│   ├── verify_firmware.py                 ← Verify .sfw signature
│   └── requirements.txt                   ← Python dependencies
└── flasher/
    ├── initial_flash.py                   ← Initial bootloader installation
    ├── update_bootloader.py               ← Bootloader self-update generator
    ├── nbu_send.py                        ← NBU framed half-duplex sender utility
    └── ninebot_flasher.py                 ← Stock IAP protocol flasher
```

## Safety Notes

- **BMS safety**: The BMS bootloader must maintain BQ76940 watchdog feed during updates. If flash programming takes too long, the AFE may trigger protection. The bootloader periodically reads BQ76940 status and feeds the watchdog via I2C during NBU receive.
- **Power loss**: If power is lost during flash write, the bootloader detects invalid signature on next boot and stays in update mode — no brick possible.
- **Key compromise**: If the private key is compromised, a new keypair must be generated and all bootloaders reflashed via SWD with the new public key. Consider key rotation support in future versions.
- **Rollback protection**: The `fw_version` field in the header can optionally be checked against a minimum version stored in the config page to prevent downgrade attacks.
