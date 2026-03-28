# Requirements — Ninebot G30 Max Custom Firmware

## 1. VESC Motor Integration — UART

| Item | Detail |
|---|---|
| **Module / Component** | `firmware/decompiled/ble/`, `lib/ninebot-protocol/` |
| **Interface** | UART (115200 8N1) |
| **Board** | BLE STM32 |
| **Requirements** | BLE STM32 must bridge between Ninebot protocol bus and VESC UART. Translate throttle/brake commands from Ninebot format to VESC commands. Forward telemetry (speed, current, temperature, duty cycle) from VESC back to Ninebot protocol registers. VESC UART uses its own packet format (start byte, length, CRC16). |

## 2. Stock Feature Set Preservation — BLE Dashboard

| Item | Detail |
|---|---|
| **Module / Component** | `firmware/decompiled/ble/` |
| **Interface** | UART, GPIO, ADC |
| **Board** | BLE STM32 |
| **Requirements** | Custom BLE firmware must provide all original dashboard features: throttle ADC reading, brake lever input, display driving (speed, battery %, mode), headlight control, tail light control, power button handling, error code display. Must respond to Ninebot protocol register reads (0x10-0x79) with correct data. |

## 3. VESC App BLE Service — nRF51822

| Item | Detail |
|---|---|
| **Module / Component** | `firmware/decompiled/nrf51822/` |
| **Interface** | BLE (GATT), UART |
| **Board** | nRF51822 |
| **Requirements** | nRF51822 must expose a BLE GATT service compatible with the VESC Tool mobile app. Forward VESC UART packets over BLE NUS (Nordic UART Service) or custom VESC BLE service. Must coexist with Ninebot BLE protocol for backward compatibility with phone apps. Use SoftDevice S110 or S130. |

## 4. Speed Cap Removal

| Item | Detail |
|---|---|
| **Module / Component** | `firmware/decompiled/ble/` |
| **Interface** | UART |
| **Board** | BLE STM32 |
| **Requirements** | Remove speed cap enforcement from BLE firmware. The BLE board must not limit speed commands sent to VESC. Speed register 0xB0 should report actual speed without artificial limits. VESC handles its own configurable speed limits via motor ERPM. |

## 5. Secure Bootloader — STM32

| Item | Detail |
|---|---|
| **Module / Component** | `bootloader/stm32/` |
| **Interface** | UART (XMODEM-CRC), Flash |
| **Board** | BLE STM32 |
| **Requirements** | 16 KB bootloader at 0x08000000. ECDSA-P256-SHA256 firmware signature verification. XMODEM-CRC receiver for firmware updates. Update triggers: software flag, hardware button, missing/invalid app. Must fit in 16 KB with ECDSA + SHA-256 + XMODEM + flash driver. Public key embedded in read-only bootloader flash. |

## 6. Secure Bootloader — nRF51822

| Item | Detail |
|---|---|
| **Module / Component** | `bootloader/nrf51/` |
| **Interface** | UART (XMODEM-CRC), Flash |
| **Board** | nRF51822 |
| **Requirements** | 16 KB bootloader at 0x0003C000 (end of flash). Same ECDSA signature verification as STM32. XMODEM receive via UART0 (relayed from STM32). Must coexist with Nordic MBR and SoftDevice. Flash programming via MBR API calls. UICR BOOTLOADERADDR set to 0x0003C000. |

## 7. Signed Firmware Image Format (.sfw)

| Item | Detail |
|---|---|
| **Module / Component** | `bootloader/common/`, `tools/signing/` |
| **Interface** | N/A |
| **Board** | All |
| **Requirements** | 256-byte header: magic "SFW0", header version, firmware version, target ID, crypto type, flags, firmware size, CRC-32, SHA-256 hash, ECDSA signature (r\|\|s 64 bytes). Total file = 256 header + N firmware bytes. Target IDs: 0x01=BLE_STM32, 0x02=BMS, 0x03=NRF51. |

## 8. Stock IAP Compatibility

| Item | Detail |
|---|---|
| **Module / Component** | `tools/flasher/` |
| **Interface** | UART (Ninebot protocol) |
| **Board** | All |
| **Requirements** | Flasher tools must be able to use the stock 4 KB bootloader's IAP protocol for Phase 1 deployment. Implement registers 0x07 (IAP start), 0x08 (data block), 0x09 (verify), 0x0A (reset). 64-byte blocks, 500ms post-erase delay, 16-bit checksum. |

## ~~9. BMS Custom Firmware — Battery Monitoring~~ *(removed 2026-03-29)*

~~BMS custom firmware is out of scope. Stock BMS firmware works correctly and custom firmware adds unnecessary risk to battery safety. The BLE firmware supports the stock BMS Ninebot protocol natively.~~

## 10. VESC UART Passthrough for Updates

| Item | Detail |
|---|---|
| **Module / Component** | `tools/flasher/` |
| **Interface** | UART (USB via VESC) |
| **Board** | VESC |
| **Requirements** | VESC must support UART passthrough mode for updating BLE and BMS boards. PC sends "UPDATE BLE\n" or "UPDATE BMS\n" to VESC USB; VESC transparently bridges USB↔UART to the target board. This enables firmware updates without opening the scooter. |

## 11. Debug UART Output

| Item | Detail |
|---|---|
| **Module / Component** | All firmware modules |
| **Interface** | UART |
| **Board** | All |
| **Requirements** | Debug builds must output human-readable log messages over UART prefixed with `[DBG]`. Debug output must not interfere with Ninebot protocol packets (5A A5 framed). Compile-time `DEBUG_ENABLED` flag to disable for release builds. Bootloader always outputs status messages prefixed with `[BOOT]`. |

## 12. Deployment Phase Support

| Item | Detail |
|---|---|
| **Module / Component** | `bootloader/`, `firmware/`, `tools/` |
| **Interface** | UART, Flash |
| **Board** | BLE STM32, nRF51822 |
| **Requirements** | Firmware and bootloader must support the 5-phase deployment strategy: Phase 0 (backup/prep), Phase 1 (app via stock IAP at 0x08001000), Phase 2 (bootloader-as-app at 0x08001000, app at 0x08005000), Phase 3 (final bootloader at 0x08000000, app at 0x08004000), Phase 4 (nRF51822). Each phase must be independently buildable with correct linker addresses. |

## 13. Daly BMS Compatibility — UART Protocol

| Item | Detail |
|---|---|
| **Module / Component** | `firmware/decompiled/ble/`, `lib/ninebot-protocol/` |
| **Interface** | UART (115200 or 9600, 8N1) |
| **Board** | BLE STM32 |
| **Requirements** | BLE firmware must optionally support Daly BMS as an alternative to stock Ninebot BMS. Daly BMS uses a proprietary UART protocol: header 0xA5, address byte (0x40 host→BMS, 0x01 BMS→host), command byte, fixed 8-byte data, checksum (byte sum & 0xFF). Must support commands: 0x90 (voltage/current/SOC), 0x91 (min/max cell voltage), 0x92 (temperature), 0x93 (MOSFET status), 0x94 (status info), 0x95 (cell voltages, multi-frame), 0x96 (cell temperatures), 0x97 (balance state), 0x98 (fault codes). Current offset is 30000 (0x7530). Reference library: maland16/daly-bms-uart. Detection must be automatic or configurable via config region. |

## 14. VESC Lisp Motor Control Script — Dashboard Integration

| Item | Detail |
|---|---|
| **Module / Component** | `vesc-lisp/` |
| **Interface** | UART (115200 8N1, half-duplex), Ninebot protocol |
| **Board** | VESC |
| **Requirements** | VESC Lisp script running on VESC hardware provides the bridge between the G30 dashboard and VESC motor control. Reads dashboard throttle (byte 5) and brake (byte 6) from Ninebot protocol frame 0x65 (sent by BLE STM32). Sends display updates via frame 0x64 (mode, battery, light, beep, speed, error fields). Ninebot protocol: 0x5AA5 header, CRC = XOR 0xFFFF of byte sum from offset 2. Supports eco/drive/sport speed modes, headlight control, lock function, and power button handling. Throttle stays at the dashboard — script reads ADC values relayed via protocol, not direct ADC. Reference implementations: CRZX1337/g30-vesc-dash, m365fw/vesc_m365_dash. |

---

## Traceability Matrix

| Req # | Feature | Depends On |
|---|---|---|
| 1 | VESC Motor Integration | 2, 4, 14 |
| 2 | Stock Feature Set Preservation | — |
| 3 | VESC App BLE Service | 1, 6 |
| 4 | Speed Cap Removal | 1, 14 |
| 5 | Secure Bootloader STM32 | 7 |
| 6 | Secure Bootloader nRF51 | 7, 5 |
| 7 | Signed Firmware Image Format | — |
| 8 | Stock IAP Compatibility | — |
| ~~9~~ | ~~BMS Custom Firmware~~ | ~~5~~ |
| 10 | VESC UART Passthrough | 1 |
| 11 | Debug UART Output | — |
| 12 | Deployment Phase Support | 5, 6, 7, 8 |
| 13 | Daly BMS Compatibility | 2 |
| 14 | VESC Lisp Motor Control | 1 |
