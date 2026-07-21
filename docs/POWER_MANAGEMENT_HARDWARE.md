# Power Management Hardware Concept — Dashboard-Controlled VESC Power

## Problem Statement

In the stock G30 Max, the ESC is the power master: it latches its own power on button press, provides 5V to the dashboard, and manages BMS communication. When you replace the stock ESC with a **VESC**, the VESC has no native power management — it draws **2–4W idle** (50–100mA @ 42V) continuously while connected to the battery, draining a full charge in about 5 days of standby.

**Goal:** The BLE dashboard controls VESC power on/off via the power button. The dashboard runs on permanent ultra-low-power standby (~0.6mW) from an always-on supply. Pressing power toggles the VESC on/off, saving ~4W of idle drain.

---

## Concept Overview

```
              ┌─────────────────────────────────────────────────────┐
              │                   Battery Pack                      │
              │                  10S3P  36V nom                     │
              │                  (30V – 42V)                        │
              │                                                     │
              │   ┌──────────────────┐                              │
              │   │   BMS Board      │                              │
              │   │   BQ76940 AFE    │                              │
              │   │                  │                              │
     Cell B+──┼──►│ DSG FET ─► B+ Out┼──────────► [Power Switch] ──►│ VESC B+
              │   │ CHG FET ─► CHG   │            (controlled by    │
              │   │                  │             dashboard)       │
     Cell B-──┼──►│ B- Out ──────────┼──────────────────────────────►│ VESC B-
              │   │                  │                              │
              │   │ UART (PA2/PA3)───┼──► optional Ninebot/Daly link│
              │   └──────────────────┘                              │
              │                                                     │
     Cell B+──┼──► [Always-On Buck] ──► 5V ──► BLE Dashboard (permanent)
              │    (before DSG FET)       │
              │    via fused thin wire    └──► STM32 + nRF51 + display
              │    (500mA fuse)                (sleep when off)
              │                                                     │
              └─────────────────────────────────────────────────────┘
```

---

## Two Architecture Options

### Option A: External MOSFET Power Switch (Recommended)

**Works with ANY BMS** — stock Ninebot, Daly, or third-party. No BMS firmware modification needed.

```
                     BLE Dashboard
                    ┌─────────────────────┐
                    │ STM32F103C8T6       │
                    │                     │
                    │  PA11 (Power Hold)──┼──► Gate Driver ──► MOSFET Switch
                    │  PA2  (free GPIO)──┼──► (alternative control pin)
                    │                     │
                    │  PB12 (Button) ◄────┼──── Power Button (active-low)
                    │                     │
                    └─────────────────────┘

    BMS B+ Output ───────────────► MOSFET Switch ──────────► VESC B+
                                      │
                                 Gate controlled by
                                 Dashboard PA11 / PA2
```

#### Power Switch Circuit — Low-Side N-Channel MOSFET

The simplest and most reliable approach — an N-channel MOSFET on the ground (B-) path:

```
                  BMS B+ ──────────────────────────────────── VESC B+
                                                              
                  BMS B- ──┬───────────────┬────────────────── Battery B-
                           │               │
                           │        ┌──────┘
                           │        │ Drain
                           │   ┌────┤ IRFZ44N / IRLZ44N
                           │   │    │ (N-ch MOSFET, 55V, 49A)
                           │   │    │ Gate
                           │   │    │◄──── 10kΩ ──── Dashboard PA11 (3.3V)
                           │   │    │       │
                           │   │    │       ├── 100kΩ to GND (pull-down, off by default)
                           │   │    │ Source
                           │   │    └───────┤
                           │   │            │
                           │   └────────────┴────────────────── VESC B-
                           │
                           └── Shared GND (Dashboard GND must = Battery B-)
```

| Component | Value | Purpose |
|-----------|-------|---------|
| Q1 | **IRLZ44N** (logic-level N-ch MOSFET) | Main power switch, 55V/41A, RDS(on) 22mΩ @ 3.3V Vgs |
| R1 | **10kΩ** (series gate resistor) | Limits gate charge current, prevents ringing |
| R2 | **100kΩ** (pull-down to GND) | Ensures MOSFET is OFF when dashboard is sleeping/reset |
| F1 | **500mA fuse** | Protects always-on 5V supply wire |

**Why IRLZ44N:** It is a logic-level MOSFET — fully enhanced at 3.3V Vgs (RDS(on) = 22mΩ). Standard MOSFETs (like IRFZ44N) need 10V Vgs and won't fully turn on from a 3.3V GPIO.

**Why low-side:** A high-side P-channel MOSFET rated for 42V/30A+ is expensive and harder to drive. The low-side N-channel approach is simpler but requires the dashboard GND to be connected to the same ground as the battery/MOSFET. Since the dashboard gets its always-on power from the same battery, ground is shared.

> **⚠️ CRITICAL:** The MOSFET's pull-down resistor R2 ensures the VESC stays OFF when the dashboard MCU is in reset, bootloader, or sleep mode. Without it, the gate floats and the MOSFET may turn on randomly.

#### Power Switch Circuit — High-Side P-Channel MOSFET (Alternative)

For cleaner current flow (B- is direct, switched on B+ side):

```
                  BMS B+ ──┬──────── Source ────┐
                           │              │     │
                           │         ┌────┤     │
                           │         │    │ IRF9540N (P-ch, -100V, -23A)
                           │         │    │ Gate
                           │         │    │◄─── NPN driver (BC547)
                           │         │    │     │ Base ◄── 10kΩ ── Dashboard PA11
                           │         │    │     │ Emitter ── GND
                           │         │    │     │ Collector ── Gate (via 10kΩ to Src)
                           │         │    │ Drain
                           │         │    └───┬──────────────── VESC B+
                           │         │        │
                           └─────────┘        │
                                              │
                  BMS B- ─────────────────────────────────── VESC B-
```

This is more complex (needs an NPN transistor as level-shifter to drive the P-MOSFET gate to battery voltage) but keeps the ground path unbroken. Choose this if you encounter ground loop issues with the low-side switch.

---

### Option B: BMS Discharge FET Control via UART

**Requires custom BMS firmware (stock Ninebot BMS) or Daly BMS (native UART FET control).**

The dashboard sends a UART command to the BMS to enable/disable the discharge MOSFET. No external power switch needed — the BMS itself switches the VESC power.

```
                     BLE Dashboard
                    ┌─────────────────────┐
                    │ STM32F103C8T6       │
                    │                     │
                    │  PA2 (UART TX) ─────┼──► BMS UART RX (Ninebot or Daly protocol)
                    │  PA3 (UART RX) ◄────┼──── BMS UART TX
                    │                     │
                    │  PB12 (Button) ◄────┼──── Power Button
                    │                     │
                    └─────────────────────┘

    Dashboard sends "enable discharge FET" → BMS enables DSG MOSFET → B+ flows → VESC powers on
    Dashboard sends "disable discharge FET" → BMS disables DSG MOSFET → B+ cut → VESC powers off
```

#### Option B1: Stock Ninebot BMS (Custom Firmware Required)

The stock BMS uses the BQ76940 AFE with the discharge FET controlled by `SYS_CTRL2` register (address `0x05`):

| SYS_CTRL2 Bit | Function | Control |
|----------------|----------|---------|
| Bit 1 | DSG_ON | 1 = discharge FET enabled |
| Bit 0 | CHG_ON | 1 = charge FET enabled |

To control from the dashboard, we need custom BMS firmware that:
1. Listens on USART2 (PA2/PA3) for a Ninebot protocol command
2. Responds to a new custom command (e.g., register `0x70`: power control)
3. Writes `SYS_CTRL2` via I2C to the BQ76940

**Pros:**
- No external hardware needed
- Uses existing wiring (BMS UART cable)
- BMS protections (OV, UV, OCD, SCD) remain fully active

**Cons:**
- Requires custom BMS firmware development (risky — battery safety)
- Stock BMS firmware is encrypted/obfuscated — starting from scratch is complex
- If BMS firmware crashes, battery protections may fail

> **⚠️ WARNING:** Modifying BMS firmware can cause battery fires. The BQ76940 hardware protections (SCD, OCD) are independent of firmware, but UV/OV recovery logic is firmware-dependent. Only experienced developers should attempt this.

#### Option B2: Daly BMS (Native UART Support — No Custom Firmware)

The **Daly Smart BMS** supports discharge FET control over UART natively:

| Command | Byte Sequence | Description |
|---------|---------------|-------------|
| Enable Discharge | `A5 40 D9 08 01 00 00 00 00 00 00 00 [checksum]` | Turn on discharge MOSFET |
| Disable Discharge | `A5 40 D9 08 00 00 00 00 00 00 00 00 [checksum]` | Turn off discharge MOSFET |
| Enable Charge | `A5 40 DA 08 01 00 00 00 00 00 00 00 [checksum]` | Turn on charge MOSFET |
| Read MOSFET state | `A5 40 93 08 00 00 00 00 00 00 00 00 [checksum]` | Query FET status |
| BMS Reset | `A5 40 00 08 00 00 00 00 00 00 00 00 [checksum]` | Reset BMS (MOSFETs re-enabled) |

**Daly UART Configuration:**

| Parameter | Value |
|-----------|-------|
| Baud rate | 9600 (some models use 115200) |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Logic level | 3.3V TTL |
| Checksum | Sum of all bytes, truncated to 1 byte |

**Daly BMS Sleep/Wake Behavior:**

| State | UART Available? | Wake Method |
|-------|-----------------|-------------|
| Active | Yes | — |
| Sleep (after timeout, no current) | **No** — UART is powered down | **Wake pin** (physical GPIO) or start charging/discharging |

> **IMPORTANT:** When a Daly BMS enters sleep mode (after a configurable timeout with no current flow), the UART interface is powered down. You **cannot** wake it via UART. You must use the physical **wake pin** on the Daly's light-board connector.

**Daly BMS Wake Pin Connection:**

```
Dashboard PA2 ──► Daly UART RX (for commands when awake)
Dashboard PA3 ◄── Daly UART TX (for responses when awake)
Dashboard PA11 ──► Daly Wake Pin (momentary pull to GND to wake from sleep)
```

**Pros:**
- No external power switch hardware
- No custom BMS firmware
- Daly BMS is widely available (10S 36V, 15A–100A models)
- Clean architecture

**Cons:**
- Must use a Daly BMS (not stock Ninebot)
- Wake from sleep requires a physical GPIO signal to the wake pin
- Dashboard firmware must implement the Daly UART protocol (different from Ninebot)
- BMS protection thresholds must be configured via Daly PC software first

---

## Always-On 5V Power Supply

Both options require an always-on 5V supply for the dashboard that is independent of the VESC power state.

### Recommended Module: MP1584EN Buck Converter

| Parameter | Value |
|-----------|-------|
| Input voltage | 4.5V – 28V (standard) or **LM2596HV** for 4.5–60V |
| Output voltage | 5V (adjustable, set with trimmer) |
| Output current | Up to 3A (we need ~100mA max) |
| Quiescent current | ~100µA (no load) |
| Efficiency | ~90% at light load |
| Size | ~22mm × 17mm |
| Cost | ~$1–2 |

> **NOTE:** The standard MP1584EN accepts max 28V input. The G30 battery is 30–42V. Use the **LM2596HV** (60V input) or a dedicated high-voltage buck like **XL7015** (80V input) or **Mini-360** (XL4015-based, 4.75–23V — NOT suitable for 42V).

### Recommended: XL7015 Buck Module

| Parameter | Value |
|-----------|-------|
| Input voltage | 5V – 80V |
| Output voltage | 5V (adjustable) |
| Output current | Up to 0.8A |
| Quiescent current | ~2mA (no load) |
| Efficiency | ~85% |
| Size | ~20mm × 11mm |
| Cost | ~$1 |

### Wiring — Always-On 5V Supply

```
INSIDE BATTERY COMPARTMENT:

Battery Cell Stack B+  ──┐
(before BMS discharge FET)│
                          │   ┌────────────────┐
                          ├──►│ XL7015 Module   │
                          │   │ IN+     OUT+ ───┼──► Dashboard +5V (Red wire up stem)
                          │   │          OUT- ──┼──► Dashboard GND
                          │   │ IN-  ────────────┼──► Battery B- / GND
                          │   └────────────────┘
                          │
                          │   500mA fuse (inline, before buck IN+)
                          │   for short circuit protection
                          │
BMS DSG FET Output (B+)──┼──► [Power Switch] ──► VESC B+
```

### Where to Tap Cell B+ (Before Discharge FET)

On the stock Ninebot BMS board:
- The BQ76940 `VC10` pin connects to the top cell (Cell 10 positive = pack B+)
- This voltage is present regardless of discharge FET state
- Tap from the VC10 connection point or the pack B+ wire **before** it enters the BMS FET circuit
- Route a thin wire (24 AWG is sufficient for <100mA) through the existing cable harness

On a Daly BMS:
- The B+ terminal on the BMS is **before** the discharge FET
- The P+ terminal is **after** the discharge FET (to the load)
- Connect the always-on buck converter to the **B+** terminal

```
Daly BMS Terminal Layout:
┌────────────────────────────────────┐
│   B-    B+    C-    P-    P+       │
│   │     │     │     │     │        │
│   │     │     │     │     └── Load + (after DSG FET) → VESC B+
│   │     │     │     └──────── Load - → VESC B-
│   │     │     └────────────── Charger -
│   │     └──────────────────── Pack + (before DSG FET) → Always-on buck IN+
│   └────────────────────────── Pack - → Always-on buck IN- / GND
└────────────────────────────────────┘
```

---

## Complete Wiring Diagram — Option A (MOSFET Switch)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ BATTERY PACK (10S3P, 36V nom / 42V max)                                     │
│                                                                              │
│  Cell B+ ──┬──► BMS ──► DSG FET ──► P+ ──► [MOSFET Switch] ──► VESC B+     │
│            │                                      ▲                          │
│            │                                      │ Gate = Dashboard PA11    │
│            │                                      │                          │
│            └──► [500mA Fuse] ──► [XL7015 Buck] ──► 5V Always-On             │
│                                      │                  │                    │
│                                      │                  ▼                    │
│  Cell B- ──┬──► BMS ──► P- ──────────┴── VESC B-       BLE Dashboard        │
│            │                                            (permanent 5V)       │
│            └──► Buck GND ─────────────── Dashboard GND                       │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────┐
│ DASHBOARD CONNECTIONS                                              │
│                                                                    │
│  Always-On 5V ───────────────────────────────► Dashboard +5V      │
│  GND ────────────────────────────────────────► Dashboard GND      │
│  Dashboard PA11 ──► 10kΩ ──► MOSFET Gate ──► VESC power switch   │
│  Dashboard PB6 (USART2_TX) ─────────────────► VESC COMM TX       │
│  Dashboard PB7 (USART2_RX) ◄────────────────── VESC COMM RX     │
│  Dashboard PB12 ◄─────────────────────────── Power Button         │
│                                                                    │
│  Dashboard PA2 ──► (Optional: BMS UART TX for Daly/monitoring)   │
│  Dashboard PA3 ◄── (Optional: BMS UART RX for Daly/monitoring)   │
└────────────────────────────────────────────────────────────────────┘
```

---

## Complete Wiring Diagram — Option B (Daly BMS FET Control)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ BATTERY PACK (10S3P, 36V nom / 42V max + Daly BMS)                          │
│                                                                              │
│  Cell B+ ──► Daly BMS B+ ──┬──► [DSG FET inside Daly] ──► P+ ──► VESC B+  │
│                             │          ▲                                      │
│                             │          │ Controlled by Daly UART cmd 0xD9    │
│                             │          │ (sent by dashboard via PA2)         │
│                             │                                                │
│                             └──► [500mA Fuse] ──► [XL7015 Buck] ──► 5V      │
│                                                         │                    │
│  Cell B- ──► Daly BMS P- ─────────── VESC B-           ▼                    │
│                                                   BLE Dashboard              │
│                                                   (permanent 5V)             │
│                                                                              │
│  Daly UART RX ◄────── Dashboard PA2 (UART TX, Daly protocol @ 9600)        │
│  Daly UART TX ──────► Dashboard PA3 (UART RX)                               │
│  Daly Wake Pin ◄───── Dashboard PA11 (GPIO, momentary pull to GND)          │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## Power Consumption Analysis

### Component Power Draw — Detailed

| Component | State | Current | Voltage | Power |
|-----------|-------|---------|---------|-------|
| **STM32F103C8T6** | Run @ 72MHz, all peripherals | 30–40 mA | 3.3V | 100–132 mW |
| **STM32F103C8T6** | STOP mode (RTC + EXTI wake) | 20 µA | 3.3V | 66 µW |
| **STM32F103C8T6** | Standby mode (only wake) | 2 µA | 3.3V | 6.6 µW |
| **nRF51822** | BLE advertising (1s interval) | 8–12 mA | 3.3V | 26–40 mW |
| **nRF51822** | System ON, no radio | 2.6 mA | 3.3V | 8.6 mW |
| **nRF51822** | System OFF | 0.4 µA | 3.3V | 1.3 µW |
| **LED display** | Active (all segments) | 30–50 mA | 3.3V | 100–165 mW |
| **LED display** | Off | 0 mA | — | 0 mW |
| **Indicator LEDs** | Active (3–4 LEDs) | 15–20 mA | 3.3V | 50–66 mW |
| **Indicator LEDs** | Off | 0 mA | — | 0 mW |
| **Dashboard LDO** | 5V→3.3V (quiescent) | ~1 mA | 5V | 5 mW |
| **XL7015 buck** | No load (quiescent) | ~2 mA | 42V | 84 mW |
| **XL7015 buck** | Light load (~10mA out) | ~5 mA | 42V | 210 mW |
| **VESC** | Idle (Lisp running, no motor) | 50–100 mA | 42V | 2.1–4.2 W |
| **VESC** | Off (power cut) | 0 mA | — | 0 mW |
| **MOSFET switch** | Conducting (30A motor load) | — | I²R loss | ~20W peak (22mΩ × 30A²) |
| **MOSFET switch** | Off (leakage) | ~1 µA | 42V | ~42 µW |

### System Power — Three States

#### State 1: Riding (VESC on, dashboard active)

| Component | Power |
|-----------|-------|
| Dashboard active (STM32 + nRF51 + display + LEDs) | ~350 mW |
| Dashboard LDO/buck overhead | ~250 mW |
| VESC idle + Lisp | ~3,000 mW |
| MOSFET I²R (at 20A average) | ~8,800 mW |
| **Total system (excluding motor)** | **~12.4 W** |

> MOSFET loss at 20A continuous is significant. For Option A, choose a MOSFET with lower RDS(on) (e.g., IRFB7430: 1mΩ, I²R = 400mW at 20A) or use two in parallel to halve resistance.

#### State 2: Standby — VESC ON, not riding (current system without power management)

| Component | Power |
|-----------|-------|
| Dashboard active | ~350 mW |
| Dashboard buck overhead | ~250 mW |
| VESC idle | ~3,000 mW |
| **Total standby** | **~3.6 W** |

#### State 3: Sleep — VESC OFF, dashboard sleeping (with power management)

| Component | Power |
|-----------|-------|
| STM32 STOP mode | 0.066 mW |
| nRF51 System OFF | 0.001 mW |
| Display/LEDs off | 0 mW |
| XL7015 quiescent | 84 mW |
| MOSFET off (leakage) | 0.042 mW |
| **Total sleep** | **~84 mW** |

> Most of the sleep power is the always-on buck converter quiescent current. A higher quality regulator (TPS54060: 116µA quiescent) would reduce this to ~5mW.

---

## Power Savings Summary

| Scenario | No Power Mgmt | With Power Mgmt | Savings |
|----------|---------------|-----------------|---------|
| **Idle power (VESC on vs off)** | 3.6 W | 0.084 W | **3.5 W (97.7%)** |
| **24h idle energy** | 86.4 Wh | 2.0 Wh | **84.4 Wh saved** |
| **Days until flat (551 Wh battery)** | **6.4 days** | **275 days** | **43× longer standby** |
| **Annual standby cost (electricity equiv)** | N/A (battery) | N/A | Prevents deep-discharge damage |

### With Higher Quality Buck (TPS54060 or similar, ~5mW quiescent)

| Scenario | Value |
|----------|-------|
| Total sleep power | ~5 mW |
| 24h idle energy | 0.12 Wh |
| Days until flat (551 Wh) | ~4,592 days (12.5 years) |

> At this level, battery self-discharge (~2–3% per month for Li-ion) dominates. The power management circuit is effectively invisible.

---

## Bill of Materials — Option A (MOSFET Switch)

| # | Component | Value / Part | Qty | Purpose | Est. Cost |
|---|-----------|-------------|-----|---------|-----------|
| 1 | N-ch MOSFET | **IRLZ44N** (logic-level, 55V, 41A, TO-220) | 1 | Main power switch | $1.50 |
| 2 | Gate resistor | 10kΩ 0805 | 1 | Limit gate current | $0.01 |
| 3 | Pull-down resistor | 100kΩ 0805 | 1 | Default-off when MCU sleeping | $0.01 |
| 4 | Buck converter module | XL7015 (5–80V → 5V) | 1 | Always-on 5V for dashboard | $1.00 |
| 5 | Fuse | 500mA axial (or PTC resettable) | 1 | Protect always-on wire | $0.20 |
| 6 | Wire | 24 AWG silicone (for always-on 5V) | 1m | Route from battery to buck | $0.50 |
| 7 | Wire | 12 AWG silicone (for MOSFET drain/source) | 0.5m | Power path | $1.00 |
| 8 | Heat shrink | Assorted | — | Insulation | $0.50 |
| 9 | Heatsink | TO-220 clip-on (optional) | 1 | MOSFET cooling at high current | $0.50 |
| | | | | **Total** | **~$5.22** |

### Upgrade: Use IRFB7430 for Lower Losses

| Part | RDS(on) | I²R @ 20A | I²R @ 30A |
|------|---------|-----------|-----------|
| IRLZ44N | 22 mΩ | 8.8W | 19.8W |
| **IRFB7430** | 1.3 mΩ | 0.52W | 1.17W |
| **IRFB3207** | 4.5 mΩ | 1.8W | 4.05W |

> For sustained high-current use, use IRFB7430 ($3) or two IRLZ44N in parallel (RDS = 11mΩ).

---

## Thermal Considerations

| Scenario | MOSFET | Current | Power | Notes |
|----------|--------|---------|-------|-------|
| Cruising 20 km/h | IRLZ44N | ~10A | 2.2W | OK without heatsink |
| Full throttle | IRLZ44N | ~25A | 13.8W | **Needs heatsink + airflow** |
| Full throttle | IRFB7430 | ~25A | 0.8W | OK without heatsink |
| Braking (regen) | Any | ~10A | Varies | Current flows through body diode |

> **Recommendation:** Use IRFB7430 or IRFB3207 for the power switch to avoid thermal issues. The IRLZ44N works for testing and low-current use but will overheat at sustained 25A+.

---

## Physical Mounting

```
ESC Compartment (under the deck):
┌────────────────────────────────────────┐
│                                        │
│  ┌──────────┐     ┌─────────┐         │
│  │ VESC     │     │ MOSFET  │         │
│  │ Motor    │     │ Switch  │         │
│  │ Ctrl     │     │ (TO-220)│         │
│  │          │     │ on HS   │         │
│  └──────────┘     └─────────┘         │
│                                        │
│  ┌──────────┐                          │
│  │ XL7015   │                          │
│  │ 5V Buck  │                          │
│  │ Module   │                          │
│  └──────────┘                          │
│                                        │
│  [to battery]  [to dashboard]          │
└────────────────────────────────────────┘
```

Both the MOSFET and the always-on buck converter fit in the ESC compartment alongside the VESC. Mount the MOSFET on a small heatsink attached to the aluminum deck for heat dissipation.

---

## Safety Checklist

- [ ] Always-on wire is fused (500mA) at the battery end
- [ ] MOSFET gate has pull-down resistor (default OFF)
- [ ] MOSFET is rated for at least 55V (10S max = 42V + regen spikes)
- [ ] No exposed high-voltage connections
- [ ] Dashboard GND is connected to battery GND (same reference)
- [ ] Test with multimeter before first power-on: no shorts B+ to B-
- [ ] VESC power input has reverse polarity protection (most VESCs have this built-in)
- [ ] BMS protections (OV, UV, OCD, SCD) are still active in the power path
- [ ] MOSFET body diode direction allows regenerative braking current
