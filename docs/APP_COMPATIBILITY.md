# Keeping the Original Phone App Working on the VESC Scooter

**Date:** 2026-06-09
**Question this answers:** *"How do we make the new (VESC + Daly) scooter concept compatible with the
old Ninebot/Segway phone app?"*

**Builds on:** [`BLE_PROTOCOL_VERIFIED.md`](BLE_PROTOCOL_VERIFIED.md) (firmware RE + APK disassembly),
[`REGISTER_MAP.md`](REGISTER_MAP.md), and the new firmware modules
[`dash_bridge.h`](../firmware/decompiled/ble/include/dash_bridge.h) /
[`vesc_tunnel.h`](../firmware/decompiled/nrf51822/include/vesc_tunnel.h).

---

## 1. The core problem

The original app expects to talk to a **stock ESC at address `0x20`** using the Ninebot **register
protocol** (`CMD 0x01` READ / `0x02` WRITE, ARG-indexed register file). In the new build **the stock
ESC is gone** (replaced by a VESC), and the VESC lisp only speaks the **head-I/O protocol** (`0x64`
display / `0x65` throttle) — it does **not** answer register reads. So out of the box, the app would
connect over BLE but read nothing.

**Three things must line up for app compatibility:**

| Layer | Who provides it | Status |
|-------|-----------------|--------|
| **Transport** — BLE GATT the app scans for | nRF51 advertises stock name + **Nordic UART Service** (NUS) | firmware-verified; custom nRF51 reproduces it |
| **Framing** — `5A A5 | LEN | SRC | DST | CMD | ARG | payload | Σ^0xFFFF` | shared verified core (`ninebot_protocol_verified.hpp`) | ✅ implemented + tested |
| **Semantics** — answers to the ARG-indexed register reads/writes | **`DashBridge`** synthesizes a stock-ESC register image from VESC/Daly telemetry | ✅ implemented + tested (`test_new_modules.cpp`) |
| **Auth/crypto** — pairing handshake | MiIO (legacy `BLE_1.1.x`) **or** nbcrypto (current Segway app) | documented; backend interface designed (`nb_auth`) |

---

## 2. Data path in the VESC build

```
 phone app ──BLE NUS──► nRF51 (nb_ble_bridge) ──UART──► STM32 (DashBridge)
   register R/W (DST 0x20)                                  │ onAppPacket()
                              ◄──── read-response ──────────┘ from synthetic ESC image
                                                            ▲
   VESC lisp ─0x64 display─► parseDisplayFrame() ───────────┘  (fills the image)
   ADC throttle/brake ─────► buildThrottleFrame() ─0x65─► VESC lisp
   VESC Tool (2nd NUS) ◄────────── vesc_tunnel ──────────► VESC UART (raw packets)
```

- The **nRF51** runs **two** NUS instances: the **App NUS** (stock Ninebot framing → STM32) and a
  **VESC NUS** that tunnels raw VESC packets for **VESC Tool mobile** (Req 3). They coexist; the stock
  app never sees the VESC packets.
- The **STM32 `DashBridge`** is the piece that makes the app think a stock ESC is present: it keeps an
  ARG-indexed register image, fills it from the VESC's `0x64` display frame (and/or direct telemetry),
  and answers the app's reads/writes locally.

---

## 3. Register synthesis — what the app reads ↦ where the value comes from

These are the registers the app polls to render its UI, and how `DashBridge` produces each on a
VESC scooter (ARG semantics from [`REGISTER_MAP.md`](REGISTER_MAP.md)). **High-confidence, UI-critical
rows in bold.**

| ARG | App shows | Source on the VESC build | Notes |
|-----|-----------|--------------------------|-------|
| 0x00 | (probe) ESC magic | constant `0x515C` | makes the app accept the "ESC" |
| 0x1A | ESC firmware version | reported `0x0613` (spoofed) | so the app shows a plausible ESC FW |
| **0x1B** | **Error / fault code** | mapped from VESC `get-fault` / `0x64` error field | bitmask |
| **0x22** | **Battery %** (the big one) | VESC `(get-batt)*100` **or** Daly `0x90` SOC | from `0x64` batt field or telemetry |
| **0x26** | **Current speed** (0.1 km/h) | VESC `(get-speed)*3.6` | `0x64` speed field ×10 |
| 0x47/0x48 | Supply / battery voltage | VESC `get-batt`/Daly `0x90` total V | scaling is a calibration point |
| 0x49 | Battery current | VESC current / Daly `0x90` current | signed (+ discharge) |
| 0x3E | Frame/controller temp | VESC `get-temp-mot` | 0.1 °C |
| 0x41 | MOSFET temp | VESC `get-temp-fet` | 0.1 °C |
| 0x29 | Total distance | VESC odometer (`get-dist`/persisted) | optional |
| 0x1F / **0x75** | **Operation mode** | mode state (Eco/Drive/Sport) | `0x64` mode field |

**Writes the app issues** (handled by `DashBridge::onAppPacket` → `BridgeAction` the firmware relays to
the VESC):

| ARG | App action | Bridge behaviour |
|-----|-----------|------------------|
| 0x75 | set riding mode | `SetMode` → VESC `apply-mode` |
| 0x70 / 0x71 | lock / unlock | `Lock` / `Unlock` → VESC lock logic |
| 0x7D | tail light | `SetLight` → VESC PPM light |
| 0x7B | KERS / regen level | stored; map to VESC regen if desired |
| 0x78 / 0x79 | reboot / power down | `Reboot` / `PowerOff` → keeper power-cut |
| **0x72 / 0x73 / 0x74** | **speed-limit writes** | **accepted + stored but NEVER used to clamp** (Req 4 — speed-cap removal). The VESC owns real limits via ERPM. |

> Implemented and locked by tests in
> [`test_new_modules.cpp`](../firmware/decompiled/tests/test_new_modules.cpp): synthesized-battery read,
> `0x65` throttle offsets matching `g30_dash.lisp`, `0x64` parse → register update, mode-write action,
> and **no-speed-clamp** on a `0x73` write.

---

## 4. Auth / pairing — the hard part, and which generation to target

The app encrypts BLE payloads; the handshake differs by app generation (from the APK native libs +
firmware RE — see [`BLE_PROTOCOL_VERIFIED.md`](BLE_PROTOCOL_VERIFIED.md) §3–4):

| Generation | App | Handshake | What the custom nRF51 must reproduce |
|------------|-----|-----------|--------------------------------------|
| **Legacy** | Mi Home / early Ninebot (bonded to the stock `BLE_1.1.x`) | **Xiaomi MiIO** — token / beaconkey / login-confirm; `LEN+0x0D` frames | the MiIO state machine (`DECOMPILATION.md` §4f); keys cloned from the bonded device or via the MiIO algorithm |
| **Current** | **Segway-Ninebot** `com.ninebot.segway` | **nbcrypto** — AES-ECB session key from `Key_rule_analysis(authParam)` + MD5; RC4 in the handshake | the nbcrypto key-rule (`NbEncryption`) |

**Practical guidance:**
- The proprietary **key derivation** is the only genuinely hard part; **transport + framing + register
  semantics are fully specified and implemented**. `nb_auth` is a clean backend interface
  (`auth_init / auth_on_write / auth_wrap_tx / auth_unwrap_rx`) with selectable `NONE | MIIO | NBCRYPTO`.
- **Bring-up order:** start with `NB_AUTH=NONE` (plaintext framed protocol) against a **custom companion
  app or VESC Tool** to validate the whole pipe. Then add the auth backend for the app generation you
  must support.
- **Easiest route to the *stock* app:** target the generation your hardware was bonded to. The repo's
  stock nRF51 speaks **MiIO**, so reproducing MiIO (or cloning the bonded device's stored keys via PSM)
  keeps the *original* app working. Targeting the *current* Segway app means implementing the nbcrypto
  key-rule.
- We do **not** re-publish the proprietary keys here — the backend is structured so the correct rule can
  be supplied (documented MiIO spec, or cloned keys).

---

## 5. What stays stock vs. what is custom

| Piece | Stock or custom | Why |
|-------|-----------------|-----|
| BLE framing/register semantics | **reused verified core** | byte-identical to the scooter |
| App NUS transport | custom nRF51 reproduces stock | so the app connects unchanged |
| ESC register answers | **custom `DashBridge`** (synthetic) | the real ESC is gone |
| Throttle/brake to motor | VESC (lisp `app-adc-override`) | VESC owns motor control |
| VESC Tool BLE | **custom 2nd NUS tunnel** | new capability (Req 3) |
| Auth | reproduce MiIO **or** nbcrypto | match the target app |
| BMS data the app reads | from Daly via `daly_soft_uart`, re-exposed as BMS regs | Daly replaces the stock BMS |

---

## 6. Remaining work to be "app-verified on hardware"
- [ ] nRF51: implement `nb_ble_bridge` App-NUS ⇄ UART on real S130 (host framer already tested).
- [ ] nRF51: `nb_auth` MIIO or NBCRYPTO backend for the target app generation.
- [ ] STM32: wire `DashBridge` into `ble_main` (relay path) so app reads are answered from the image.
- [ ] On-device: pair with the original app, confirm it reads battery/speed/mode/fault and that
      speed is uncapped; confirm VESC Tool over the 2nd NUS; confirm FindMy in Haystack mode.
