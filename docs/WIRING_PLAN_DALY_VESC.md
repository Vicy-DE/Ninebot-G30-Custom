# Wiring Plan — Ninebot G30 Max: Daly BMS + VESC + Stock Dashboard

**Date:** 2026-06-03
**Build:** up to **20S (84 V)** Li-ion · **Daly Smart BMS 20S 100 A** · **100 V-class VESC** · stock G30
BLE dashboard (original cable only) · backlight switched from the **VESC PPM/servo output** via a
**MOSFET driver**.

> Only three electronic units live in the scooter: **Daly BMS**, **VESC**, **dashboard**.
> The dashboard keeps its **original cable only** (no new wires to the dashboard). The power button
> works through that cable + the `g30_dash.lisp` button logic.

> ⚠️ **84 V is lethal and exceeds most jurisdictions' legal limits.** Use a **100 V-rated VESC**
> (a 75 V VESC like the Flipsky 75100 will be destroyed by 84 V — see
> [`POWER_MANAGEMENT_84V_UPGRADE.md`](POWER_MANAGEMENT_84V_UPGRADE.md)). All power-path parts ≥100 V.

---

## 1. System overview

```
        ┌──────────────────────┐
        │  20S Li-ion pack      │  (84.0 V full / 72 V nom; ≤20 cells)
        │  B+            B-     │
        └───┬─────────────┬─────┘
            │ balance     │ (cell taps → Daly B0..B20)
            │ leads       │
        ┌───┴─────────────┴───────────────┐
        │  Daly Smart BMS 20S 100A         │  common-port (P- = charge & discharge)
        │  100A cont / 150A peak           │
        │  P+ (=B+)            P-          │  UART/CAN/BT (optional → VESC)
        └───┬──────────────────┬───────────┘
            │ XT90             │ XT90
            ▼                  ▼
        ┌────────────────────────────────────────────────────┐
        │                100 V-class VESC                     │
        │  XT90 IN  ─ B+/B-                                    │
        │  MT60 OUT ─ Phase U/V/W ───────────────► Motor (MT60)│
        │  PH 6-pin ─ Hall H1/H2/H3 +5V/GND ─────► Motor sensor│
        │  COMM hdr ─ 5V / GND / TX / RX ──┐                   │
        │  SERVO/PPM (GPIOB5, 3.3V) ──► MOSFET driver ──► LIGHT │
        │  USB ─ (config only)             │                   │
        └──────────────────────────────────┼───────────────────┘
                                            │ original 4-pin dashboard cable
                                            ▼
                                ┌──────────────────────┐
                                │  Stock G30 Dashboard │  (throttle, brake, button, display, BLE)
                                │  STM32 + nRF51       │
                                └──────────────────────┘
```

- **Battery → Daly:** cells to the Daly's main B+/B− and the **balance lead** to the cell-tap header.
- **Daly → VESC:** protected output (`P+`/`P−`) to the VESC power input via **XT90**.
- **VESC → motor:** 3 phases via **MT60**; Hall sensor via the VESC **PH 6-pin**.
- **VESC ↔ dashboard:** the **original dashboard cable** only (5 V, GND, half-duplex data, button).
- **VESC → light:** the **PPM/servo pin** drives a **MOSFET driver** that switches the headlight/back-light.

---

## 2. Bill of materials

| # | Part | Spec | Why |
|---|------|------|-----|
| 1 | **VESC** (100 V class) | Flipsky/Makerbase **VESC 100/200** or TRAMPA **100/250**; FW **6.x+**, Lisp | 84 V needs ≥100 V FETs |
| 2 | **Daly Smart BMS 20S 100A** | Li-ion, **common port**, 100 A cont / 150 A peak, balance, UART/CAN/BT; ~166×65×24 mm | battery protection + balance |
| 3 | 20S Li-ion pack | ≤84.0 V; xP for your range/current | energy |
| 4 | **MOSFET driver / trigger module** | logic-level (3.3 V) input, N-ch low-side, ≥light voltage & current, PWM-capable | switches the light from PPM |
| 5 | Light (head + back) | 12 V or battery-voltage LED | illumination |
| 6 | Buck (light/12 V) *(if 12 V light)* | 100 V→12 V, ≥light current | powers a 12 V light |
| 7 | Buck (dashboard 5 V) *(optional)* | **MP9486** 100 V→5 V 2 A | only if not using VESC 5 V (see §5) |
| 8 | **XT90** connectors | 90 A | battery↔BMS↔VESC power |
| 9 | **MT60** connector | 3-phase bullet | motor phases |
| 10 | JST-**PH 2.0 6-pin** | matches VESC sensor port | Hall sensor |
| 11 | ANL fuse 100 A + holder | bolt-down | main-power protection |
| 12 | 8 AWG silicone wire (red/blk) | ≥80 A | main power |
| 13 | Heat-shrink, crimps, lugs | — | terminations |

Connectors reuse the **VESC's existing plugs** (XT90 power leads, sensor PH-6 header, COMM header, servo header).

---

## 3. Pin-by-pin wiring

### 3.1 Battery → Daly BMS (common port)
| From (pack) | To (Daly) | Notes |
|-------------|-----------|-------|
| Pack **B−** | Daly **B−** (main, thick) | main negative |
| Pack **B+** | Daly **B+ / P+** (thick) | common-port: B+ = P+ |
| Cell taps 0..20 | Daly **balance connector** (B0=most-negative … B20=most-positive) | **connect in order, B0 first**; verify each step voltage with a meter |
| Daly **P−** | → system negative (to VESC via XT90) | switched/protected negative |

> Common-port Daly: **charge and discharge share `P−`**. The charger plugs onto the same `B+`/`P−`.

### 3.2 Daly BMS → VESC power (XT90)
| Daly | Wire | VESC |
|------|------|------|
| **B+ / P+** | 8 AWG red → **ANL 100 A fuse** → | VESC **B+ / VIN+** (XT90 male/female pair) |
| **P−** | 8 AWG black | VESC **B− / VIN−** |

> Put the **ANL 100 A fuse** in the positive line between Daly P+ and the VESC. Use **XT90** at the VESC input (the VESC's existing XT90 leads).

### 3.3 VESC → Motor phases (MT60)
| VESC phase | MT60 pin | Motor |
|------------|----------|-------|
| U (A) | 1 | phase 1 |
| V (B) | 2 | phase 2 |
| W (C) | 3 | phase 3 |

> Phase order sets direction; if it spins backwards, swap any two **and re-run** FOC detection. Don't rewire after detection without re-detecting.

### 3.4 VESC → Motor Hall sensor (PH 6-pin)
| VESC PH-6 pin | Signal | Motor Hall wire |
|---------------|--------|-----------------|
| 1 | **+5 V** | Red |
| 2 | **H1** | Hall A |
| 3 | **H2** | Hall B |
| 4 | **H3** | Hall C |
| 5 | **TEMP** | (G30 motor: leave NC) |
| 6 | **GND** | Black |

> The exact PH-6 pin order varies per VESC model — **verify against your VESC's silkscreen/datasheet**. If detection reports a bad hall sequence, swap H1/H2/H3 and re-detect.

### 3.5 VESC ↔ Dashboard — **original cable only** (4-pin, half-duplex)
The stock G30 dashboard cable carries power + a half-duplex Ninebot data line + the button. Wire colors **vary by batch — verify with a multimeter** (continuity to the dashboard 5 V rail, GND, and the data/button pins).

| Dashboard cable | Function | VESC COMM header | Notes |
|-----------------|----------|------------------|-------|
| +5 V | dashboard power | **5 V** | from VESC 5 V (see §5) |
| GND | ground | **GND** | common reference |
| **Data** (half-duplex) | Ninebot bus | **UART TX** | `(uart-start 115200 'half-duplex)` — TX is the bidirectional line |
| **Button** | power button | **UART RX** (GPIO, pull-up) | `(gpio-configure 'pin-rx 'pin-mode-in-pu)`; button **grounds** the line when pressed |

> Community-confirmed mapping (tonymillion/VescNinebotDash, m365fw): the **data** wire → VESC **TX**, the **button** wire → an input with **pull-up** (pressing pulls it low). The `g30_dash.lisp` in this repo already configures exactly this. Do **not** add any other wires to the dashboard.

### 3.6 VESC → Backlight/headlight — **PPM/servo + MOSFET driver**
The VESC ESC platform has **one PWM channel on the servo/PPM pin (GPIOB5)**. The Lisp drives it from the
`light` state; a **MOSFET driver/trigger module** does the actual high-current switching.

| VESC | Wire | MOSFET driver module | Then |
|------|------|----------------------|------|
| **SERVO/PPM signal** (3.3 V) | signal | **trigger IN** | module gate-drives its N-MOSFET |
| **GND** | ground | module **GND** | common reference (share VESC/light ground) |
| — | — | module **V+ / load+** ← 12 V (buck) or battery | light supply |
| — | — | module **load−/OUT** → **Light −** | switched low side |

```
VESC servo/PPM (GPIOB5, 3.3V) ──► [MOSFET driver module] ──► LIGHT (12V or batt)
                                   IN   GND   V+    OUT
                                    │    │     │     │
                              from VESC  GND  +12V  Light−
```

- **Light voltage:** if a **12 V** LED, add a 100 V→12 V buck (item 6) and use a logic-level MOSFET
  (e.g. IRLZ44N, 55 V — fine for 12 V). If the light runs at **battery voltage (84 V)**, the MOSFET
  must be **≥100 V** (e.g. IRFP4110) — a 12 V light is simpler and recommended.
- Gate handling (if building discrete instead of a module): PPM→**100 Ω** series to gate, **10 kΩ**
  gate→GND pulldown (default-off).
- Enable the servo output in **VESC Tool → App Settings → General → Enable Servo Output**, and set the
  PPM app **Control Type = Off** (so the motor isn't driven by PPM). The Lisp owns the servo via `set-servo`.

---

## 4. Power button behaviour (software, via the dashboard cable)

Because only the **VESC ↔ dashboard** original cable exists (no dashboard→BMS wire), the button is handled
**in software** by `g30_dash.lisp` (the VESC stays powered):

| Action | Effect (from the Lisp) |
|--------|------------------------|
| **Single press** (when off) | turn scooter **on** (re-enable motor output) |
| **Single press** (when on) | **toggle the light** (drives the PPM output, §3.6) |
| **Double press** | cycle speed mode Sport→Eco→Drive |
| **Double press + brake held** | toggle **lock** |
| **Long press (~6 s)** | **off** — motor disabled, light off, low-power state |

**True power-off / standby:** with only these three units and no dashboard→BMS link, "off" is a software
low-power state (VESC idles at ~2.5–4 W). To fully cut power use **one of**:
- the **Daly BMS** Bluetooth app / its own button to disable discharge, or
- a **manual XT90 disconnect** or key switch on the main line, or
- *(optional, not via the dashboard)* a **VESC↔Daly UART/CAN** link so the VESC commands the Daly
  discharge-FET off on long-press — see §6.

---

## 5. Dashboard 5 V supply

Power the dashboard from the **VESC's COMM 5 V** pin (the VESC's onboard 5 V regulator is fed from the HV
rail and is rated for it; the dashboard draws <0.5 W). **No extra buck needed** in the basic build.

Use the optional **MP9486 100 V→5 V** buck (item 7) **only if** you want the dashboard powered even when
the VESC is off (e.g. for a future always-on feature) — tap it from raw **B+** before the BMS FET. The
basic design keeps the VESC always powered, so the VESC 5 V is sufficient.

---

## 6. Optional: VESC ↔ Daly link (accurate SoC + hardware off)

Not required (the dashboard battery % comes from the VESC's voltage estimate, `(get-batt)`), but if you
want true cell-level SoC and a hardware power-cut on long-press:

| Daly | VESC |
|------|------|
| UART **TX** | VESC second UART **RX** (or CAN H/L if using CAN) |
| UART **RX** | VESC second UART **TX** |
| GND | GND |

Daly UART: **9600 8N1**, `0xA5` framed; discharge-FET control is **command `0xD9`**. A Lisp extension
could read SoC and, on long-press, send `0xD9` to open the discharge FET (full 0 W standby). This adds a
cable **between VESC and Daly only** (still nothing extra to the dashboard).

---

## 7. Lisp: backlight on the PPM output

`vesc-lisp/g30_dash.lisp` now drives the servo/PPM pin from the `light` state (see the added
`update-light` call). On/off maps to `set-servo 1.0 / 0.0`. If your MOSFET module wants a clean GPIO level
instead of a servo pulse, drive the PPM pin as GPIO instead (commented alternative in the script).

---

## 8. Wiring ToDo (do in this order; battery LAST)

- [ ] **Confirm VESC is 100 V-rated** (reject 75 V units for 84 V) and on **FW 6.x+**.
- [ ] Build/verify the 20S pack; **pre-balance cells** before first BMS connection.
- [ ] Wire the Daly **balance leads B0→B20 in order**; verify each tap voltage with a meter.
- [ ] Connect pack **B+/B−** to the Daly; confirm Daly P− output ≈ pack voltage (FET on).
- [ ] Crimp **XT90** on Daly **P+ (via 100 A ANL fuse)** and **P−**; matching XT90 on the VESC input.
- [ ] Crimp **MT60** on the 3 motor phases ↔ VESC phase outputs.
- [ ] Crimp the motor Hall lead to the VESC **PH 6-pin** (5 V/H1/H2/H3/GND; TEMP NC) — verify pin order.
- [ ] Connect the **original dashboard cable** to the VESC COMM header: 5 V, GND, **data→TX**,
      **button→RX (pull-up)**. **Add no other dashboard wires.**
- [ ] Wire **VESC servo/PPM → MOSFET driver IN**, driver **GND→GND**, driver **V+→12 V buck (or battery)**,
      driver **OUT→Light−**, **Light+→supply**.
- [ ] Multimeter checks (battery still disconnected): no B+↔B− short, no 5 V↔GND short, phases not shorted.
- [ ] **VESC Tool first (USB, no battery):** App Settings → ADC **Off**; enable **Servo Output**; PPM
      Control Type **Off**; set battery **Cells = 20**, cutoff start/end (e.g. 60 V/57 V), current limits.
- [ ] Connect battery via XT90. Run **FOC detection** (wheel off the ground!). Fix phase/Hall order if needed.
- [ ] Upload `vesc-lisp/g30_dash.lisp` (Upload to Flash). Verify dashboard shows speed/battery/mode.
- [ ] Test: throttle, brake/regen, **single-press light toggle (PPM→driver→light)**, mode cycle, lock,
      long-press off. Start with **low current limits**, then raise while watching temps.
- [ ] Insulate every joint; secure wires; fit the ANL fuse; strain-relieve the XT90/MT60.

---

## 9. Safety

- 84 V is lethal; disconnect the battery for all wiring. Never short B+↔B−. Use the 100 A ANL fuse.
- Standard 42 V G30 charger **will not work** — use a **20S/84 V** CC-CV charger on the Daly charge port.
- Do **not** bypass Daly over/under-voltage, over-current, or over-temp protection.
- Re-tune VESC limits for the higher voltage; keep VESC cutoff **above** the Daly under-voltage trip.
- The VESC self-derates on heat; ensure good thermal contact in the enclosed deck.

---

## Sources (deep-researched)

- Daly Smart BMS 20S 72 V 100 A (100 A cont / 150 A peak, common port, ~166×65×24 mm, UART/CAN/BT):
  [Makerlab](https://www.makerlab-electronics.com/products/daly-smart-bms-20s-72v-100a-li-ion-24s-72v-100a-lfp-include-temperature-sensor-and-bt-module) ·
  [dalybms.com](https://www.dalybms.com/bms-72v-20s/) ·
  [robu.in](https://robu.in/product/daly-smart-bms-li-ion-20s-72v-100a-common-port-with-can-communication/)
- VESC servo/PPM output (one PWM channel on the servo pin / GPIOB5, `set-servo` 0–1, enable Servo Output):
  [VESC: Servo output control](https://vesc-project.com/node/3015) ·
  [VESC LispBM README](https://github.com/vedderb/bldc/blob/master/lispBM/README.md) ·
  [VESC: Breaking Light](https://vesc-project.com/node/1584)
- VESC ↔ Ninebot G30 dashboard half-duplex wiring (data→TX, button→input with pull-up, FW 6+):
  [tonymillion/VescNinebotDash](https://github.com/tonymillion/VescNinebotDash) ·
  [m365fw/vesc_m365_dash](https://github.com/m365fw/vesc_m365_dash) ·
  [Sharkboy-j/vesc_g30_dash](https://github.com/Sharkboy-j/vesc_g30_dash)
- Ninebot protocol framing/registers: [etransport/ninebot-docs](https://github.com/etransport/ninebot-docs/wiki) (see `docs/REGISTER_MAP.md`).
- High-voltage component selection: [`POWER_MANAGEMENT_84V_UPGRADE.md`](POWER_MANAGEMENT_84V_UPGRADE.md).
