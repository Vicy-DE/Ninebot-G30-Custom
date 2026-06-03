# Power Latch — Daly cuts VESC power on OFF, power button wakes it

**Date:** 2026-06-03
**Goal:** the **Daly BMS discharge FET is the master power switch** for the VESC. A **long-press of the
dashboard power button turns the system fully OFF** (Daly opens discharge → VESC loses power → BMS
sleeps, ~µA). A **short press from OFF wakes** the Daly → VESC powers up. Only the original dashboard
cable is used; the only added hardware is a small always-on **Power-Latch Controller (PLC)**.

---

## 1. Why a latch is needed (the cold-start problem)

```
OFF state:   Daly discharge = OPEN  →  VESC = unpowered  →  dashboard = unpowered
```
The power button lives on the dashboard and its line only reaches the **VESC** (original cable). When
everything is off, the VESC can't sense the button, and the dashboard can't either. **Something that is
always powered must (a) sense the press and (b) re-enable the Daly discharge.** That is the PLC: a tiny
MCU on an always-on micro-supply tapped from the raw cell pack (`B+/B−`, upstream of the discharge FET).

Daly control primitives used (firmware-confirmed, see Sources):
- **`S1` "button activation" pin** — active-LOW; grounding it **wakes** a sleeping BMS.
- **UART `0xD9`** discharge-FET control @ 9600 8N1:
  - **ON :** `A5 40 D9 08 01 00 00 00 00 00 00 00 C7`
  - **OFF:** `A5 40 D9 08 00 00 00 00 00 00 00 00 C6`

---

## 2. System schematic (block level)

```
            20S Li-ion pack
            ┌───────────────┐
   B+ ●─────┤+             −├─────● B−
      │     └───────────────┘     │
      │                           │
      │   ┌───────────────────────┴───────────────────────────┐
      │   │                 Daly Smart BMS 20S 100A            │
      │   │   ┌──────────────┐                                 │
      ├───┤B+ │ Discharge FET │  S1 ●  UART(TX/RX) ●           │
      │   │   │  (in B−/P−)   │   │        │  │                │
      │   │   └──────┬────────┘   │        │  │                │
      │   └──────────┼────────────┼────────┼──┼─────● P−       │
      │           P+ │            │        │  │      │ (switched −)
      │              │            │        │  │      │
      │   ┌──────────┴──┐         │        │  │      │
      │   │ ALWAYS-ON   │         │        │  │      │
      ├──►│ buck 100V→5V│──5V_AON─┤        │  │      │
      │   │ (MP9486)+1A │   │     │        │  │      │
      │   │ fuse        │   │ ┌───┴────────┴──┴───┐  │
      │   └─────────────┘   └─┤ POWER-LATCH CTRL  │  │
      │     (from raw B+,     │  (ATtiny/RP2040)  │  │
      │      always live)     │  5V→3V3 LDO       │  │
      │                       │                   │  │
      │             BTN ●─────┤ btn-in (pull-up)  │  │
      │           KEEPALIVE●──┤ ka-in             │  │
      │                       │  S1-out (o.d.) ●──┘  │
      │                       │  DALY-TX/RX ●────────┘  (9600 8N1, 3V3)
      │                       └───────┬───────────┘
      │                               │
      │   XT90                        │  BTN also goes to ↓
      ▼   ┌───────────────────────────┴──────────────────────────┐
   (P+)──►│                 100 V-class VESC                       │
          │  B+ ◄P+   B− ◄P−                                       │
          │  COMM: 5V(→dash) GND  TX(data)  RX(button)             │
          │  ADC2 ──────────────────────────► KEEPALIVE (3V3, lisp)│
          │  SERVO/PPM ──► MOSFET driver ──► light                 │
          │  MT60 phases ; PH-6 Hall                               │
          └───────────────────────────────────────────────────────┘
                                   │ original 4-pin cable
                                   ▼  (5V from VESC COMM, GND, data→TX, button→RX)
                          ┌──────────────────┐
                          │  Stock Dashboard │  button line is shared:
                          │  STM32 + nRF51   │   dashboard → VESC RX  AND  → PLC btn-in
                          └──────────────────┘
```

**Net summary**
| Net | From → To | Notes |
|-----|-----------|-------|
| `5V_AON` | MP9486 (from raw **B+/B−**, 1 A fuse) → PLC | always live, even when Daly discharge open |
| `BTN` | dashboard button wire → **PLC btn-in** *and* **VESC RX** | spliced on the controller side (no extra wire to dashboard); pull-up on 3V3 |
| `S1` | **PLC S1-out** (open-drain) → Daly **S1** | pulse LOW to wake the BMS |
| `DALY-TX/RX` | **PLC UART** ↔ Daly **UART** | 9600 8N1, 3V3; sends `0xD9` ON/OFF |
| `KEEPALIVE` | **VESC ADC2** (lisp GPIO, 3V3) → **PLC ka-in** | HIGH = VESC running; LOW = power-off request |
| Power | Daly **P+/P−** → VESC (XT90, 100 A ANL fuse) | the switched main rail |

> **Levels:** run the PLC at **3.3 V** so `BTN`, `KEEPALIVE`, and the Daly UART all share 3V3 logic
> (the VESC RX/ADC and the Daly UART are 3.3 V). 5V_AON → 3V3 LDO on the PLC board.

---

## 3. PLC logic (state machine)

```
        ┌────────────── OFF ──────────────┐
        │ Daly discharge OPEN (asleep)     │
        │ VESC + dashboard UNpowered       │
        │ PLC idle, btn-in pulled high     │
        └───────────────┬──────────────────┘
            BTN pressed (debounced ≥30 ms)
                        ▼
        ┌──────────── WAKE ───────────────┐
        │ S1-out LOW 200 ms (wake Daly)    │
        │ send UART 0xD9 ON                │
        │ → discharge closes → VESC powers │
        └───────────────┬──────────────────┘
            wait ≤3 s for KEEPALIVE = HIGH
              ├─ HIGH ──────────────► ON
              └─ timeout ─► 0xD9 OFF ─► OFF   (VESC failed to boot)
        ┌──────────────── ON ─────────────┐
        │ hold; ignore BTN (VESC owns it)  │
        │ watch KEEPALIVE                  │
        └───────────────┬──────────────────┘
            KEEPALIVE LOW (debounced ≥200 ms)   ← VESC long-press OFF
                        ▼
                 send UART 0xD9 OFF
                 → discharge OPENS → VESC power cut → Daly sleeps
                        ▼
                       OFF
```

- **ON:** VESC long-press → `g30_dash.lisp` drives **ADC2 LOW** → PLC sends **`0xD9 OFF`** → the **Daly
  opens the discharge FET → the VESC loses power** (this is the requested "Daly cuts the power").
- **OFF→ON:** press → PLC pulses **S1** (wake) and sends **`0xD9 ON`** → VESC powers → lisp raises ADC2.
- Standby draw: MP9486 (~1 mW) + PLC sleep (~tens of µA) + Daly sleep (~µA). For long storage, still add
  a manual XT90 disconnect.

---

## 4. Added BOM (on top of the wiring plan)

| # | Part | Spec | Purpose |
|---|------|------|---------|
| L1 | Always-on buck | **MP9486** 100 V→5 V, 1 mA quiescent | always-live supply from raw B+ |
| L2 | 3V3 LDO | e.g. MCP1700-3.3 | PLC + logic rail |
| L3 | **PLC MCU** | ATtiny412/416 or RP2040-Zero (3V3) | latch logic + Daly UART |
| L4 | MOSFET (S1 o.d.) | small N-ch (2N7002) + 100 kΩ | open-drain S1 wake pulse |
| L5 | Pull-ups/decoupling | 10 kΩ (BTN, KEEPALIVE), 100 nF | logic |
| L6 | 1 A fuse | 5×20 mm / PTC | protects the always-on tap |
| L7 | Diode (BTN OR) | BAT54S (optional) | isolate VESC-RX vs PLC btn-in if needed |

> **Inrush:** when the Daly discharge FET closes into the VESC's bulk caps there is an inrush spike.
> Use an XT90-**S** (anti-spark) on the VESC feed, or a pre-charge resistor, to protect the FET/contacts.

---

## 5. VESC keep-alive (already wired into the Lisp)

`vesc-lisp/g30_dash.lisp` now:
- configures **ADC2 as a GPIO output** and drives it **HIGH at boot** (`keep-alive asserted`);
- drives it **LOW in `handle-holding-button`** (long-press OFF) so the PLC cuts power via the Daly.

```lisp
(def pin-keepalive 'pin-adc2)               ; VESC GPIO → PLC KEEPALIVE (HIGH = stay on)
(gpio-configure pin-keepalive 'pin-mode-out)
(gpio-write pin-keepalive 1)                ; assert at boot
; ... on long-press OFF:  (gpio-write pin-keepalive 0)
```

Pick any free lisp-addressable VESC GPIO if ADC2 is unavailable on your board
(`'pin-adc1`/`'pin-adc2`/`'pin-swdio`/…). Keep the light on `'pin-ppm` (§ wiring plan).

---

## 6. Variants / trade-offs

| Variant | Daly cuts power? | Extra MCU? | When to use |
|---------|:----------------:|:----------:|-------------|
| **D — Dashboard-as-keeper + dual-purpose wires (RECOMMENDED, §7)** | ✅ yes | **none** | this project (we already build custom dashboard FW); mirrors the stock scooter |
| **A — Power-Latch Controller + UART `0xD9` (§2–§5)** | ✅ yes | yes (tiny) | if you keep **stock** dashboard FW |
| **B — No-MCU, S1 wake only** | ✅ if your Daly *restores discharge on wake* | none | only if your Daly re-enables discharge after S1 wake — **verify first** |
| **C — Hardware soft-latch P-FET load switch** | ❌ external FET cuts (Daly = protection) | none | simplest/most robust if the Daly itself need not switch |

Variant C is the classic ebike "soft power switch" (P-FET high-side + keep-alive); see the Hackaday/
Mosaic references. **Solution D (below) is the elegant fit for this build** — no added MCU, uses the
custom dashboard as the always-on brain exactly like the original scooter.

---

## 7. Solution D (recommended): the dashboard is the keeper — no extra MCU

### How the original G30 solves it
On the stock scooter the **dashboard is the always-on master**. It sits in a deep-sleep micro-power
state powered from a tiny standby rail, **wakes on the power-button**, then brings the rest of the bus
up and tells it to sleep again on power-off. The ESC/BMS don't watch the button themselves — the
dashboard does, and it *commands* power. We reproduce exactly that: the **dashboard (running our custom
energy-efficient firmware) becomes the keeper**, so no separate controller is needed.

The trick is that the dashboard only has its **original 4-wire cable**, yet must (a) stay alive, (b)
talk to the **VESC** (115200 Ninebot) *and* (c) talk to the **Daly** (9600, `0xD9`) + wake it (`S1`).
We get there by **repurposing the 4 wires** — "ugly but it fits":

| Orig wire | Stock use | New use (Solution D) |
|-----------|-----------|----------------------|
| 1 — **5V** | dashboard power (from ESC) | **always-on** 5 V from a low-Iq HV rail off raw **B+** (or the Daly UART VCC if always-on) |
| 2 — **GND** | ground | ground |
| 3 — **DATA** | Ninebot half-duplex | **VESC only**, 115200 Ninebot half-duplex (stays clean) |
| 4 — **BUTTON** | button sense to ESC | **dashboard GPIO → bit-banged 9600 → Daly RX + Daly S1** (stays clean) |

The dashboard reads the **physical button internally** (its own PB12) — it no longer needs to export the
button on a wire — which frees wire 4 to become the dashboard's **Daly control line**. One wire does
double duty: a 9600 `0xD9` frame's leading edges also drive `S1` low, so **transmitting the command also
wakes the BMS**. Each device sees only its own protocol (no cross-baud garbage on either bus).

### Solution D schematic

```
   20S pack  B+ ●───────────────┬─────────────────────────► Daly B+
                                 │
                    ┌────────────┴────────────┐
                    │ low-Iq HV rail 84V→5V    │  (always live, before discharge FET;
                    │ (or Daly UART VCC)       │   ~tens of µA quiescent target)
                    └────────────┬─────────────┘
                                 │ 5V_AON
   ── original 4-wire dashboard cable (only cables to the dashboard) ──
        w1 5V_AON ─────────────► Dashboard VDD (always on)
        w2 GND    ─────────────► GND
        w3 DATA   ───┬─────────► VESC COMM data (115200 Ninebot, half-duplex)
        w4 BTNCTL ───┼───┐
                     │   └─────► Daly  S1   (active-LOW wake)
                     │   └─────► Daly  RX   (9600 8N1, bit-banged 0xD9 frames)
                     │
   ┌─────────────────┴───────────────────────────────────────────┐
   │  Dashboard  (STM32, custom energy-efficient firmware)        │
   │   • physical power button on internal PB12 (EXTI wake)       │
   │   • STOP-mode sleep (~µA) when OFF                            │
   │   • ON  : bit-bang Daly 0xD9 ON on w4 (edges pulse S1 → wake) │
   │           → Daly discharge closes → VESC powers              │
   │   • OFF : (long-press) bit-bang Daly 0xD9 OFF on w4          │
   │           → Daly opens discharge → VESC power cut            │
   │   • forwards button/throttle/brake to VESC as Ninebot frames │
   │     on w3; renders VESC telemetry (frame 0x64)               │
   └──────────────────────────────────────────────────────────────┘
        Daly P+/P− ──XT90(100A ANL)──► VESC (powered only when discharge on)
        VESC: g30_dash.lisp (throttle/brake/mode), light on PPM (§ wiring plan)
```

### Power-on / power-off sequence (Solution D)
1. **OFF:** Daly discharge open (asleep, ~µA). VESC unpowered. **Dashboard alive** in STOP on `5V_AON`,
   watching PB12.
2. **Press:** dashboard wakes → bit-bangs **`A5 40 D9 08 01 … C7`** on w4 (the start-bit edges also pull
   `S1` low → Daly wakes) → discharge closes → **VESC powers**.
3. **Run:** dashboard talks 115200 Ninebot to the VESC on w3 (throttle/brake/mode/telemetry); VESC runs
   `g30_dash.lisp`; light on the PPM driver.
4. **Long-press:** dashboard bit-bangs **`A5 40 D9 08 00 … C6`** on w4 → **Daly opens discharge → VESC
   power cut** → dashboard returns to STOP.

### Firmware responsibilities (Solution D)
- **Dashboard (custom FW, `firmware/decompiled/ble`):** STOP-mode sleep + button-EXTI wake; a soft 9600
  UART on the w4 GPIO that emits the two `0xD9` frames; button/throttle/brake → Ninebot frames to the
  VESC on w3; render telemetry. (This is the natural home for the keeper logic — it's what the stock
  dashboard does.)
- **VESC (`g30_dash.lisp`):** unchanged motor/throttle/brake/mode/light logic. The ADC2 **keep-alive**
  added earlier is **not needed** in Solution D (the dashboard owns on/off) — leave it unconnected, or
  keep it as a redundant cross-check.

### Notes / "ugly but fits" caveats
- **One always-on part remains** — the µA HV rail (or the Daly's always-on UART VCC). True zero-draw
  still needs a manual disconnect for long storage.
- **w4 dual use** (S1 + 9600 Daly-RX): verify the Daly tolerates the start-bit edges as `S1` pulses
  (it does — `S1` just wants a LOW to wake; a UART frame provides plenty). If your Daly's `S1` needs a
  longer hold, precede the frame with a short LOW break.
- If you prefer to keep **stock dashboard firmware**, use **Solution A** (the PLC) instead — same Daly
  behaviour, at the cost of one tiny MCU.

## Sources
- Daly discharge control & frames (`0xD9` ON/OFF), S1 "button activation" wake:
  [Decoding the DALY SmartBMS protocol (DIY Solar)](https://diysolarforum.com/threads/decoding-the-daly-smartbms-protocol.21898/) ·
  [maland16/daly-bms-uart](https://github.com/maland16/daly-bms-uart/blob/main/daly-bms-uart.cpp) ·
  [DALY communication protocols](https://www.dalybms.com/news/daly-three-communication-protocols-explanation/)
- Soft-latch / push-button power circuits (Variant C reference):
  [Hackaday: soft-latching roundup](https://hackaday.com/2019/06/24/ditch-the-switch-a-soft-latching-circuit-roundup/) ·
  [Mosaic Industries push-button on/off](http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/electronic-circuits/push-button-switch-turn-on/latching-toggle-power-switch) ·
  [DigiKey ebike soft power switch](https://forum.digikey.com/t/electric-bike-soft-power-switch-mosfet-vs-fc270sa20/17834)
