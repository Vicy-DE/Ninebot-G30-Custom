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
| **Interface** | UART (NBU framed half-duplex), Flash |
| **Board** | BLE STM32 |
| **Requirements** | 16 KB bootloader at 0x08000000. ECDSA-P256-SHA256 firmware signature verification. NBU receiver for firmware updates (framed half-duplex over the one-wire Ninebot bus; replaces the former XMODEM path). Update triggers: software flag, hardware button, missing/invalid app. Must fit in 16 KB with ECDSA + SHA-256 + NBU + flash driver. Public key embedded in read-only bootloader flash. |

## 6. Secure Bootloader — nRF51822

| Item | Detail |
|---|---|
| **Module / Component** | `bootloader/nrf51/` |
| **Interface** | UART (NBU framed half-duplex), Flash |
| **Board** | nRF51822 |
| **Requirements** | 16 KB bootloader at 0x0003C000 (end of flash). Same ECDSA signature verification as STM32. NBU receive via UART0 (relayed from STM32). Must coexist with Nordic MBR and SoftDevice. Flash programming via MBR API calls. UICR BOOTLOADERADDR set to 0x0003C000. |

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

## 15. OpenHaystack AirTag Emulation — Apple FindMy

| Item | Detail |
|---|---|
| **Module / Component** | `firmware/decompiled/nrf51822/` |
| **Interface** | BLE (non-connectable advertising), UART (mode command from STM32) |
| **Board** | nRF51822 |
| **Requirements** | The nRF51822 must be able to emulate an Apple AirTag using the OpenHaystack protocol, making the scooter trackable via Apple's Find My network without any Apple hardware. This feature must remain active while the rest of the scooter is in deep sleep (STM32 in STOP mode, VESC `app-disable-output`). |

**15.1 — BLE Advertisement Format**

The nRF51822 must broadcast non-connectable undirected BLE advertisements (`ADV_NONCONN_IND`) matching Apple FindMy format. Each advertisement payload must contain:
- Length `0x1E`, Type `0xFF` (manufacturer-specific data)
- Apple company ID `0x004C`
- FindMy type byte `0x12`, length `0x19`
- Status byte (key-roll indicator + reserved)
- 22-byte compressed ECDH P-224 public key (bytes 06–27 of the full 28-byte public key)
- Upper 2 bits of the public key as the first byte after the header
- Hint byte (last byte of the public key)

**15.2 — Rolling Key Schedule**

- A pre-generated set of at minimum 96 rolling public keys must be stored in nRF51 application flash (96 keys × 28 bytes = 2,688 bytes — well within the 80 KB application region).
- Keys are indexed by the current advertisement period. One key is active per 900-second window (15 minutes), matching the Apple FindMy rotation schedule.
- The nRF51 must maintain a persistent period counter in UICR or a dedicated flash page (survives power loss and reboot).
- Key generation (deriving the key set from a single private seed + OpenHaystack tooling) is a one-time offline step executed on PC; only public keys are stored on device.

**15.3 — Mode Switching**

The nRF51822 operates in two mutually exclusive modes:

| Mode | Active When | BLE Behaviour | UART |
|---|---|---|---|
| `NRF51_MODE_NORMAL` | Scooter running/awake | Ninebot BLE protocol + VESC App NUS | Active (receives protocol frames from STM32) |
| `NRF51_MODE_HAYSTACK` | Scooter sleeping | FindMy advertising only | Inactive (STM32 is in STOP mode) |

STM32 must send a single-byte UART command `0xAA` to nRF51 before entering STOP mode. nRF51 receives this command, terminates any active BLE connections, stops the VESC App NUS service, and starts non-connectable FindMy advertising. On STM32 wake-up, STM32 must send command `0xAB` to nRF51 to return to `NRF51_MODE_NORMAL`.

**15.4 — Advertising Interval in Sleep Mode**

In `NRF51_MODE_HAYSTACK` the advertising interval must be configurable between 2,000 ms (minimum per Apple FindMy spec) and 10,240 ms. Default: **5,000 ms**. Longer intervals reduce average current at the cost of network detection latency. At 5,000 ms interval with DCDC enabled, expected average current: ~4–8 µA.

**15.5 — SoftDevice Requirement**

SoftDevice S130 v2.0.1 is required (already planned in Req 3). S130 supports the broadcaster role (`BLE_GAP_ADV_TYPE_ADV_NONCONN_IND`) needed for FindMy.

**15.6 — Coexistence in Normal Mode**

In `NRF51_MODE_NORMAL`, FindMy advertising may optionally continue between VESC App advertising events, space permitting within SoftDevice scheduling windows. This is optional and must not degrade VESC App BLE throughput.

**15.7 — Power Saving Integration**

The FindMy feature is the primary reason to keep the nRF51822 powered during scooter sleep, rather than cutting its supply. The power budget during scooter sleep:

| Component | Sleep Current |
|---|---|
| STM32 (STOP mode) | ~10 µA |
| VESC (`app-disable-output`, 3.3V LDO standby) | ~1–5 mA |
| nRF51822 (FindMy advertising, 5 s interval, DCDC) | ~4–8 µA |
| Daly BMS (standby) | ~1 mA |
| **Total** | **~2–7 mA** |

The dominant sleep consumer is the VESC standby draw. Cutting VESC power during extended park would be the largest saving; the nRF51 cost is negligible.

**15.8 — Implementation Options Summary**

Three options were evaluated for how the nRF51 remains active during sleep:

| Option | Description | Hardware change | Complexity |
|---|---|---|---|
| **A (selected)** | STM32 sends UART mode-switch command (`0xAA`/`0xAB`) before/after sleep | None | Low — one UART byte |
| B | nRF51 auto-detects UART inactivity >5 s and self-transitions | None | Medium — timeout state machine |
| C | STM32 drives dedicated GPIO to nRF51 to signal sleep state | 1 wire | Low — GPIO + IRQ |

Option A is selected: explicit software commands are more deterministic than inactivity timeouts and require no extra hardware.

## 16. Dashboard Watchdog — IWDG 5000 ms (hard requirement)

| Item | Detail |
|---|---|
| **Module / Component** | `firmware/dashboard/`, `firmware/decompiled/common/include/watchdog_supervisor.h` |
| **Interface** | STM32F103 IWDG (independent watchdog, LSI-clocked); subsystem health |
| **Board** | BLE STM32 (dashboard) |
| **Requirements** | The dashboard firmware **must** run the STM32F103 IWDG with a **5000 ms timeout**. The IWDG resets the MCU if it is not reloaded in time, recovering from a hung firmware. The firmware reloads the IWDG **only while every *required* subsystem is fresh** — loop alive, clock up, ADC converting, and (while in RUN) the VESC link. If a required subsystem is **missing or stalls** beyond the staleness window (must be **< 5000 ms**, default 2000 ms), the firmware **withholds the reload** so the IWDG fires and the MCU resets+re-inits — i.e. "reset if something is missing." IWDG register values (PR=4, RLR=3124 at LSI 40 kHz → 5000 ms) are produced by `tools/analysis/iwdg_config.py` and `ninebot::iwdg_params()` (single source of truth). The IWDG is started **last** during init so a failed earlier init cannot be cut short mid-sequence. |
| **Verification** | Host tests `test_new_modules.cpp` (`Watchdog.IwdgParamsFor5000ms`, `Watchdog.FeedsOnlyWhenRequiredSubsystemsFresh`, `Watchdog.NonRequiredSubsystemDoesNotBlockFeed`); HW test `Target/dashboard_watchdog_test.py` (reset cadence ≈ 5 s when a dependency is missing). |

> LSI is ~40 kHz but spec'd 30–60 kHz, so the realised timeout spans ~3.3–6.7 s around the 5 s nominal — acceptable for a recovery watchdog.

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
| 15 | OpenHaystack AirTag Emulation | 3, 6 |
| 16 | Dashboard Watchdog (IWDG 5000 ms) | 2 |
