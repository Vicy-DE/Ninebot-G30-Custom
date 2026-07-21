# Daly Smart BMS — Selection, Buy Links & G30 Max Compartment Fit

**Date:** 2026-06-09
**For:** the VESC custom build (stock ESC→VESC, stock BMS→Daly Smart BMS). Multi-source web research,
cross-verified, confidence flags inline. Pairs with
[`../../docs/WIRING_PLAN_DALY_VESC.md`](../../docs/WIRING_PLAN_DALY_VESC.md).

---

## TL;DR / Recommendation

- **The Daly board is NOT the fit problem — the 20S cell pack is.** The stock G30 Max pack is
  **10S6P, 365 × 118 × 73 mm, 4.5 kg, 60× 18650 cells** in a tight deck cavity. A Daly board at
  **166 × 65 × 24 mm** uses well under half the cavity and fits the height with room to spare. But
  going **20S doubles the series cell count** (→ ~120 cells at equal parallel), which does **not** drop
  into the stock deck at useful capacity. Community 72 V Max builds **chop/stack decks or use an
  external box** — there is no reported clean 20S drop-in.
- **Recommended realistic path → 13S / 48 V Daly.** 48 V/13S is the proven G30 upgrade voltage with
  off-the-shelf deck-fit packs (Monorim B3 13S4P **388 × 84 × 70 mm**, NordBot, MyMaxMods). The
  **Daly 13S 48 V 80–100 A Li-ion, common-port, UART + BT (24 × 65 × 166 mm, ~0.6 kg)** fits the cavity
  trivially and pairs with a 60–75 V-class VESC. Lowest risk.
- **The documented 20S / 84 V concept is electrically sound** (needs a 100 V VESC) **but requires deck
  modification** for a full-capacity pack. The Daly 20S board fits; the pack is the constraint.

> ⚠️ **Repo correction:** `CLAUDE.md`/`PROJECT_DOC.md` say the stock pack is **10S3P** — the genuine
> Segway cell part number **`10INR19/66-6`** (prefix 10 = series, suffix 6 = parallel) proves it is
> **10S6P** (60 cells). 15.3 Ah / 6P ≈ 2550 mAh/cell, consistent. Recommend updating the docs.

---

## 1. Daly Smart BMS — options compared

All **common-port** (charge+discharge share one port), **Li-ion**, **smart** (UART + Bluetooth dongle;
CAN/RS485 on "communication"/fan variants), passive balancing (~30–50 mA). Dimensions follow the
BATTERYINT convention **H × W × L (mm)** (the most explicit primary source; baked into their product
URLs and matching this repo's "166×65×24" note).

| Model (Li-ion, common port) | Dims H×W×L (mm) | Weight | Cont. current | Comms | Price (USD) | Buy / search |
|---|---|---|---|---|---|---|
| **10S 36 V 100 A** | ~14×65×~166 \* | ~0.5–0.6 kg | 100 A (50 A chg) | UART+BT | ~$50–70 | AliExpress "Daly smart BMS 10S 36V 100A common port bluetooth" |
| **13S 48 V 100 A** ★ | **24×65×166** | **0.6 kg** | 100 A (50 A chg) | UART+BT | $95.80 BATTERYINT / ~€25–55 AliExpress | **DALY Official Store**; "Daly smart BMS Li-ion 13S 48V 100A" |
| **16S 60 V 100 A (fan)** | **33×65×184** | **0.7 kg** | 100 A | UART+BT (LCD *or* BT) | $114.50 BATTERYINT | "Daly smart BMS 16S 60V 100A common port bluetooth" |
| **20S 72/84 V 100 A** | **24×65×166** | **0.9 kg** | 100 A (50 A chg) | UART+BT (CAN/485 var.) | $109.40 BATTERYINT / ~€27–55 AliExpress | **DALY Official Store**; "Daly smart BMS Li-ion 20S 72V 100A" |
| 20S 72 V **80 A** | similar board | ~0.8 kg | 80 A | UART+BT+temp | ~$90–100 | Electrotech-USA (genuine); same store |

★ recommended for a deck-fit build. \* **10S dims medium-confidence** — no primary L×W×H for the 10S
*smart* board; it shares the 65 mm width / ~166 mm length K-series footprint and is at the shorter/
thinner end. Verify the board photo before buying.

**Notes:**
- **One board footprint spans 10S–20S** at a given amperage (only more balance taps populated) — that's
  *why* series count doesn't change BMS size. **High confidence.** The **16S "fan" board is bigger
  (33 mm tall, 184 mm long)** due to the fan/heatsink — flag if it must lie flat.
- **Current reality check:** Daly "100 A" common-port boards are FET-limited and run hot near rating;
  G30-class community guidance caps useful current ~30–40 A. An **80 A** board is plenty and runs
  cooler. The **VESC does the heavy current work**; the Daly mainly protects/monitors (you can wire
  discharge to bypass the BMS FETs if desired). **Common-port matches the project requirement.**
- **Comms:** UART (TTL) for VESC/PC + Bluetooth dongle (Daly app). CAN/RS485 on variants — useful if you
  want the VESC to read the BMS over CAN.

### AliExpress sourcing (links rot — use store + search terms)
- **DALY Official Store on AliExpress** — active, 49.2K+ followers, full 4S–24S range. Real street price
  for a 13S/16S/20S **100 A smart common-port + BT** board ≈ **€25–55** (vs $95–115 at Western
  resellers). Stable landing/search pages:
  - https://www.aliexpress.com/w/wholesale-daly-smart-bms.html
  - https://www.aliexpress.com/w/wholesale-daly-bms.html
- Exact search strings:
  - `Daly smart BMS Li-ion 13S 48V 100A common port bluetooth UART`
  - `Daly smart BMS Li-ion 20S 72V 100A common port bluetooth`
  - `Daly smart BMS Li-ion 16S 60V 100A common port bluetooth`
- Spec-verification reseller (explicit dims/weight/price): **BATTERYINT** (`batteryint.com`) —
  13S `…13s…24-65-166`, 20S `…20s…24-65-166`, 16S `…16s…33-65-184`.

---

## 2. G30 Max battery compartment & stock-pack measurements

| Item | Value | Confidence / source |
|---|---|---|
| **Stock pack outer dims** | **365 × 118 × 73 mm** | **High** (more4motion + scootered agree) |
| **Stock pack weight** | **4.5 kg** | **High** (more4motion) |
| **Voltage** | 36 V nom / 42 V full | **High** (Segway) |
| **Capacity** | 15.3 Ah / **551 Wh** | **High** (Segway) |
| **Cell config** | **10S6P**, 60× 18650 | **High** — Segway cell `10INR19/66-6` (**overrides repo "10S3P"**) |
| **Cell brand** | LG / Samsung INR 18650 | Medium |
| **BMS location** | potted in grey foam at one **end** of the sealed (IP67) pack | Medium-high (SH forum) |
| **Deck cavity (internal)** | quoted "must fit within ~400 × 160 × 100 mm, tight tolerance" | Low-med (Alibaba buyer guide) — treat as upper bound |

### "Fits-the-deck" replacement packs (these define the real fit envelope)
| Pack | Config | V | Dims (mm) | Weight | Note |
|---|---|---|---|---|---|
| Stock G30 Max | 10S6P | 36 | **365×118×73** | 4.5 kg | baseline |
| Monorim B3 | **13S4P** | 48 | **388×84×70** | 4.0 kg | sold as deck-fit 48 V upgrade (longer, narrower) |
| NordBot / MyMaxMods | 13S | 48 | deck-fit | — | confirm 13S/48 V is the standard "fits stock deck" bump |

**Inferred free internal envelope:** length ~**385–390 mm** (Monorim 388 mm fits), width up to ~**118
mm**, height ~**70–73 mm**, holding 60 cells as a 10S6P brick.

---

## 3. Fit verdict

**Daly board (166×65×24 mm) in the stock deck — fits with huge margin.** The 20S **pack** is the
constraint:
- Deck length ≈ 385–390 mm → Daly 166 mm leaves ~220 mm for cells in line. **BMS length: fits.**
- Deck height ≈ 70–73 mm → Daly 24 mm fits flat with ~45 mm spare. **BMS height: fits.**
- Deck width ≈ ≤118 mm → Daly 65 mm fits. **BMS width: fits.**
- **The pack:** 20S at equal parallel needs ~120 cells → does **not** fit. In-deck 20S means **20S2P/3P**
  (much lower capacity, ~5–7.5 Ah) **or** enlarge the box (chopped/stacked deck / external enclosure).

**Therefore:**
1. **Stock-deck-friendly → 13S / 48 V Daly (recommended).** Proven upgrade voltage, deck-fit packs
   exist, board fits trivially; pair with a 60–75 V VESC.
2. **Modest stretch → 16S / 60 V** — electrically fine, but the 16S **fan board is bigger (33×65×184)**
   and 16S packs are tighter on parallels.
3. **20S / 84 V (documented high-performance concept) → deck mod required.** The Daly 20S board fits,
   but a full-capacity 20S pack needs a chopped/stacked deck or external box. Plan the mechanics before
   buying cells.

---

## Sources (primary first)

**Daly specs / dims / price**
- BATTERYINT 20S 72V 100A (dims "24 65 166", 0.9 kg, $109.40): https://batteryint.com/products/daly-smart-bms-li-ion-li-ion-20s-bt-li-ion-100a-bluetooth-24-65-166
- BATTERYINT 16S 60V 100A fan ("33 65 184", 0.7 kg): https://batteryint.com/products/daly-smart-bms-li-ion-li-ion-16s-bt-li-ion-100a-with-fan-bluetooth-33-65-184
- BATTERYINT 13S 48V 100A (0.6 kg): https://batteryint.com/products/daly-smart-bms-li-ion-li-ion-13s-bt-li-ion-100a-bluetooth-24-65-166
- DALY Official Store / search: https://www.aliexpress.com/w/wholesale-daly-smart-bms.html
- JAG35 10S 36V 100A common port: https://jag35.com/products/daly-bms-10s-36v-100a-common-port-waterproof
- Electrotech-USA genuine 20S 72V 80A BT common port: https://electrotech-usa.com/products/genuine-daly-smart-bms-li-ion-20s-72v-80a-bluetooth-temperature-sensor-common-port
- Daly official: https://www.dalybms.com/bms-13s-48v-100a/ · datasheets: https://www.gobelpower.com/daly-bms-data-sheets_f13.html

**G30 Max pack / compartment**
- Genuine Segway pack, cell `10INR19/66-6`, 551 Wh: https://partslinkent.com/ninebot-10inr19-66-6-36v-battery-pack-for-max-g30-551-wh-1/
- more4motion pack (365×118×73 mm, 4.5 kg): https://more4motion.com/products/ninebot-g30-max-electric-scooter-battery
- scootered specs (36.5×11.8×7.3 cm): https://www.scootered.co.uk/electric-scooter-specs/ninebot-segway-max-g30-electric-scooter-full-specification.html
- Monorim B3 48V 13S4P deck-fit (388×84×70 mm): https://monorim.store/products/b3-for-segway-ninebot-max-g30
- iFixit battery replacement: https://www.ifixit.com/Guide/Segway+Ninebot+Max+Battery+Replacement/142867
- 72V/external build refs: https://mymaxmods.com/product/maxpak/ · https://scootertalk.org/forum/viewtopic.php?t=6715
