# BLE Protocol — Verified (firmware RE + official-app APK disassembly)

**Date:** 2026-06-03
**Sources:** (1) nRF51 firmware re-disassembly (`firmware/decompiled/RE_FINDINGS.md`,
`DECOMPILATION.md` §4e/§4f); (2) **disassembly of the official Segway-Ninebot app**
(`com.ninebot.segway`, XAPK from APKPure, base + `config.arm64_v8a.apk` native libs); (3) community
docs (`etransport/ninebot-docs`).

> The APK was downloaded (337 MB XAPK) and unpacked: base APK (RN/Hermes, one 208 KB loader dex,
> encrypted `assets/nedata.db`) + the `config.arm64_v8a.apk` native split. The Ninebot **protocol codec
> and crypto are native** (Rust/C++ `.so`); the **GATT UUIDs are not static** in the APK (delivered via
> the runtime React-Native bundle through `libbundle_manager.so`), so the **transport UUIDs come from
> the firmware RE**, while the **framing + encryption come from the app's native libs**. Tooling
> downloaded: `apktool 2.9.3` (Java 8), `unzip`, `strings`.

---

## 1. Transport (from firmware RE)

- The dashboard's BLE SoC is the **nRF51822** running **SoftDevice S110** (app @ `0x00018000`).
- BLE transport = **Nordic UART Service (NUS)** — the bonded app writes/notifies framed bytes over the
  NUS RX/TX characteristics (UUID base `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`, per
  `boards/ble-dashboard/PINOUT.md`). The nRF51 bridges NUS ⇄ STM32 UART.
- The nRF51 parser (`@0x00018450`) frames **Ninebot `5A A5`** packets toward the phone with
  `expected = LEN + 8` (normal) / `LEN + 0x0D` (MiIO), checksum byte-identical to the ESC/BMS core.

## 2. Framing codec (from the app — `libnbenc_ffi.so`)

The official app carries the Ninebot/M365 wire protocol in a native Rust/C++ FFI codec. Verified
exported symbols (demangled):

| Symbol | Meaning |
|--------|---------|
| `is_frame_header_AA55(const u8*)` | frame-header detector — confirms a **header-framed** protocol (the unified app handles both M365 `55 AA` and Ninebot `5A A5`) |
| `nb_encrypt(crypto_param_t*, u8* in, u16 len, u8* out)` | encrypt a Ninebot packet payload |
| `crypto_encrypt` | top-level encrypt entry |

This corroborates the firmware-verified framing (`docs/REGISTER_MAP.md`): `5A A5 | LEN(payload) | SRC |
DST | CMD | ARG | payload | checksum`. The codec confirms the protocol is **header-delimited and
length-prefixed** exactly as reconstructed from the ESC/BMS/nRF firmware.

## 3. Security / pairing (from the app — `libnbenc_ffi.so` + `libnbcrypto.so`)

The current app encrypts BLE payloads. Verified symbols:

| Symbol / class | Meaning |
|----------------|---------|
| `cn.ninebot.nbcrypto.NbEncryption` (JNI class) | the app's BLE crypto bridge |
| `Java_..._NbEncryption_crypto_setAuthParam`, `..._crypto_setKey` | JNI: set auth param + session key |
| `setAuthParam(u8*)`, `setKey/setKey1/setKey2/setKeys(u8*,u8*)` | key/auth handshake |
| `Key_rule_analysis(u8*, u8*, key_rule_t*)` | derive the session key per a "key rule" |
| `AES_init_ctx`, `AES_ECB_encrypt`, `AES_ECB_decrypt` | **AES-ECB** payload cipher |
| `Encryption::rc4`, `Encryption::rc4_init` | **RC4** cipher (legacy/handshake) |
| `MD5::encode`, `MD5::decode` | **MD5** in key derivation |

**Conclusion:** the modern Segway-Ninebot app wraps Ninebot frames with **AES-ECB** payload encryption
using a **session key derived (MD5 + "key rule") from an auth parameter** negotiated at connect — i.e.
a challenge/response pairing. RC4 + MD5 appear in the handshake/legacy path.

## 4. Two app/firmware generations (important for "compatible with the original app")

| Generation | App | Auth/crypto | Evidence |
|------------|-----|-------------|----------|
| **Legacy** (stock `BLE_1.1.x`) | Mi Home / early Ninebot | **Xiaomi MiIO** (token/beaconkey, `LEN+0x0D` frames) | nRF51 firmware strings/SVCs (`DECOMPILATION.md` §4f) |
| **Current** | **Segway-Ninebot** (`com.ninebot.segway`) | **nbcrypto**: AES-ECB + RC4 + MD5 key-rule, `setAuthParam`/`setKey` | the APK native libs (this doc) |

The **stock nRF51 firmware in this repo (`BLE_1.1.x`) speaks MiIO** — to stay compatible with the app
that bonded to *that* hardware, the custom firmware must reproduce the **MiIO** handshake. To target the
**current** Segway-Ninebot app, it must reproduce the **nbcrypto AES key-rule** handshake instead.

## 4b. Live-device confirmation (real G30 dashboard over PC Bluetooth, 2026-06-15)

The dashboard was connected to from a PC (`tools/ble_ninebot.py`, bleak) on the **live scooter**, which
confirms the transport, the GATT layout, and that the **MiIO auth gates the NUS relay on real hardware**:

| Property | Value |
|----------|-------|
| Advertised name | **`G30LD`** |
| MAC | **`D8:68:BA:16:A0:33`** |
| Service — data transport | **Nordic UART Service** (`6e400001-…`) |
| Service — pairing/auth | **Xiaomi MiIO `0xfe95`** |
| MiIO product id | **`0x035C`** |

**Exact MiIO `0xfe95` characteristics read on the device:**

| Char UUID (short) | Role |
|-------------------|------|
| `0x0001` | control |
| `0x0004` | beaconkey |
| `0x0010` | auth |
| `0x0013` | token |
| `0x0014` | device-id |

**Verified on hardware:** the connection succeeds and the MiIO info chars (incl. product `0x035C`) read
fine, but the nRF51 **relays/answers nothing** without the MiIO handshake — raw Ninebot `5A A5` frames,
MiIO-control `0x0001` writes, and dummy auth writes all produced **zero notifications**. Per the nRF51
RE, stock `BLE_1.1.x` runs the full MiIO flow (auth challenge → **token login-confirm → cloud bind →
register**) before bridging NUS↔STM32 (UART0), so the relay is keyed by the device's **MiIO
registration token** — a per-device secret held in the Xiaomi cloud / the owner's Mi Home account.

> **Two-channel auth wall.** Combined with the wired-bus finding (entering the dashboard bootloader over
> the ESC bus needs the chip-UID password, CMD `0x57` — see
> [`protocol.md`](protocol.md) and [`REGISTER_MAP.md`](REGISTER_MAP.md)), the stock dashboard is locked
> against reflashing **two ways by design**: wired = chip-UID password, BLE = MiIO registration token.
> Neither secret is derivable from the bus or an unauthenticated BLE read. See
> [`../Documentation/VERIFICATION_REPORT.md`](../Documentation/VERIFICATION_REPORT.md) §8.

## 5. What the custom nRF51 firmware must implement (Req 2, 3)

To be **app-compatible**, the custom BLE firmware must, on the BLE side:
1. Advertise with the stock name/format the app scans for (`"NBScooter…"`/`"Ninebot-Mini…"` seen in
   firmware; `g30.svga` etc. confirm G30 support in the app).
2. Expose the **GATT service the app expects** — Nordic UART Service (NUS) characteristics for framed
   data (firmware-verified transport). (Custom companion apps / VESC-Tool can use a second NUS instance.)
3. Run the **Ninebot framing** (`5A A5`, verified core in `ninebot_protocol_verified.hpp`).
4. Implement the **pairing/crypto** matching the target app generation:
   - **MiIO** (token / beaconkey / login-confirm) for the legacy app — see `DECOMPILATION.md` §4f; or
   - **nbcrypto** (AES-ECB session key via auth-param/key-rule) for the current app — §3 above.
5. Bridge framed payloads to the STM32 over UART; coexist with the **VESC App NUS** and **Haystack**
   (Req 3, 15).

> Reproducing Xiaomi MiIO or the nbcrypto key-rule **crypto** is the hard part (proprietary key
> derivation). The transport + framing + register semantics are fully specified here and in
> `REGISTER_MAP.md`; the crypto handshake must be reproduced from the legacy MiIO spec or the app's
> nbcrypto key-rule (out of scope to re-key here — documented so it can be implemented/cloned from the
> bonded device's stored keys).

## Evidence appendix (exact strings)
```
libnbenc_ffi.so:  _Z20is_frame_header_AA55PKh         is_frame_header_AA55(unsigned char const*)
                  _Z10nb_encryptP14crypto_param_tPhtS1_ nb_encrypt(crypto_param_t*, u8*, u16, u8*)
                  _Z15AES_ECB_encryptPK7AES_ctxPh      AES_ECB_encrypt(AES_ctx const*, u8*)
                  _ZN10Encryption3rc4EPhi / 8rc4_init  Encryption::rc4 / rc4_init
                  _Z17Key_rule_analysisPhS_P10key_rule_t  Key_rule_analysis(...)
                  _ZN3MD56encodeEPKjPhm                MD5::encode
                  setKey1 / setKey2 / setKeys / crypto_encrypt
libnbcrypto.so:   Java_cn_ninebot_nbcrypto_NbEncryption_crypto_1setAuthParam
                  Java_cn_ninebot_nbcrypto_NbEncryption_crypto_1setKey
                  _Z12setAuthParamPh  _Z7setKeysPhS_  AES_ECB_encrypt/decrypt  AES_init_ctx
```

Sources: official app `com.ninebot.segway` (APKPure XAPK, native `config.arm64_v8a.apk`) ·
`firmware/decompiled/RE_FINDINGS.md` · `etransport/ninebot-docs`.
