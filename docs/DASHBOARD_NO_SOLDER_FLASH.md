# Custom-Firmware the G30 Dashboard **Without Soldering** — Wiring & Flash Plan

**Date:** 2026-06-09
**Goal:** put custom firmware on the G30 Max **BLE dashboard board** (STM32F103C8T6 + nRF51822) using
the **stock firmware-update mechanism** and **no soldering to the PCB** — just connectors, test clips,
and (where needed) a spring-pin/pogo clip on existing pads.

**Grounded in:** [`../boards/ble-dashboard/DASHBOARD_PINOUT_RESEARCH.md`](../boards/ble-dashboard/DASHBOARD_PINOUT_RESEARCH.md)
(cable + pads + IAP, all citation-backed), [`iap-update-protocol.md`](iap-update-protocol.md) (the IAP
sequence), [`../firmware/decompiled/RE_FINDINGS.md`](../firmware/decompiled/RE_FINDINGS.md) (no code
signing on the stock images), and [`WIRING_PLAN_DALY_VESC.md`](WIRING_PLAN_DALY_VESC.md).

> **Before anything: do you even need to reflash the dashboard?** See the decision tree (§0). For a
> basic VESC swap you may not need to touch the dashboard firmware at all.

---

## 0. Decision tree — which dashboard firmware do you actually need?

| You want… | STM32 dash FW | nRF51 BLE FW | How (no-solder) |
|-----------|---------------|--------------|-----------------|
| **Basic VESC ride** (display, throttle, modes, lights, lock) | **Stock — unchanged** | **Stock — unchanged** | None. The VESC runs `vesc-lisp/g30_dash.lisp` and speaks the stock dashboard's `0x64`/`0x65` head protocol. ✅ simplest |
| **Power latch "Solution D"** (dashboard sleeps, Daly cuts VESC power on long-press) + **synthetic ESC registers** for the app | **Custom** (`dash_keeper`, `dash_bridge`, `daly_soft_uart`) | Stock or custom | **STM32 via serial IAP** (§2). No solder. |
| **App-compatible BLE + VESC Tool BLE + FindMy/Haystack** | Custom or stock | **Custom** (`nb_ble_bridge`, `vesc_nus`, `haystack`, `mode_ctrl`) | **nRF51 via SWD pogo** (§3) — stock OTA won't take arbitrary images |
| **Secure bootloader at `0x08000000`** (Phase 3) | Custom BL | Custom BL | **SWD pogo only** (§3) — IAP can't overwrite the bootloader region |

**Reversibility first (Phase 0):** before flashing anything, **back up** what you can and keep stock
`.bin`/`.enc` images on hand. Serial-IAP and SWD-app flashes are reversible (reflash stock); writing a
bootloader at `0x08000000` is SWD-only to undo.

---

## 1. The no-solder wiring — what you connect

You need a **CP2102 USB-TTL adapter set to 3.3 V** (an **ST-Link is NOT compatible with serial IAP**),
plus **test clips / Dupont** to a tap point. Two tap options, both solder-free:

### Option A (recommended) — internal **ESC↔BLE 7-pin connector** (full-duplex UART)
Open the deck, **unplug** the 7-pin harness connector between the ESC bay and the BLE/dash, and clip
onto these pins (ScooterHacking-documented, **[CONFIRMED]**):

```
 7-pin connector            CP2102 USB-TTL
  Pin 1  = +5 V    ──────►  (leave NC if board self-powered; or 5V in)
  Pin 2  = GND     ──────►  GND
  Pin 6  = RX      ◄─────   TX     (scooter-RX ← adapter-TX)
  Pin 7  = TX      ─────►   RX     (scooter-TX → adapter-RX)
```
> Cross **TX↔RX**. Verify the pin numbering on your harness with a multimeter (continuity to the BLE
> board's 5 V rail and GND) before powering — connector keying/numbering varies by batch.

### Option B — external **4-pin dashboard cable** (half-duplex single wire)
Unplug the dashboard cable from the VESC/ESC and tap it directly. The data line is **single-wire
half-duplex**, so you must tie the adapter TX→bus through a **~1 kΩ resistor** and connect RX directly
to the bus (or use a small Schottky diode), else TX fights the bus:

```
  Red    = +5 V    ──►  (5 V in, only if powering the board from the adapter)
  Black  = GND     ──►  GND
  Yellow = DATA    ──►  RX (direct)  and  TX (through ~1 kΩ)   ← half-duplex
  Green  = button  ──►  not needed for flashing
```
> **Color ≠ pin** on replacement cables — ohm it out first. The yellow DATA line carries the same
> half-duplex bus, so IAP to the dashboard works here too, but Option A's separate RX/TX is easier.

**No soldering anywhere** in either option — connectors and clips only.

---

## 2. Flashing the **STM32 dashboard app** via serial IAP (no solder)

The stock 4 KB dashboard bootloader accepts the Ninebot serial-IAP sequence on the bus, addressed to
the **BLE/dashboard at `0x21`** (host = PC `0x3D`/`0x3F` or phone-BLE `0x3E`). RE confirms **no
cryptographic code-signing** on the stock images — the only gate is the IAP "unlock/safe-mode" check
(error `0x04`).

### 2a. The IAP sequence (recap — full detail in [`iap-update-protocol.md`](iap-update-protocol.md))
```
0x07 CMD_IAP_BEGIN   → [u32 firmware length]        (erases app region)
0x08 CMD_IAP_TRANS   → [block#, …data…]  ×N         (128-byte blocks, multiple-of-8)
0x09 CMD_IAP_VERIFY  → [u32 checksum]               (Σ^0xFFFF over the image)
0x0A CMD_MCU_RESET   → []                            (boots the new app)
```
Frame = `5A A5 LEN SRC DST CMD ARG data… CKlo CKhi`, checksum `Σ^0xFFFF`. **"Always flash the BLE
first."**

### 2b. Two ways to drive it
1. **Ninebot IAP web tool** (https://iap.scooterhacking.org) — Serial tab → Vehicle **Ninebot** →
   Interface **BLE (3E)** → pick/upload firmware. Easiest; expects Ninebot-formatted (often `.enc`)
   images. Use this to **restore stock** at any time (full reversibility).
2. **This repo's flasher** — [`../tools/flasher/ninebot_flasher.py`](../tools/flasher/ninebot_flasher.py)
   implements 0x07/0x08/0x09/0x0A and can push a **plain custom `.bin`** built for `0x08001000`:
   ```powershell
   python tools/flasher/ninebot_flasher.py --port COM5 --target BLE --addr 0x21 `
       --image firmware/decompiled/build/ble_app.bin --base 0x08001000
   ```
   (Build the app with the Phase-1 linker @ `0x08001000`; see `DEPLOYMENT.md`.)

### 2c. Custom-image caveat (read before relying on IAP for custom code)
- The stock bootloader has **no signature check** (good — a plain custom `.bin` should be accepted), but
  it **does** enforce the scooter-unlock / safe-mode gate. If you hit IAP error `0x04`, the unit is
  refusing the update state, not rejecting your signature.
- IAP writes the **app region only** (`0x08001000+`) and **cannot** replace the 4 KB stock bootloader.
  That is fine for **Phase 1** (custom app behind the stock bootloader — fully reversible). The **custom
  secure bootloader at `0x08000000` (Phase 3) needs SWD** (§3).
- **Always keep a stock BLE `.bin`/`.enc`** to reflash via the same path if the custom app misbehaves.

### 2d. Verify after flashing
Use [`verify-hw`](../.claude/commands/verify-hw.md): UART monitor @ 115200 8N1, confirm the boot banner,
that the dashboard answers register reads (battery `0x22`, speed `0x26`, mode `0x75`), throttle/brake
move the motor through the VESC, modes/lights/lock work, and long-press triggers the Daly power-cut.

---

## 3. Flashing the **nRF51822** (and the custom bootloader) — no-solder **SWD pogo**

The nRF51 **cannot** take arbitrary custom firmware over its stock OTA (MiIO-over-BLE is auth-gated and
ships `.enc`; image-signing is unconfirmed but the channel is locked to Xiaomi's flow). So for the
custom BLE firmware (app-compat + VESC Tool NUS + Haystack), use **SWD** — the nRF51 has **no readout
protection**, so it programs cleanly.

- **No-solder method:** a **spring-pin / pogo clip** (or hand-held pogo jig — community-proven on these
  boards) on the nRF51 SWD pads (**SWDIO / SWCLK / GND / VDD**). ⚠️ **Pad locations are undocumented on
  the G30** — locate them by inspection + the nRF51822 datasheet pinout (an open item).
- **Programmer:** ST-Link V2 or J-Link + `nrfjprog`/OpenOCD. Flash order: SoftDevice (S130) → custom
  app → (optional) Nordic/our DFU. See [`../firmware/decompiled/nrf51822/nrf51822-reprogramming.md`](../firmware/decompiled/nrf51822/nrf51822-reprogramming.md)
  and [`NRF51_BLE_FIRMWARE.md`](NRF51_BLE_FIRMWARE.md).
- **The STM32 custom bootloader at `0x08000000`** likewise needs the **STM32 SWD pads** (3 pads
  **GND/SWCLK/SWDIO** + a **5 V** pad under cap **C2**, or 5 V via the dashboard red wire) — pogo, no
  solder. This is the only path that bypasses the IAP format/unlock constraints entirely.

> "No soldering" is achievable for SWD via pogo pins, but the pads are **potted (scrape gently) and
> fragile**. If you only need the custom **app** (not a custom bootloader) on the STM32, prefer the
> serial-IAP path (§2) and avoid the pads.

---

## 4. Full no-solder sequence (recommended build)

1. **Phase 0 — back up.** Save stock BLE `.bin`/`.enc`. Note current firmware versions via the app/IAP.
2. **Decide scope** (§0). For most: keep stock dashboard FW and just run the VESC lisp — skip to your
   VESC bring-up (`WIRING_PLAN_DALY_VESC.md`).
3. **STM32 custom app (if needed):** Option-A tap → `ninebot_flasher.py` or Ninebot IAP → flash the
   Phase-1 app `@0x08001000` → `verify-hw`. Reversible: reflash stock the same way.
4. **nRF51 custom BLE (if needed):** locate SWD pads → pogo clip → SWD-flash S130 + custom app →
   pair with the original app + VESC Tool, test Haystack. (Open item: pad locations.)
5. **Phase 3 secure bootloader (optional, advanced):** STM32 SWD pogo on GND/SWCLK/SWDIO(+5V) →
   flash bootloader `@0x08000000` + signed app `@0x08004000`. SWD-only to revert.

---

## 5. Open hardware checks (carry a multimeter)
Inherited from the pinout research (§D there): confirm the **4-pin cable pin order**, the **7-pin tap
numbering**, the **STM32 SWD pad → MCU-pin** mapping, the **nRF51 SWD pad locations**, whether **5 V is
reachable without removing C2**, any **SWDCLK pull resistor**, and whether stock IAP accepts a **custom
unsigned image** on your unit. Verify each before trusting it.

---

## 6. Safety
- 3.3 V TTL only on the bus. Don't back-feed 5 V into a board that's already powered.
- Keep stock images; verify checksums before flashing (`verify-firmware`).
- The dashboard board can be powered from the deck while you tap it — disconnect the battery for any
  work near the power path, and never probe the nRF51/STM32 pads with the HV pack live.
