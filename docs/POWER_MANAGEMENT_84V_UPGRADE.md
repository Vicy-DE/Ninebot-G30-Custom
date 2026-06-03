# Power Management — 84V (20S) / 80A Upgrade Analysis

## Overview

This document analyzes the upgrade of the Ninebot G30 Max custom build from a **10S (42V) / 30A** configuration to a **20S (84V) / 60–80A** configuration. It covers VESC controller compatibility, component selection, thermal constraints for enclosed mounting, VESC Lisp power-saving options, and a detailed consequences analysis.

**This document supersedes the 10S-specific sections of [POWER_MANAGEMENT_HARDWARE.md](POWER_MANAGEMENT_HARDWARE.md) and adds a VESC Lisp power-saving analysis not covered in [POWER_MANAGEMENT_SOFTWARE.md](POWER_MANAGEMENT_SOFTWARE.md).**

---

## CRITICAL WARNING — VESC 75/100 Voltage Incompatibility

```
╔══════════════════════════════════════════════════════════════════════╗
║  ⚠  THE FLIPSKY 75100 (VESC 75/100) CANNOT BE USED WITH 84V  ⚠   ║
║                                                                      ║
║  VESC 75/xxx absolute maximum voltage:  75V                          ║
║  VESC 75/xxx safe operating voltage:    14V – 67V (up to 16S)       ║
║  20S Li-ion fully charged voltage:      84.0V (4.20V × 20)          ║
║                                                                      ║
║  84V EXCEEDS the MOSFET ratings inside the VESC 75/100 by 9V.      ║
║  Connecting an 84V battery WILL destroy the controller and may      ║
║  cause a short circuit, fire, or explosion.                          ║
╚══════════════════════════════════════════════════════════════════════╝
```

### Source

The TRAMPA VESC 75/300 product page states:
> "14V – 67V safe, voltage spikes may not exceed 75V."

The Flipsky 75100, Flipsky 75200, and MakerBase 75200—all VESC 75/xxx derivatives—use 75V-rated MOSFETs internally and share this limitation.

### Impact

| Scenario | 20S Battery Voltage | VESC 75/100 Max | Outcome |
|----------|--------------------:|----------------:|---------|
| Full charge | 84.0V | 75V | **Instant MOSFET failure** |
| Regen braking at full charge | 84V + back-EMF spikes | 75V | **Catastrophic failure** |
| Nominal riding | ~72V | 67V safe | **Marginal / overstress** |
| Low battery | ~60V | 67V safe | OK (but brief) |

### Solution Options

| Option | Controller | Voltage Rating | Max Cells | Est. Price | Notes |
|--------|-----------|---------------|-----------|------------|-------|
| **A (Recommended)** | **VESC 100/250** (TRAMPA) | 14V – 100V | 22S Li-ion | ~€400 | Official VESC hardware, 100V FETs |
| **B** | **Flipsky VESC 100/200** | 14V – 100V | 22S Li-ion | ~$200 | Flipsky clone, 100V FETs |
| **C** | **MakerBase VESC 100/200** | 14V – 100V | 22S Li-ion | ~$150 | Budget option, verify MOSFET ratings |
| **D (Compromise)** | Keep Flipsky 75100 | 14V – 67V | 16S Li-ion | Already owned | Limit battery to **16S (67.2V max)** |

> **Recommendation:** Use a **100V-rated VESC** (Option A or B). If staying with the 75100, reduce the battery to **16S** (67.2V max, 58V nominal). This gives up 20% of the voltage range but avoids controller damage.

---

## Target Specifications Comparison

| Parameter | Previous (10S) | Upgraded (20S) | Change |
|-----------|---------------:|---------------:|--------|
| Cell configuration | 10S3P | 20S (xP TBD) | 2× series cells |
| Max voltage (full charge) | 42.0V | 84.0V | +100% |
| Nominal voltage | 36V | 72V | +100% |
| Cutoff voltage | 30V | 60V | +100% |
| Max continuous current | ~30A | 60–80A | +167–267% |
| Peak energy (e.g., 20S5P 21700) | 551 Wh | ~1,400 Wh | +154% |
| Required MOSFET VDS | ≥55V | **≥100V** | +82% |
| Required buck converter Vin | ≥50V | **≥100V** | +100% |
| Required VESC voltage rating | ≥55V | **≥100V** | +82% |

---

## Component Selection for 84V / 80A

All components must be rated above 84V with margin for voltage spikes (regen braking, inductive transients). A **minimum 100V** rating is required for all power-path components.

### 1. VESC Motor Controller

See the **CRITICAL WARNING** section above. A 100V-rated VESC is required.

| Part | Voltage | Current | RDS(on) (phase) | Package | AliExpress Search Term | Est. Price |
|------|---------|---------|-----------------|---------|----------------------|------------|
| **Flipsky VESC 100/200** | 100V | 200A peak | ~2 mΩ | Aluminum case | `Flipsky VESC 100 200` | $150–200 |
| **MakerBase VESC 100/200** | 100V | 200A peak | ~2 mΩ | Aluminum case | `MakerBase VESC 100 200` | $120–170 |
| **TRAMPA VESC 100/250** | 100V | 250A peak | <2 mΩ | Aluminum case | (trampa.com only) | ~€400 |

### 2. Daly Smart BMS (20S, 72V, 60A–100A)

The Daly Smart BMS 20S with UART is available in various current ratings. For 60–80A continuous motor draw plus regenerative braking headroom, choose the **80A or 100A** variant.

| Part | Config | Continuous | Peak | UART | Bluetooth | AliExpress Search Term | Est. Price |
|------|--------|-----------|------|------|-----------|----------------------|------------|
| Daly Smart BMS 20S 60A | 20S | 60A | 120A | Yes | Optional | `Daly BMS 20S 72V 60A UART` | $35–50 |
| **Daly Smart BMS 20S 80A** | 20S | 80A | 160A | Yes | Optional | `Daly BMS 20S 72V 80A UART` | $40–60 |
| Daly Smart BMS 20S 100A | 20S | 100A | 200A | Yes | Optional | `Daly BMS 20S 72V 100A UART` | $50–75 |

**Recommendation:** The **80A variant** matches the target current. Choose 100A for extra margin if the price difference is small.

Key Daly BMS specs (common across current ratings):

| Parameter | Value |
|-----------|-------|
| Protocol | 0xA5 start byte, 9600 baud 8N1 |
| Discharge FET control | Command 0xD9 (enable/disable) |
| Balance current | ~60 mA per cell (passive) |
| Operating temp | -20°C to +60°C |
| Dimensions (80A) | ~160 × 70 × 25 mm |
| Weight (80A) | ~250 g |

> The Daly BMS internal discharge MOSFETs are rated for 80A+ continuous and dissipate heat through the BMS PCB and casing. These FETs are specifically designed for this current level and are more practical than external MOSFETs for power switching at 80A in an enclosed space.

### 3. MOSFET Power Switch — External (Option A)

**For 84V / 80A in an enclosed space, external MOSFET switching is impractical.** See the thermal analysis below. This section is included for reference only—**Option B (Daly BMS FET control) is strongly recommended.**

| Part | VDS | ID | RDS(on) | I²R @ 80A | Package | AliExpress Search Term | Est. Price |
|------|-----|----|---------|-----------:|---------|----------------------|------------|
| **IRFP4110PBF** | 100V | 180A | 3.7 mΩ | 23.7W | TO-247 | `IRFP4110 MOSFET` | $2–4 |
| IRFP4568PBF | 150V | 171A | 5.9 mΩ | 37.8W | TO-247 | `IRFP4568 MOSFET` | $3–5 |
| IPP039N10N3 | 100V | 100A | 3.9 mΩ | 24.9W | TO-220 | `IPP039N10N MOSFET` | $2–3 |
| 2× IRFP4110 parallel | 100V | 360A | 1.85 mΩ | 11.8W | 2× TO-247 | — | $4–8 |
| 4× IRFP4110 parallel | 100V | 720A | 0.93 mΩ | 5.9W | 4× TO-247 | — | $8–16 |

> **Note:** At 84V, the IRFB7430 (40V, 1.3 mΩ) from the original design is **completely inadequate**—its 40V rating would be exceeded immediately. The IRLZ44N (55V) is also undersized.

### Why External MOSFETs Are Impractical at 80A Enclosed

| Config | RDS(on) | I²R @ 80A | Feasible Enclosed? |
|--------|---------|----------:|:------------------:|
| 1× IRFP4110 | 3.7 mΩ | 23.7W | **No** — requires large heatsink + airflow |
| 2× IRFP4110 | 1.85 mΩ | 11.8W | **Marginal** — needs 200 cm² heatsink |
| 4× IRFP4110 | 0.93 mΩ | 5.9W | Maybe — complex, expensive, uses space |

In an enclosed scooter deck with no airflow, you need to dissipate all heat conductively through the aluminum deck. Even 6W concentrated on a small PCB area will raise the junction temperature significantly. The Daly BMS's internal FETs are purpose-built for this load and already have thermal management designed in.

### 4. Always-On Buck Converter (84V → 5V)

The original design used an XL7015 module (5–80V input). At 84V battery, this **exceeds the XL7015's maximum input rating**. A 100V+ input buck converter is required.

| Part / Module | Input Range | Output | Current | Quiescent | AliExpress Search Term | Est. Price |
|---------------|-------------|--------|---------|-----------|----------------------|------------|
| **MP9486-based module** | 4.5V – 100V | 5V adj. | 2A | ~1 mA | `MP9486 buck converter module` | $2–4 |
| **XL7005A-based module** | 5V – 80V | 5V adj. | 0.4A | ~2 mA | N/A (80V max — **borderline**) | $1–2 |
| **TPS54560-based module** | 4.5V – 60V | Adj. | 5A | ~116 µA | N/A (**60V max — too low**) | $5–8 |
| Custom: HV regulator + LDO | 100V → 12V → 5V | 5V | 0.5A | Varies | `100V to 12V buck module` + `LM7805` | $3–5 |
| **Mini buck 100V module** | 9V – 100V | 5V fixed | 3A | ~1 mA | `100V 5V buck converter mini` | $3–5 |

**Recommendation:** Use an **MP9486-based module** (100V rated, 2A output, small form factor). Available on AliExpress as a complete module for ~$3. If unavailable, a 2-stage approach (100V→12V buck + 12V→5V LDO) works but wastes more power.

Alternative search terms on AliExpress:
- `"100V to 5V step down module"`
- `"80V 100V buck converter 5V"`
- `"high voltage DC DC converter 5V mini"`
- `"MP9486 module"`

### 5. Wiring and Connectors

| Item | Rating Required | Spec | AliExpress Search Term | Est. Price |
|------|----------------|------|----------------------|------------|
| Main power wire | 80A continuous | **8 AWG silicone** (8.37 mm², ~73A at 60°C) | `8 AWG silicone wire` | $3–5/m |
| Alternative main wire | 80A+ | **6 AWG silicone** (13.3 mm², ~101A) | `6 AWG silicone wire` | $5–8/m |
| Always-on wire (5V supply) | 500 mA | 24 AWG silicone | `24 AWG silicone wire` | $1/m |
| Main connector | 80A | **XT90** (90A rated) | `XT90 connector` | $2–4/pair |
| Alternative connector | 100A+ | **AS150** or **QS8** (150A rated) | `AS150 anti spark connector` | $5–8/pair |
| Fuse (always-on) | 1A fast-blow | 5×20mm glass or PTC resettable | `5x20mm fuse 1A` | $0.50 |
| Fuse (main power) | 80–100A | **ANL fuse 100A** or automotive blade | `ANL fuse 100A` | $2–3 |

### 6. Bill of Materials — Complete 84V System

#### Option B: Daly BMS FET Control (Recommended)

| # | Component | Specification | Qty | Purpose | Est. Price |
|---|-----------|--------------|-----|---------|------------|
| 1 | VESC 100/200 (Flipsky) | 100V, 200A peak | 1 | Motor controller | $150–200 |
| 2 | Daly Smart BMS 20S 80A | 72V, 80A cont., UART | 1 | Battery management + power switch | $40–60 |
| 3 | 20S Li-ion battery pack | 84V max, xP config | 1 | Energy storage | (varies) |
| 4 | MP9486 buck module | 100V → 5V, 2A | 1 | Always-on 5V for dashboard | $3–5 |
| 5 | 8 AWG silicone wire | 80A rated, red + black | 1m each | Main power wiring | $6–10 |
| 6 | 24 AWG silicone wire | For always-on 5V | 1m | Buck converter wiring | $1 |
| 7 | XT90 connectors | 90A rated | 2 pairs | VESC power + battery | $4–8 |
| 8 | 1A fuse | Glass or PTC | 1 | Always-on wire protection | $0.50 |
| 9 | ANL fuse 100A | Bolt-down | 1 | Main power protection | $2–3 |
| 10 | Heat shrink | Assorted | — | Insulation | $1 |
| | | | | **Total (excl. battery)** | **~$210–290** |

#### Option A: External MOSFET Switch (Not Recommended for Enclosed 80A)

| # | Component | Specification | Qty | Purpose | Est. Price |
|---|-----------|--------------|-----|---------|------------|
| 1 | VESC 100/200 (Flipsky) | 100V, 200A peak | 1 | Motor controller | $150–200 |
| 2 | Daly Smart BMS 20S 80A | 72V, 80A cont., UART | 1 | Battery management | $40–60 |
| 3 | 20S Li-ion battery pack | 84V max, xP config | 1 | Energy storage | (varies) |
| 4 | IRFP4110PBF ×4 | 100V, 180A, 3.7 mΩ | 4 | Parallel MOSFET switch | $8–16 |
| 5 | Gate resistor 10 kΩ | 0805 | 1 | Limit gate inrush | $0.01 |
| 6 | Pull-down resistor 100 kΩ | 0805 | 1 | Default-off | $0.01 |
| 7 | Gate driver (optional) | IR2117 or similar | 1 | Drive 4 MOSFETs reliably | $1–2 |
| 8 | MP9486 buck module | 100V → 5V, 2A | 1 | Always-on 5V for dashboard | $3–5 |
| 9 | 8 AWG silicone wire | 80A rated | 2m | Power wiring | $6–10 |
| 10 | XT90 connectors | 90A rated | 2 pairs | Connectors | $4–8 |
| 11 | Heatsink (aluminum) | ~200 cm², bolt to deck | 1 | MOSFET cooling | $3–5 |
| 12 | Fuses (1A + 100A) | See above | 2 | Protection | $3 |
| | | | | **Total (excl. battery)** | **~$220–320** |

---

## Thermal Analysis — Enclosed Mounting

All electronics are housed inside the scooter deck with **no forced airflow**. Heat must dissipate conductively through the aluminum deck structure and radiate to still air.

### Heat Sources

| Component | Idle | 30A Cruise | 80A Full Load |
|-----------|-----:|----------:|--------------:|
| VESC 100/200 (internal losses) | 2–4W | ~15W | ~50–80W |
| External MOSFET switch (4× IRFP4110) | 0W | 1.7W | 5.9W |
| Daly BMS internal FETs (if using Option B) | 0W | ~1W | ~4W |
| Buck converter (5V supply) | 0.08W | 0.08W | 0.08W |
| Dashboard (STM32 + nRF51 + display) | 0.35W | 0.35W | 0.35W |
| **Total (Option B)** | **~3W** | **~17W** | **~85W** |
| **Total (Option A)** | **~3W** | **~18W** | **~91W** |

### Thermal Derating

In an enclosed space with no airflow, a rough thermal resistance of **5–10 °C/W** from component to ambient is typical for TO-247 on a small heatsink with natural convection. At maximum load:

| Scenario | Power | ΔT (@ 10°C/W) | Ambient 35°C → Junction |
|----------|------:|---------------:|------------------------:|
| VESC at 80A | ~80W | 800°C | **Unrealistic — VESC has its own thermal management** |
| 4× IRFP4110 at 80A | 5.9W | 59°C | 94°C (approaching limit) |
| Daly BMS FETs at 80A | ~4W | Managed internally | Within BMS spec |

> The VESC handles its own thermal management via its aluminum casing and internal temperature sensors. It will derate (reduce current) automatically when FET temperature exceeds ~80°C. The critical question is external MOSFET heat—at 80A, even 4 parallel MOSFETs generate 6W, which in an enclosed space pushes junction temperatures to the edge.

### Thermal Conclusion

**Option B (Daly BMS FET control) is the only practical choice for enclosed 80A operation.** The BMS internal MOSFETs are:
- Purpose-designed for the rated current
- Thermally integrated into the BMS PCB
- Already inside the battery compartment (separate from the deck)
- Managed by the BMS's own over-temperature protection

External MOSFET switching requires heatsinking, careful PCB layout, and ideally some airflow—none of which are available in a sealed scooter deck.

---

## VESC Power Saving via Lisp

### Available Sleep Modes on VESC ESC Hardware

| Lisp Command | Available on ESC? | Effect | Power Saved |
|-------------|:-----------------:|--------|-------------|
| `(sleep-deep N)` | **No** — Express only (ESP32) | Deep sleep, wake after N seconds | N/A |
| `(sleep-light N)` | **No** — Express only (ESP32) | Light sleep, wake after N seconds | N/A |
| `(app-disable-output -1)` | **Yes** | Disables PWM output to motor | ~0.5–1W |
| `(set-current 0)` | **Yes** | Zero motor current command | Minimal |
| `(set-kill-sw 1 0)` | **Yes** | Kill switch — disable motor | ~0.5–1W |
| `(conf-set 'foc-short-ls-on-zero-duty 0)` | **Yes** | Don't short low-side FETs at zero duty | ~0.1–0.3W |
| `(event-shutdown)` | **Only with HW switch** | Triggers shutdown event | N/A on most ESCs |
| `(shutdown-hold N)` | **Only with HW switch** | Delays hardware shutdown | N/A on most ESCs |

### Key Finding: No Native VESC ESC Sleep Mode

The `sleep-deep` and `sleep-light` commands are **exclusive to the VESC Express** (ESP32-based auxiliary processor). Standard VESC ESC hardware—including the Flipsky 75100, VESC 100/250, and all STM32-based VESCs—**has no low-power sleep mode accessible via Lisp or any other interface.**

When the VESC is powered, its MCU runs at full speed regardless of motor state. Typical idle consumption:

| VESC State | Approximate Power | Notes |
|-----------|------------------:|-------|
| Idle, output enabled | 3–4W | PWM switching even with no motor current |
| Idle, output disabled (`app-disable-output`) | 2–3W | PWM stopped, MCU + peripherals still running |
| MCU core + gate drivers (irreducible) | ~1.5–2W | Cannot be reduced via software |

**Maximum software-achievable saving: ~1–1.5W** (from 3–4W idle to ~2–2.5W with output disabled and reduced switching).

### Lisp Implementation for Power Saving

The existing `g30_dash.lisp` already uses `(app-disable-output -1)` in the `handle-features` function when `off=1` or `lock=1`. The following additions maximize software power reduction:

```lisp
; ---------------------------------------------------------------------------
; Power saving when scooter is "off" (VESC still powered)
; ---------------------------------------------------------------------------
; This reduces VESC idle power by ~1W but does NOT replace hardware switching.
; For true power cutoff, the dashboard must control the BMS discharge FET.

(defun enter-low-power ()
    {
        ; 1. Disable motor output (already in handle-features)
        (app-adc-override 0 0)
        (app-adc-override 1 0)
        (app-disable-output -1)
        (set-current 0)

        ; 2. Disable low-side shorting at zero duty (reduces gate driver power)
        (conf-set 'foc-short-ls-on-zero-duty 0)

        ; 3. Kill switch as belt-and-suspenders safety
        (set-kill-sw 1 0)    ; disable motor, no braking
    }
)

(defun exit-low-power ()
    {
        ; Restore normal operation
        (conf-set 'foc-short-ls-on-zero-duty 1)
        (set-kill-sw 0 0)    ; re-enable motor
        (app-disable-output 0)
    }
)
```

### Integration with handle-holding-button

The `handle-holding-button` function in `g30_dash.lisp` (line ~308) is the natural integration point. When the user long-presses the power button:

1. **VESC Lisp** sets `off=1`, calls `enter-low-power` → VESC drops from ~3.5W to ~2.5W
2. **Dashboard STM32** detects the off state, sends Daly BMS command to disable discharge FET → VESC power is cut entirely → 0W
3. **Dashboard STM32** enters STOP mode → ~84 µW total system draw

The Lisp power saving provides a **brief transition window** (~100–500 ms) between the user pressing "off" and the hardware power cut, ensuring the motor is safely disabled before the VESC loses power.

### Modified handle-holding-button Concept

```lisp
(defun handle-holding-button ()
    {
        (if (= (+ lock off) 0)
            {
                (set 'off 1)
                (set 'light 0)
                (set 'feedback 1)
                (enter-low-power)     ; <-- Added: reduce power before dashboard cuts VESC
                (apply-mode)
            }
        )
    }
)
```

### Power Saving Summary

| Layer | Action | Power Before | Power After | Saving |
|-------|--------|------------:|------------:|-------:|
| **Lisp** (software) | `app-disable-output` + `foc-short-ls-on-zero-duty 0` | 3.5W | 2.5W | ~1W |
| **Dashboard** (hardware) | Daly BMS discharge FET off | 2.5W | 0W | 2.5W |
| **Dashboard** (sleep) | STM32 STOP mode + buck quiescent | 0.6W | 0.084W | 0.5W |
| **Total system (off)** | — | 4.1W | **0.084W** | **97.9%** |

> Software-only power saving (Lisp) is minimal (~1W). Hardware power switching (Daly BMS FET) is essential for meaningful standby life.

---

## Wiring Architecture — 84V / Option B

```
                    84V Battery Pack (20S)
                    ┌──────────────────────┐
                    │  20S Li-ion cells    │
                    │  84V max / 72V nom   │
                    └──┬────┬────┬────┬────┘
                       │    │    │    │
                      B+   B-   P+   P-
                       │    │    │    │
                 ┌─────┴────┴────┴────┴─────┐
                 │   Daly Smart BMS 20S 80A │
                 │   ┌──────────────────┐   │
                 │   │ Discharge FETs   │   │
                 │   │ (internal, 80A)  │   │
                 │   └──────────────────┘   │
                 │   UART: 9600 8N1         │
                 │   CMD 0xD9: DSG FET ctrl │
                 └──┬────┬────┬─────────────┘
                    │    │    │
                   P+   P-   UART (TX/RX)
                    │    │    │
     ┌──────────────┤    │    │
     │              │    │    │
     │    ┌─────────┘    │    │
     │    │              │    │
     │    │    ┌─────────┘    │
     │    │    │              │
     │    │    │      ┌───────┘
     │    │    │      │
  ┌──┴────┴──┐ │  ┌───┴──────────────────────┐
  │ MP9486   │ │  │  BLE Dashboard STM32     │
  │ 100V→5V  │ │  │  PA2/PA3: Daly UART      │
  │ Buck     │ │  │  PA11: unused (no MOSFET) │
  │ Module   │ │  │  PB12: power button       │
  │ (always  │ │  └──────────────────────────┘
  │  on from │ │
  │  B+)     │ │            ┌──────────────────────┐
  └──┬───────┘ │            │  VESC 100/200        │
     │         └────────────┤  (powered via P+/P-) │
     │ 5V                   │  UART: 115200        │
     │                      │  to BLE dashboard    │
     ▼                      └──────────────────────┘
  Dashboard
  3.3V LDO
```

### Key Wiring Points

1. **MP9486 buck module** connects to **B+** (before BMS discharge FET)—always powered from raw battery regardless of BMS state
2. **VESC** connects to **P+/P-** (after BMS discharge FET)—controlled by BMS on/off
3. **Dashboard PA2/PA3** connects to Daly BMS UART (9600 baud) for discharge FET control
4. **Dashboard PB6/PB7** connects to VESC UART (115200 baud) for Ninebot protocol
5. **Main power wires** (P+ to VESC): **8 AWG minimum** for 80A
6. **ANL fuse (100A)** in series between BMS P+ and VESC power input

---

## Consequences Analysis

### 1. Electrical Safety

| Risk | Severity | Mitigation |
|------|----------|------------|
| **VESC voltage damage (75V unit at 84V)** | **Critical — fire/explosion** | Use 100V-rated VESC only |
| MOSFET avalanche breakdown (wrong VDS) | Critical | All power-path components ≥100V rated |
| Battery short circuit (84V, hundreds of amps) | Critical | ANL fuse + BMS short-circuit protection |
| Regen overvoltage (84V + back-EMF) | High | BMS overvoltage cutoff + VESC OV limit |
| Wiring undersized for 80A | High | 8 AWG minimum, XT90 connectors |
| Always-on buck input overvoltage | Medium | Use 100V-rated module (MP9486), not XL7015 |

### 2. Thermal Consequences (Enclosed)

| Risk | Severity | Mitigation |
|------|----------|------------|
| External MOSFET overheating at 80A | High | **Use Option B** (no external MOSFET) |
| VESC thermal throttling in enclosed space | Medium | VESC has internal temp sensors, auto-derates |
| BMS overheating at sustained 80A | Low–Medium | BMS rated for this current, has OT protection |
| Buck converter overheating | Low | Only 100–200 mW load, negligible heat |

### 3. Reliability and Longevity

| Concern | Impact | Notes |
|---------|--------|-------|
| 20S cell imbalance | Higher with more series cells | Daly BMS passive balancing (60 mA) is slow; consider pre-balancing cells before assembly |
| BMS UART failure | VESC stays on, no auto-off | Add watchdog timeout on dashboard: if BMS unreachable for 60s, cut power via alternative method |
| Voltage headroom erosion | 100V FETs at 84V = 16V margin | Adequate for normal operation; regen spikes handled by VESC capacitors |
| Vibration and connector fatigue | Higher current = higher thermal cycling | Use quality XT90 connectors, secure all wires, apply conformal coating |

### 4. Functional Impact

| Change | Effect | Adaptation |
|--------|--------|------------|
| Higher voltage = more speed potential | VESC motor control must be re-tuned | Adjust ERPM limits, motor current limits, battery current limits in VESC Tool |
| Higher current = more torque | Acceleration and hill climbing improved | May need to reduce max current for safety and comfort |
| Heavier battery (20S vs 10S) | Scooter weight increases significantly | Weight distribution affects handling; mount battery low and centered |
| Larger battery physically | May not fit in stock battery compartment | Custom battery enclosure likely required |
| Battery charging | Requires 84V / 20S charger | Standard G30 charger (42V) will NOT work; need CC/CV charger matched to 20S |

### 5. Legal and Warranty

| Consideration | Status |
|---------------|--------|
| Manufacturer warranty | **Voided** — replacing ESC with VESC and modifying battery |
| Road legality | **Varies by jurisdiction** — 84V systems exceed legal limits in many EU/US regions |
| Insurance | Custom modifications may void insurance coverage for accidents |
| Electromagnetic compliance | Custom electronics have not been tested for EMC/FCC compliance |

### 6. Recovery from Failure

| Failure Mode | Recovery |
|-------------|----------|
| VESC destroyed by overvoltage | Replace with 100V-rated unit; cannot recover 75V VESC |
| BMS FET stuck closed | Battery stays on — add manual disconnect (e.g., anderson connector) |
| BMS FET stuck open | VESC has no power — manual BMS reset or bypass (dangerous) |
| Dashboard STM32 crash | Watchdog timer forces reboot; BMS stays in last FET state |
| All systems fail | Manual battery disconnect via physical connector |

---

## Migration Checklist — 10S → 20S

- [ ] **Verify VESC voltage rating ≥100V** before connecting battery
- [ ] Replace XL7015 buck module with MP9486 (100V input) or equivalent
- [ ] Replace main power wires with 8 AWG or thicker
- [ ] Replace connectors with XT90 (80A) or QS8 (100A+)
- [ ] Add ANL fuse (100A) in main power path
- [ ] If using Option A (MOSFET): replace all MOSFETs with 100V+ rated parts
- [ ] If using Option B (Daly BMS): wire PA2/PA3 to BMS UART, verify 0xD9 command
- [ ] Update VESC Tool settings: battery voltage limits, ERPM limits, current limits
- [ ] Update Lisp script: add `enter-low-power` / `exit-low-power` functions
- [ ] Acquire 84V / 20S charger (DO NOT use 42V charger)
- [ ] Test at low current first (e.g., limit VESC to 10A battery current)
- [ ] Gradually increase current limits while monitoring temperatures
- [ ] Verify BMS over-voltage, under-voltage, and over-current protections trigger correctly

---

## Summary of Recommendations

1. **Do NOT use the Flipsky 75100 with a 20S (84V) battery.** Use a 100V-rated VESC, or limit to 16S.
2. **Use Option B (Daly BMS FET control) for power switching.** External MOSFETs at 80A in an enclosed space are thermally impractical.
3. **Replace the XL7015 buck module** with an MP9486-based module rated for 100V input.
4. **VESC Lisp power saving is marginal** (~1W) but useful as a safe transition before hardware power cut. Implement `enter-low-power` / `exit-low-power` in `g30_dash.lisp`.
5. **Hardware power switching (BMS discharge FET) is essential** for meaningful standby life. Software alone cannot put the VESC into a low-power state.
6. **Wire gauge must increase** from 12 AWG to 8 AWG minimum for 80A current.
7. **Add proper fusing** (ANL 100A for main power, 1A for always-on supply).
8. **Re-tune all VESC parameters** in VESC Tool for the new voltage and current range.
