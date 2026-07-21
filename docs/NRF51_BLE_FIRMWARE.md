# nRF51822 BLE Firmware — Design (app-compatible)

**Date:** 2026-06-03
**Target:** nRF51822-QFAA (Cortex-M0, 256 KB flash, 16 KB SRAM), **SoftDevice S130 v2.0.1**
(S110 for legacy-only). App @ `0x0001B000` (post-S130) / `0x00018000` (post-S110).
**Role:** BLE↔UART bridge that is **compatible with the original phone app**, also exposes the **VESC
App** over BLE, and emulates an **Apple FindMy tag** in sleep. Satisfies Req 3 and Req 15; grounded in
`docs/BLE_PROTOCOL_VERIFIED.md`, `REGISTER_MAP.md`, `PROTOCOL_V2.md`.

---

## 1. What "compatible with the original app" requires (verified)

From `docs/BLE_PROTOCOL_VERIFIED.md` (firmware RE + APK disassembly):
- **Transport:** Nordic UART Service (NUS, base `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`) — the app
  writes/notifies framed bytes over RX/TX characteristics.
- **Framing:** Ninebot `5A A5 | LEN(payload) | SRC | DST | CMD | ARG | payload | ~sum` (verified core,
  `ninebot_protocol_verified.hpp`).
- **Registers:** the app reads/writes the ARG-indexed register file (`REGISTER_MAP.md`).
- **Pairing/crypto** — matches the **app generation**:
  - *Legacy* (stock `BLE_1.1.x`, Mi Home/early Ninebot): **Xiaomi MiIO** (token/beaconkey/login-confirm).
  - *Current* (Segway-Ninebot `com.ninebot.segway`): **nbcrypto** = AES-ECB session key derived
    (MD5 + key-rule) from an auth param (`cn.ninebot.nbcrypto.NbEncryption`).
- Advertising name/format the app scans for (`"NBScooter…"`, G30 model string).

## 2. GATT layout

| Service | UUID | Purpose | Client |
|---------|------|---------|--------|
| **App NUS** | `6E400001-…CA9E` (RX `…0002`, TX `…0003`) | stock Ninebot framed protocol (+ MiIO/nbcrypto) | original phone app |
| **VESC NUS** | second NUS instance (distinct 128-bit base) | raw VESC packets (TUNNEL) | VESC Tool mobile (Req 3) |
| **(optional) Mi service** | `FE95` | MiIO advertising/bind if targeting legacy app | Mi Home |
| **DFU** | Nordic Secure DFU / our XMODEM relay | firmware update (Req 6) | bootloader |

The two NUS instances let the **stock app and VESC Tool coexist**. The improved **NB+** protocol
(`PROTOCOL_V2.md`) rides the VESC/companion path as `TYPE=TUNNEL`; the **stock app path stays stock**.

## 3. Module architecture (`firmware/decompiled/nrf51822/`)

```
            ┌──────────────────────── nRF51 app ────────────────────────┐
 phone app ─┤ App NUS  ─► ninebot_frame_rx ─► [auth/crypto] ─► uart_tx ──┼─► STM32
            │ App NUS  ◄─ ninebot_frame_tx ◄─ [auth/crypto] ◄─ uart_rx ◄─┤
 VESC Tool ─┤ VESC NUS ◄──────────► vesc_tunnel ◄──────────► uart (VESC) │
            │ mode_ctrl: UART 0xAA/0xAB ──► normal / haystack            │
            │ haystack_adv: FindMy rolling-key broadcaster (Req 15)      │
            └────────────────────────────────────────────────────────────┘
```

| File | Contents |
|------|----------|
| `nrf51_main.cpp` | SoftDevice init, GAP/advertising, event loop, mode state machine |
| `nb_ble_bridge.{h,cpp}` *(new)* | App NUS ⇄ UART; Ninebot framing (reuses verified core) |
| `nb_auth.{h,cpp}` *(new)* | pairing/crypto: `miio` (legacy) **or** `nbcrypto` (AES key-rule) backend |
| `vesc_nus.{h,cpp}` *(new)* | second NUS; raw VESC packet tunnel (Req 3) |
| `haystack.{h,cpp}` *(new)* | FindMy non-connectable advertiser + rolling keys (Req 15) |
| `mode_ctrl.{h,cpp}` *(new)* | UART `0xAA`/`0xAB` → NORMAL/HAYSTACK (Req 15.3) |

## 4. Mode state machine (Req 15)

```
 NORMAL  ──UART 0xAA (STM32 sleeping)──►  HAYSTACK
   │  App NUS + VESC NUS active                 │  FindMy ADV_NONCONN_IND only,
   │  UART bridge to STM32 active               │  key rolls every 900 s, 5 s interval (~4–8 µA)
   ◄──────────── UART 0xAB (STM32 wake) ─────────┘
```
- HAYSTACK: stop connectable advertising + NUS, terminate links, start `BLE_GAP_ADV_TYPE_ADV_NONCONN_IND`
  with the current rolling public key (96 keys in flash, period counter persisted) — exactly Req 15.1–15.7.
- S130 required for the broadcaster role (Req 15.5).

## 5. Auth/crypto backends (`nb_auth`)

Selectable at build time (`NB_AUTH=MIIO|NBCRYPTO|NONE`):
- **MIIO** — reproduce the legacy handshake (Mi service `FE95`, token/beaconkey, login-confirm); state
  machine mirrors the strings in `DECOMPILATION.md` §4f. Keys persisted in flash (PSM-equivalent).
- **NBCRYPTO** — AES-ECB payload encryption with a session key from `Key_rule_analysis(authParam)` +
  MD5, per the app's `NbEncryption` (see `BLE_PROTOCOL_VERIFIED.md` §3). RC4 in the handshake.
- **NONE** — plaintext framed protocol (custom companion app / bench testing).

> The proprietary key-derivation itself is not re-published here; the backend is a clean interface
> (`auth_init / auth_on_write / auth_wrap_tx / auth_unwrap_rx`) so the correct rule can be supplied
> (cloned from the bonded device's keys, or the documented MiIO algorithm). Transport/framing/registers
> are fully specified and implementable today.

## 6. Memory map (S130)

| Region | Range | Size |
|--------|-------|------|
| MBR | `0x00000000–0x00000FFF` | 4 KB |
| SoftDevice S130 | `0x00001000–0x0001AFFF` | ~108 KB |
| **App** | `0x0001B000–0x00039FFF` | ~124 KB (NUS×2, bridge, auth, 96 Haystack keys ≈ 2.7 KB) |
| Secure bootloader | `0x0003C000–0x0003FBFF` | 16 KB (Req 6) |
| Bootloader settings / UICR | `0x0003FC00+` / `0x10001014` (`BOOTLOADERADDR`) | — |

## 7. Build & test
- Toolchain: `arm-none-eabi-gcc` (Cortex-M0), Nordic nRF5 SDK + S130; link app above the SoftDevice.
- **Host-testable** parts (no SoftDevice): the Ninebot framing (already 45/45 in
  `test_decompiled_protocol.cpp`), the bridge framer, the `nb_auth` NONE path, and the Haystack
  advertisement payload builder — add to `firmware/decompiled/tests`.
- On-device: pair with the original app (verify it connects/reads telemetry), VESC Tool over the 2nd
  NUS, and FindMy detection in HAYSTACK mode.

## 8. Implementation task list
- [ ] S130 GAP + dual NUS services; advertising name/format the app expects.
- [ ] `nb_ble_bridge`: App NUS ⇄ UART using the verified Ninebot core.
- [ ] `nb_auth`: NONE first (bring-up), then MIIO or NBCRYPTO backend for the target app.
- [ ] `vesc_nus`: raw VESC tunnel (Req 3).
- [ ] `haystack`: rolling-key non-connectable advertiser + persisted period counter (Req 15).
- [ ] `mode_ctrl`: `0xAA`/`0xAB` UART handler (Req 15.3).
- [ ] Host tests for framer/auth-NONE/haystack-payload; on-device app-compat + VESC-Tool + FindMy.

See `firmware/decompiled/DECOMPILATION.md`, `docs/BLE_PROTOCOL_VERIFIED.md`, `docs/REGISTER_MAP.md`,
`docs/PROTOCOL_V2.md`, and `docs/DASHBOARD_FIRMWARE.md` (the STM32 side this bridges to).
