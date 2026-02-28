# nRF51822 BLE Co-Processor — Firmware Analysis & Reprogramming Guide

## Executive Summary

**Can the nRF51822 be reprogrammed with internal tools?**

**YES** — but through a **different mechanism** than the STM32 boards. The nRF51822 uses **Xiaomi MiIO BLE OTA** for over-the-air firmware updates, NOT the Ninebot serial IAP protocol used for ESC/BMS/STM32 boards.

| Update Path | Protocol | Internal? | Tools Required |
|---|---|---|---|
| **MiIO BLE OTA** (Primary) | Xiaomi MiIO over BLE L2CAP | ✅ Yes | Phone app (Ninebot/ScooterHacking) |
| **Nordic DFU** (if bootloader present) | Nordic DFU over BLE | ✅ Yes | nRF Connect / DFU app |
| **SWD Direct** (External) | ARM Serial Wire Debug | ❌ External | J-Link / ST-Link + wires |
| **Ninebot IAP Relay** (Indirect) | 5A A5 protocol, relayed | ⚠️ Partial | Serial adapter / app |

---

## Critical Discovery: BLE .bin Files ARE nRF51822 Firmware

The distributed BLE firmware files (`BLE_1.1.0.bin`, `BLE_1.1.7.bin`) are **NOT STM32 firmware**. They are **nRF51822 application images** designed to be loaded at `0x00018000` (after the Nordic SoftDevice BLE stack).

### Evidence

| Property | BLE_1.1.0 | BLE_1.1.7 | Interpretation |
|---|---|---|---|
| **Size** | 33,612 bytes | 34,252 bytes | Fits nRF51822 app region |
| **Initial SP** | `0x20003D10` | `0x20003DE8` | Near top of 16 KB SRAM |
| **Reset Handler** | `0x00018155` | `0x00018155` | nRF51822 post-SoftDevice address |
| **MemManage** | `0x00000000` | `0x00000000` | **Not present** → Cortex-M0 |
| **BusFault** | `0x00000000` | `0x00000000` | **Not present** → Cortex-M0 |
| **UsageFault** | `0x00000000` | `0x00000000` | **Not present** → Cortex-M0 |
| **Architecture** | ARM Cortex-M0 | ARM Cortex-M0 | nRF51822 (NOT STM32 Cortex-M3) |

The STM32F103C8T6 on the BLE dashboard has its own **separate, factory-programmed firmware** that is not included in the distributed `.bin` files.

### Corrected Memory Map

```
 nRF51822 Flash (256 KB)
┌─────────────────────────────────────────────────────┐
│ 0x00000000  ┌────────────────────────────────┐      │
│             │    Nordic SoftDevice S110 v8.0  │      │
│             │    BLE protocol stack           │      │
│             │    96 KB                        │      │
│ 0x00018000  ├────────────────────────────────┤      │
│             │                                │      │
│             │  ██ APPLICATION FIRMWARE ██     │      │  ← BLE_1.1.x.bin
│             │                                │      │     loads HERE
│             │    BLE_1.1.0: 33,612 bytes     │      │
│             │    BLE_1.1.7: 34,252 bytes     │      │
│             │                                │      │
│             │    Implements:                  │      │
│             │    - Ninebot protocol relay     │      │
│             │    - Xiaomi MiIO BLE auth       │      │
│             │    - BLE GATT/L2CAP services    │      │
│             │    - OTA flash update handler   │      │
│             │                                │      │
│ ~0x000205CC ├────────────────────────────────┤      │
│             │    FREE SPACE (~110 KB)         │      │
│ 0x0003C000  ├────────────────────────────────┤      │
│             │    Nordic DFU Bootloader        │      │
│             │    (if present, ~15 KB)         │      │
│ 0x0003FC00  ├────────────────────────────────┤      │
│             │    Bootloader Settings (1 KB)   │      │
│ 0x00040000  └────────────────────────────────┘      │
│                                                     │
│ 0x10001000  ┌────────────────────────────────┐      │
│             │    UICR (4 KB)                 │      │
│             │    BOOTLOADERADDR at 0x10001014│      │
│ 0x10001FFF  └────────────────────────────────┘      │
│                                                     │
│ 0x20000000  ┌────────────────────────────────┐      │
│             │    SRAM (16 KB QFAA)           │      │
│             │    SP starts at 0x20003DE8     │      │
│ 0x20003FFF  └────────────────────────────────┘      │
└─────────────────────────────────────────────────────┘
```

---

## Firmware Architecture

### Vector Table (Cortex-M0)

The nRF51822 application uses the following interrupt handlers:

| IRQ | Peripheral | Handler Address | Purpose |
|---|---|---|---|
| — | Reset | `0x00018155` | Application entry point |
| — | NMI | `0x0001816F` | Infinite loop (fault) |
| — | HardFault | `0x00018171` | Infinite loop (fault) |
| 2 | **UART0** | `0x00019E71` | **Protocol data from STM32** |
| 6 | **GPIOTE** | `0x00018CE9` | GPIO events (button?) |
| 9 | **TIMER1** | `0x00019D1D` | Timer events |
| 11 | **RTC0** | via default | Real-time counter |
| 16 | **WDT** | `0x0001A1B5` | Watchdog timeout |
| 17 | **RTC1** | `0x00019C3D` | Second RTC (BLE timing) |
| 20 | **SWI0** | `0x00019C5D` | Software interrupt 0 |
| 22 | **SWI2** | `0x00019CB1` | **SoftDevice system events (flash callbacks)** |

Most other IRQ vectors point to `0x00018179` (default handler — infinite loop catch).

### Function Statistics (BLE_1.1.7)

| Metric | Count |
|---|---|
| Functions (PUSH {lr} prologue) | 206 |
| Unique BL/BLX call targets | 232 |
| Total identified functions | 282 |
| SoftDevice SVC calls | 58 |
| L2CAP SVC calls | 14 |
| BLE GAP SVC calls | 18 |
| BLE GATTS SVC calls | varies |

### Most-Called Functions

| Address | Calls | Likely Purpose |
|---|---|---|
| `0x000181E0` | 77 | Debug/logging output function |
| `0x0001A1FC` | 35 | Timer/delay utility |
| `0x00018212` | 26 | Memory utility (memcpy/memset) |
| `0x0001818C` | 22 | Initialization helper |
| `0x0001F26C` | 20 | BLE service handler |
| `0x0001E8B4` | 16 | MiIO protocol handler |

---

## Ninebot Protocol Implementation

The nRF51822 firmware **implements Ninebot protocol parsing** (5A A5 header format). This is used for the UART bridge between the phone app and the STM32.

### Protocol Constants Found

| Address | Instruction | Purpose |
|---|---|---|
| `0x000184CC` | `MOVS R0, #0x5A` | Construct packet header byte 1 |
| `0x000184D0` | `MOVS R0, #0xA5` | Construct packet header byte 2 |
| `0x0001854E` | `CMP R0, #0x5A` | Parse/validate header byte 1 |
| `0x00018554` | `CMP R0, #0xA5` | Parse/validate header byte 2 |
| `0x0001A030` | `CMP R0, #0x5A` | Second protocol parser instance |
| `0x0001A034` | `CMP R0, #0xA5` | (UART receive path) |
| `0x00018CA0` | `MOVS R7, #0x5A` | Third protocol constructor |
| `0x00018CA6` | `MOVS R7, #0xA5` | (packet building for responses) |

### Protocol Data Flow

```
Phone App                    nRF51822                     STM32 (BLE board)
    │                            │                              │
    │──── BLE Write ────────────►│                              │
    │  (Ninebot 5A A5 packet)    │                              │
    │                            │── CMP #0x5A, CMP #0xA5 ────►│ UART0 TX
    │                            │  (parse + relay via UART)    │
    │                            │                              │──► To ESC/BMS
    │                            │                              │
    │                            │◄── UART0 RX ────────────────│
    │                            │  (ISR at 0x00019E71)        │
    │◄──── BLE Notify ──────────│                              │
    │  (response packet)         │                              │
```

The nRF51822 acts as a **transparent protocol bridge** — it parses Ninebot protocol packets received over BLE, and forwards them to the STM32 via UART0. Responses flow back the same way.

---

## Xiaomi MiIO BLE Protocol

The nRF51822 firmware implements **Xiaomi's MiIO BLE authentication and OTA framework**. This is the mechanism used for firmware updates.

### MiIO Authentication Flow

Reconstructed from firmware strings:

```
Step 1: Service Init
    → "[E]: Mi Service Init fail, error code=%d"
    → Initializes MiIO BLE GATT service

Step 2: Connection
    → "On Connected"
    → BLE connection established

Step 3: Authentication
    → "On Auth Written"
    → "decrypted auth, %x"
    → Phone writes auth challenge, nRF decrypts

Step 4: Token Exchange
    → "On Token Written"
    → "login cfm handler, token: %02x %02x"
    → "decrypted %x"
    → "login cfm succ" / "login cfm fail"

Step 5: Cloud Bind
    → "cloud bind succ" / "cloud bind fail"
    → Device registered with Xiaomi cloud

Step 6: App Bond
    → "app bond succ" / "app bond fail"
    → "mi_service, bond succ"
    → BLE bond established

Step 7: Serial Number Exchange
    → "SN Arrived"
    → "sn: %02x"
    → "[E]: SN timeout, clear token"
    → "[W]: Weak SN timeout."
    → "mi_service, waiting SN"

Step 8: Registration Complete
    → "Register succ, new token, encrypt sn, beaconkey."
    → "new token: %02x %02x"
    → Device fully registered

Step 9: Flash Registration
    → "[D]: miio ble flash register succ"
    → "[E]: miio ble flash register fail"
    → OTA flash capability activated
```

### MiIO Flash Operations

Once authenticated and flash-registered, the MiIO SDK performs firmware updates:

```
Step 1: Flash Write Function Check
    → "[W]: flash write func not setting"
    → Verifies flash write callback is registered

Step 2: Flash Write
    → "flash write succ"
    → "[E]: flash write fail"
    → Pages written via SoftDevice (sd_flash_write)

Step 3: Flash Read Verification
    → "[E]: flash read error"
    → Written data verified

Step 4: PSM Callback
    → "psm callback"
    → Persistent Storage Manager notification

Step 5: Operation Complete
    → "[E]: flash operation error, opcode:%d"
    → "[D]: update succ. len = %d"
    → "[D]: load succ, len = %d"
    → "[D]: store succ. len = %d"
    → "[D]: clear succ. len = %d"
```

### SoftDevice API Usage

The firmware uses the Nordic SoftDevice extensively:

| SVC (hex) | Function | Count | Purpose |
|---|---|---|---|
| 0x60 | `sd_ble_enable` | 1 | Enable BLE stack |
| 0x61 | `sd_ble_evt_get` | 1 | Get BLE events |
| 0x63 | `sd_ble_uuid_vs_add` | 1 | Add vendor-specific UUID |
| 0x65 | `sd_ble_opt_set` | 2 | Set BLE options |
| 0x72 | `sd_ble_gap_adv_data_set` | 1 | Set advertising data |
| 0x73 | `sd_ble_gap_adv_start` | 1 | Start advertising |
| 0x76 | `sd_ble_gap_disconnect` | 3 | Disconnect |
| 0x77 | `sd_ble_gap_tx_power_set` | 1 | Set TX power |
| 0x7A | `sd_ble_gap_ppcp_set` | 2 | Set connection params |
| 0x90–0x9A | `sd_ble_gatts_*` | many | GATT server operations |
| 0xA0–0xA9 | `sd_ble_l2cap_*` | 14 | **L2CAP channels (MiIO OTA data)** |
| 0x28–0x29 | `sd_ppi_*` | 10 | PPI channels |
| 0x36 | `sd_power_gpregret_get` | 2 | **Read GPREGRET (DFU check)** |
| 0x37 | `sd_power_dcdc_mode_set` | 2 | DC-DC converter mode |

**Key observation**: The firmware reads GPREGRET (`sd_power_gpregret_get`) but **never writes** it (`sd_power_gpregret_set` = SVC 0x34 is **absent**). This means the application does not programmatically trigger DFU mode — DFU entry (if a bootloader exists) must be triggered externally or through the SoftDevice.

**No direct `sd_flash_write` (SVC 0x3D) or `sd_flash_page_erase` (SVC 0x3C) calls** were found. Flash operations are performed through the MiIO SDK's abstraction layer, which likely uses an indirect function pointer registered during initialization.

---

## Update Paths — Detailed Analysis

### Path 1: MiIO BLE OTA (PRIMARY — Internal)

This is the **primary and confirmed** update path for the nRF51822 firmware.

**How it works:**

1. Phone app (Ninebot/Segway/ScooterHacking) connects via BLE
2. MiIO authentication sequence (token, auth, cloud bind)
3. MiIO registers flash write capability on the nRF51822
4. Firmware data transferred via BLE L2CAP channels (14 L2CAP SVCs)
5. nRF51822's MiIO SDK writes flash pages through SoftDevice
6. SWI2 interrupt handler processes flash completion callbacks
7. PSM (Persistent Storage Manager) confirms write success
8. On completion, nRF51822 resets into updated firmware

**Requirements:**
- BLE connection to the scooter
- MiIO authentication credentials (auto-handled by official/community apps)
- Valid nRF51822 application image (33–35 KB)
- Image must be linked for base address `0x00018000`

**Security:**
- MiIO token authentication required (prevents unauthorized updates)
- MD5 hash used for MiIO auth (MD5 init constant `0x67452301` present)
- No code signing — any valid ARM Cortex-M0 binary can be flashed
- TEA/XiaoTEA encryption may be used for transport (key: "Ninebot Scooter")

**This is what happens when you "flash BLE firmware" using the Ninebot app or ScooterHacking Utility.**

### Path 2: Nordic DFU Bootloader (POSSIBLE — Internal)

The nRF51822 memory map reserves `0x0003C000–0x0003FFFF` for a Nordic DFU bootloader. The firmware reads GPREGRET on boot (checking for DFU trigger value `0xB1`), which is standard Nordic DFU behavior.

**Evidence for DFU bootloader:**
- GPREGRET read (`sd_power_gpregret_get`) at 2 locations
- Standard Nordic memory layout with bootloader reservation
- UICR.CLENR0 referenced 7 times (code region configuration)
- Free flash space (~110 KB between app end and bootloader)

**Evidence against DFU bootloader (or unused):**
- No `sd_power_gpregret_set` calls (app never triggers DFU)
- No direct reference to `0x0003C000` in literal pool
- No `BOOTLOADERADDR` UICR reference found
- The MiIO OTA path handles updates without needing DFU

**If a DFU bootloader IS present:**
1. Use nRF Connect app (iOS/Android)
2. Put nRF51822 in DFU mode (may require SWD or battery disconnect)
3. Select DFU target (nRF51822)  
4. Upload application `.bin` file
5. DFU bootloader writes to `0x00018000+`

**Status: Unconfirmed — would need to dump the full nRF51822 flash via SWD to verify bootloader presence at `0x0003C000`.**

### Path 3: Ninebot Protocol IAP Relay (INDIRECT — Internal)

When the Ninebot IAP protocol targets `DST=0x21` (BLE board), the data flows:

```
Phone → BLE → nRF51822 → UART → STM32 (BLE board) → STM32 bootloader
```

This updates the **STM32** on the BLE dashboard board, NOT the nRF51822 directly. The STM32's bootloader writes the received data to its own flash at `0x08001000+`.

**However**, there may be a secondary mechanism where:
1. The STM32 receives an nRF51822 firmware image via IAP
2. The STM32 stores it in its own flash as a staging area
3. The STM32 programs the nRF51822 via SWD bit-banging or UART

**Evidence:** The decompiled STM32 BLE firmware (`ble_main.cpp`) shows **no SWD programming or nRF51822 flash writing code**. The STM32 only handles:
- Throttle/brake ADC reading
- Dashboard LED control
- Ninebot protocol relay between USART1 (nRF) and USART2 (ESC)

**Conclusion: The STM32 does NOT program the nRF51822. They have independent update paths.**

### Path 4: SWD Direct Programming (EXTERNAL)

The nRF51822 has dedicated SWD pins (separate from the STM32's SWD):

| Pin | Function |
|---|---|
| SWDIO | Serial Wire Debug data |
| SWDCLK | Serial Wire Debug clock |

**Using a J-Link or compatible SWD debugger:**

```bash
# With nrfjprog (Nordic command-line tool):
nrfjprog --program BLE_1.1.7.bin --sectorerase --verify

# With OpenOCD:
openocd -f interface/jlink.cfg -f target/nrf51.cfg \
        -c "program BLE_1.1.7.bin 0x00018000 verify reset"

# With pyOCD:
pyocd flash -t nrf51822 -a 0x18000 BLE_1.1.7.bin
```

**This is the only recovery method** if the nRF51822 firmware is corrupted and BLE communication is lost.

**⚠️ Important:** Do NOT erase the SoftDevice (0x00000000–0x00017FFF) or bootloader (0x0003C000+) regions unless you have replacements. Erasing the SoftDevice will render BLE non-functional.

---

## BLE Device Identity

The firmware contains these BLE identity strings:

| Address | String | Purpose |
|---|---|---|
| `0x0001B300` | `Scooter_G30_SAT` | Model identifier (G30 Max) |
| `0x0001C3AC` | `NBScooter0001` | BLE device name / serial template |
| `0x00020454` | `N3M-Ninebot-Mini0001` | Product line identifier |

BLE Service UUIDs (Nordic UART Service):
- Primary: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- TX (Phone→Scooter): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- RX (Scooter→Phone): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

---

## UART Communication (nRF51822 ↔ STM32)

The nRF51822 communicates with the STM32 via its single UART peripheral:

| Register | Address | Usage |
|---|---|---|
| UART0.ENABLE | `0x40002500` | Enable/disable UART |
| UART0.TASKS_STARTRX | `0x40002000` | Start receiving |
| UART0.EVENTS_RXDRDY | `0x40002108` | Byte received event |
| UART0.EVENTS_TXDRDY | `0x40002110` | Byte transmitted event |
| UART0.INTEN | `0x40002300` | Interrupt enable mask |

**Pin assignments (from Nordic reference design):**
- P0.08 → UART TX → STM32 PA10 (USART1_RX)
- P0.09 → UART RX → STM32 PA9 (USART1_TX)

**Baud rate:** 115200 (configured via UART0.BAUDRATE register, value `0x01D7E000`)

**IRQ handler:** `0x00019E71` — processes received bytes from STM32, likely feeds them into the Ninebot protocol parser.

---

## Comparison: nRF51822 vs STM32 Update Methods

| Feature | STM32 (ESC/BMS) | nRF51822 (BLE) |
|---|---|---|
| **Primary update** | Ninebot IAP (serial) | MiIO BLE OTA |
| **Protocol** | 5A A5 UART packets | BLE L2CAP + MiIO auth |
| **Bootloader** | 4 KB at 0x08000000 | Nordic DFU at 0x0003C000 (unconfirmed) |
| **Initiator** | Host writes reg 0x07 | MiIO flash registration |
| **Data transport** | UART 64-byte blocks | BLE L2CAP channels |
| **Verification** | 16-bit checksum | PSM callback + flash read |
| **Code signing** | None | None |
| **Encryption** | XiaoTEA (transport only) | MiIO token auth + possible TEA |
| **Recovery** | SWD via ST-Link | SWD via J-Link |
| **Firmware type** | Cortex-M3 (Thumb/T2) | Cortex-M0 (Thumb only) |
| **Base address** | 0x08001000 | 0x00018000 |
| **Binary size** | 20–33 KB | 33–35 KB |

---

## Custom Firmware Development Notes

### Building for nRF51822

```makefile
# Toolchain: ARM GCC (arm-none-eabi-gcc)
# Target: Cortex-M0 (Thumb only, no Thumb-2 except BL)
CFLAGS = -mcpu=cortex-m0 -mthumb -mfloat-abi=soft
CFLAGS += -DNRF51 -DNRF51822_QFAA_CA
CFLAGS += -DS110   # SoftDevice S110
CFLAGS += -DSOFTDEVICE_PRESENT
CFLAGS += -DBLE_STACK_SUPPORT_REQD

# Linker script must place code at 0x00018000 (after SoftDevice)
LDFLAGS = -T nrf51_s110.ld
# ROM start: 0x00018000, ROM size: 0x00024000 (144 KB max)
# RAM start: 0x20002000 (after SoftDevice RAM), RAM size: 0x00002000
```

### Linker Script Key Settings

```ld
MEMORY
{
  FLASH (rx) : ORIGIN = 0x00018000, LENGTH = 0x24000  /* 144 KB app region */
  RAM (rwx)  : ORIGIN = 0x20002000, LENGTH = 0x2000   /* ~8 KB after SD RAM */
}
```

### SoftDevice Integration

Custom firmware MUST:
1. Use SVC calls for all BLE operations (never access radio registers directly)
2. Forward SoftDevice interrupts via the SoftDevice vector table
3. Not write to flash addresses below `0x00018000` (SoftDevice region)
4. Use `sd_flash_write` / `sd_flash_page_erase` for any flash operations
5. Handle SWI2 events for flash operation completion callbacks
6. Initialize the SoftDevice before using any BLE features

### Required Components for Custom Firmware

1. **Nordic SDK** (v10.0 or v11.0 for S110 v8.0 compatibility)
2. **SoftDevice S110 v8.0** binary (must match what's flashed on the nRF51822)
3. **ARM GCC** compiler (arm-none-eabi-gcc 4.9+ or 5.x)
4. **Ninebot protocol parser** (5A A5 header, checksum, address routing)
5. **MiIO BLE library** (for app compatibility) OR custom BLE services
6. **UART driver** (for STM32 communication at 115200 baud)

---

## Analysis Files Generated

| File | Description |
|---|---|
| `analyze_nrf51822.py` | nRF51822 Cortex-M0 firmware analyzer & disassembler |
| `nrf51822_analysis_BLE_1.1.0.txt` | Full analysis of BLE_1.1.0.bin firmware |
| `nrf51822_analysis_BLE_1.1.7.txt` | Full analysis of BLE_1.1.7.bin firmware |
| `docs/nrf51822-reprogramming.md` | This document |

---

## Safety Warnings

1. **Do NOT erase the SoftDevice** — Without S110, BLE will not function and the scooter cannot be controlled via the app. Recovery requires SWD access.
2. **Do NOT modify the bootloader** (if present) — Losing the bootloader removes the DFU recovery path.
3. **Keep stock firmware backups** — Always dump the full nRF51822 flash (256 KB) via SWD before any modifications.
4. **UART bridge is critical** — If custom nRF51822 firmware doesn't relay Ninebot protocol correctly, the phone app loses contact with the ESC and BMS.
5. **MiIO auth is required** — The Ninebot/Segway app requires MiIO authentication. Custom firmware that removes MiIO will not work with the official app (use ScooterHacking Utility instead).
6. **Test on bench first** — A broken nRF51822 firmware means no throttle/brake control from the phone app. Always test with the scooter on a stand.

---

## References

- [BLE-Dashboard/README.md](../BLE-Dashboard/README.md) — BLE dashboard hardware docs
- [BLE-Dashboard/PINOUT.md](../BLE-Dashboard/PINOUT.md) — Pin assignments for both MCUs
- [docs/iap-update-protocol.md](iap-update-protocol.md) — STM32 IAP protocol (for ESC/BMS)
- [docs/protocol.md](protocol.md) — Ninebot protocol format reference
- [Nordic nRF51822 Product Spec](https://infocenter.nordicsemi.com/pdf/nRF51822_PS_v3.3.pdf)
- [Nordic S110 SoftDevice Spec](https://infocenter.nordicsemi.com/pdf/S110_SDS_v2.0.pdf)
- [Xiaomi MiIO BLE Protocol](https://github.com/nickel-lang/miio) — Community documentation
