# G30 Max Dashboard — Pinout & No-Solder Flashing Research

**Date:** 2026-06-09
**Scope:** Ninebot/Segway G30 / G30 Max / G30D / G30LP dashboard board (STM32F103C8T6 +
nRF51822). Multi-source web research, cross-checked against this repo's firmware RE
([`RE_FINDINGS.md`](../../firmware/decompiled/RE_FINDINGS.md),
[`VERIFICATION_REPORT.md`](../../Documentation/VERIFICATION_REPORT.md)).

**Confidence tags:** **[CONFIRMED]** primary teardown/firmware/official doc · **[STRONG]** multiple
consistent community sources · **[M365-INFERRED]** derived from M365, not G30-verified ·
**[ASSUMED]** reference-design / needs a physical check.

> **Headline:** the dashboard's external cable is **4 wires** (not the M365's 3), and the STM32
> dashboard MCU **can be reflashed with no soldering** via the stock serial-IAP path (USB-TTL + the
> Ninebot IAP tool) — see [`../../docs/DASHBOARD_NO_SOLDER_FLASH.md`](../../docs/DASHBOARD_NO_SOLDER_FLASH.md)
> for the step-by-step plan. The IAP opcodes used in this repo (0x07/0x08/0x09/0x0A) are **confirmed
> against the official Ninebot protocol PDF**.

---

## A. External dashboard control-cable pinout  **[CONFIRMED]**

**Connector:** round waterproof push-lock, **Higo/Julet family**. The **M365** uses a **HIGO-B4-B**;
the **G30/G30D uses a different, G30-specific keying** — community sources state the M365 HIGO-B4-B is
**not compatible with G30D**. ScooterHacking refers to these serial-tap plugs generically as "Julet".

**Conductors — 4 wires** (ScooterHacking "Full Tutorial" Part 4, verbatim: *"The dashboard has 4
wires: red, black, green, and yellow"*). The M365 has only **3** (5V/GND/single BUS, no dedicated
button wire); the G30 adds the **green** discrete power-button line.

| Wire | Function | Confidence | VESC-side mapping |
|------|----------|-----------|-------------------|
| **Red** | **+5 V** supply (from ESC/VESC) | **[CONFIRMED]** | VESC COMM **5 V** |
| **Black** | **GND** | **[CONFIRMED]** | VESC COMM **GND** |
| **Yellow** | **Half-duplex serial DATA** — Ninebot UART, **115200 8N1**, single-wire HDX | **[CONFIRMED]** | VESC UART **TX** (`uart-start 115200 'half-duplex`) |
| **Green** | **Power-button** — active-low; pressing **grounds** the line; needs a pull-up (≈1 kΩ to 3.3 V, or 470 Ω + cap) | **[CONFIRMED]** | VESC **RX as GPIO**, `pin-mode-in-pu` |

**Reference pin order** (ES2/ES4 dashboard, from PCB side; tonymillion/VescNinebotDash) —
**[STRONG, may differ on G30]**: `[red]=5V  [green]=button  [yellow]=uart  [black]=GND`.

> ⚠️ **Color ↔ connector-pin mapping is NOT consistent across replacement cables.** tonymillion: *"pin/
> colors were not wired to the same pins at the Julet connector"* — only GND reliably passes straight
> through. **Always ohm-out continuity from the dashboard solder pad to the wire end before trusting
> color.** Idle DATA-line level ≈ 2.7 V **[M365-INFERRED]**.

This matches the repo's verified protocol (115200 8N1, `5A A5`, `Σ^0xFFFF` checksum) and the
`vesc-lisp/g30_dash.lisp` wiring (data→TX, button→RX pull-up).

---

## B. SWD pads / test points per MCU

### B1. ~~STM32F103C8T6 — dashboard main MCU~~ ❌ **RETRACTED — the tag was wrong**

> **The `[CONFIRMED]` below was never confirmed.** Its entire basis is the inference in the next
> sentence: *"ST-Link + STM32 tooling → therefore STM32 pads."* An **ST-Link V2 is a generic SWD probe**
> and is exactly what the community uses to flash **nRF51** chips (ScooterHacking ReFlasher does this),
> so the inference is invalid. Binary analysis of the stock dumps shows the dashboard is
> **nRF51822-only** (it drives the display via a TM1637 and speaks `5A A5` itself) —
> see [`MCU_IDENTIFICATION.md`](MCU_IDENTIFICATION.md). **The SWD pads described below are the
> nRF51822's**, not an STM32's.

The documented dashboard-flash procedure (ScooterHacking "Full Tutorial" Part 1) uses an **ST-Link V2
+ STM32 ST-Link Utility** (STM32 tooling → ~~these are the STM32's pads~~ **invalid inference**):
- **3 pads labeled GND / SWCLK / SWDIO**, grouped together "on the right" of the board.
- A separate **+5 V pad**, exposed after **removing capacitor C2** on the front (ST-Link 5 V powers the
  board). Alternative: feed 5 V to the **red dash-plug wire** and GND to the third pad by the data lines.
- Pads are **under silicone potting** (scrape with a plastic tool) and **fragile** (can lift traces).
- Some revisions have a **black resistor/pull on SWDCLK** that can interfere with debug **[STRONG,
  revision-dependent]**.

> The exact MCU-pin attribution in [`PINOUT.md`](PINOUT.md) (PA13=SWDIO, PA14=SWCLK, USART2 PB6/PB7) is
> the standard STM32F103 mapping but **reference-derived/ASSUMED** — the *pads exist and are labeled*
> (confirmed); the *which-MCU-pin* is the assumption (consistent with `RE_FINDINGS.md`: no STM32 dump
> exists to verify against).

### B2. nRF51822 — BLE SoC — SWD  **[WEAK / ASSUMED]**
- Standard nRF51 SWD = **SWDIO + SWCLK + GND (+ VDD)**.
- **No primary G30 source documents the nRF51 SWD pad locations** — the community never needed them
  because the nRF51 is reflashed OTA (§C3). RE work confirms the **nRF51 has no readout protection**
  ("no read-out protection… firmware BLE110"), so it is dumpable/flashable via SWD **in principle**, but
  **the pad locations must be found by inspection + datasheet pinout.** ← key open gap.

### B3. Internal STM32 ↔ nRF51 UART  **[CONFIRMED it exists / pins ASSUMED]**
The nRF51 image references only **Nordic UART0** (`0x40002xxx`, 115200) and runs the same `5A A5`
parser → it is a pure BLE↔UART bridge to the STM32. The specific STM32 USART pins (repo lists USART2
PB6/PB7 to the ESC and USART1 PA9/PA10 to the nRF51) are **reference-derived**, not dump-verified.

### B4. Throttle / brake / button / display / LEDs
- **Power button** = the green cable wire (§A) **[CONFIRMED]**.
- **Throttle & brake:** on the **G30 these wire to the ESC/mainboard, NOT the dashboard** **[STRONG]**
  — a G30-vs-M365 difference (on M365 they route via the dash cable). ⚠️ This contradicts
  [`PINOUT.md`](PINOUT.md)'s "PA0 throttle / PA1 brake on the dashboard STM32" → flag as **unverified**;
  in the VESC build the **VESC reads throttle/brake on its own ADC** anyway (the lisp does
  `app-adc-override`), so `dash_bridge.buildThrottleFrame()` (0x65) is only used if a given dashboard
  revision *does* read them. Verify on your unit.
- **Display + status LEDs:** ~~driven by the dashboard STM32; internal pin connections undocumented~~
  → **SOLVED (2026-07-26): driven by the nRF51822 through a TM1637** on **P0.04 / P0.05** (bit-banged
  2-wire, 6 grids, `0x88|brightness`). Font table + `tm1637_update()` recovered from the stock image —
  no physical trace needed. See [`MCU_IDENTIFICATION.md`](MCU_IDENTIFICATION.md).

---

## C. No-solder flashing feasibility

### C1. STM32 dashboard MCU — **YES, via serial IAP (no ST-Link, no opening the dash)**  **[CONFIRMED]**
- **Ninebot IAP** (https://iap.scooterhacking.org/) flashes over **UART or BLE**, embeds stock
  firmware, and *"removes the need of an ST-Link in a vast majority of cases."*
- Use a **CP2102 USB-TTL @ 3.3 V, 115200** (an **ST-Link is NOT compatible with IAP**). Documented
  no-solder tap on the Max/G30 = the **ESC↔BLE 7-pin connector with test clips**:
  **Pin 1 = 5 V, Pin 2 = GND, Pin 6 = RX, Pin 7 = TX**.
- In IAP: Serial → Vehicle **Ninebot** → Interface **BLE (3E)** to address the dashboard → upload BLE
  firmware. **"ALWAYS FLASH THE BLE FIRST."**
- The dashboard cable's **yellow DATA** line carries the same half-duplex bus, so a no-solder tap **on
  the 4-pin dashboard cable should reach the BLE module too** **[STRONG inference]** (tap point not
  separately documented, but electrically identical).
- **Custom-firmware caveat:** stock IAP accepts only properly-formatted (Ninebot: `.enc`) images and
  enforces an unlock/"safe-mode" gate (error `0x04 = unlocked/updatable`). A **custom** app/bootloader
  must be packaged in the Ninebot IAP frame format **or** flashed via SWD.

### C2. IAP opcode reference — **[CONFIRMED, official Ninebot ES Communication Protocol PDF]**
`https://cloud.scooterhacking.org/release/nbdoc.pdf`

| Opcode | Name | Meaning | Data |
|--------|------|---------|------|
| **0x07** | CMD_IAP_BEGIN | start of FW download (with reply) | u32 firmware length |
| **0x08** | CMD_IAP_TRANS | data frame (with reply) | FW bytes, multiple-of-8 (128 typ.) |
| **0x09** | CMD_IAP_VERIFY | check frame (with reply) | u32 checksum |
| **0x0A** | CMD_MCU_RESET | chip reset (no reply) | — |
| 0x0B | CMD_IAP_ACK | response | — |

IAP error indices: `0x01` over-size, `0x02` erase-fail, `0x03` write-fail, **`0x04` unlocked/updatable**,
`0x05` index, `0x06` busy, `0x07` not-mult-of-8, `0x08` CRC-fail. Addresses: `0x20` ESC, **`0x21`
"Bluetooth instrument" (dashboard)**, `0x22` battery, `0x3D` PC/serial/IoT, **`0x3E` phone-over-BLE**.
Frame & checksum (`Σ^0xFFFF`) match this repo's RE exactly. **The repo's IAP opcodes are correct.**

**Flashing tools (dashboard/BLE):** Ninebot IAP (UART+BLE), ScooterHacking ReFlasher (ESC, ST-Link),
XiaoFlasher / ScooterHacking Utility (Android, BLE CFW), NinebotFlasher (older), "BLE555
Autoprogrammer" (`connect_dashboard.bat`, ST-Link, installs BLE555 CFW). ⚠️ BLE555 CFW can remove
features and **block IAP rollback** — keep stock 1.x.x BLE for reversibility.

### C3. nRF51822 BLE SoC
- **No readout protection [CONFIRMED]** → dumpable/flashable via SWD if you can reach its pads (§B2).
- **Stock OTA = Xiaomi MiIO-over-BLE, not the STM32 IAP path** (app @ `0x00018000` above the S110
  SoftDevice, `sd_flash_*` SVCs, MiIO auth/cloud-bind). The BLE *transport* is encrypted (NinebotCrypto:
  SHA-1 key-gen + AES-ECB + CRC) and gated by **MiIO auth** (the *"press POWER to pair"* handshake);
  firmware ships `.enc`. This is **channel encryption + auth, not confirmed image-signing** — no source
  shows an ECDSA/RSA check on the nRF51 image, and no RDP means **no hardware secure-boot**.
- **Recommendation for custom firmware:** flash the nRF51 via **SWD (ST-Link/J-Link)** once its pads are
  located — installs the VESC-App BLE firmware / Nordic DFU cleanly, bypassing MiIO. NUS UUIDs:
  service `6E400001-…`, TX `…0002`, RX `…0003`.

---

## D. Open items — physical multimeter/visual checks still needed
1. **G30 4-pin connector pin order** — ohm-out red/black/yellow/green ↔ physical pins on *your* cable.
2. **G30 connector exact part number** — confirm the G30-specific Higo/Julet variant (≠ M365 HIGO-B4-B).
3. **STM32 SWD pad → MCU pin** — verify pads land on PA13/PA14; buzz out USART pins.
4. **nRF51822 SWD pad locations** — undocumented; locate by inspection + datasheet.
5. **Internal STM32↔nRF51 UART pins** — confirm which USART bridges to the nRF51.
6. **+5 V access without removing C2** — does the red-wire 5 V feed work on your revision?
7. **SWDCLK pull resistor** on your revision (can block SWD).
8. **Whether stock IAP accepts a custom/unsigned image** (unlock gate) — test, or plan SWD for the
   custom bootloader.
9. **Throttle/brake location** (ESC vs dashboard) and the **display/LED internal pinout**.

---

## Key sources
- Official Ninebot ES protocol (IAP, addresses, checksum): https://cloud.scooterhacking.org/release/nbdoc.pdf
- ScooterHacking Full Tutorial — dashboard SWD, 4-wire cable, rewiring (archived):
  http://web.archive.org/web/20231022215605/https://www.scooterhacking.org/forum/viewtopic.php?t=266
- ScooterHacking Ninebot-IAP guide — 7-pin ESC↔BLE tap, "flash BLE first", BLE-3E (archived):
  http://web.archive.org/web/20250529161826/https://www.scooterhacking.org/forum/viewtopic.php?t=252
- Ninebot IAP tool: https://iap.scooterhacking.org/
- m365fw/vesc_m365_dash · tonymillion/VescNinebotDash · Sharkboy-j/vesc_g30_dash · CRZX1337/g30-vesc-dash
- irmo.de STM32F1 RE: https://www.irmo.de/2023/11/14/ninebot-max-g30-ii-firmware-hacking/ ·
  BLE/MiIO RE: https://www.irmo.de/2023/11/08/e-scooter-bluetooth-hacking/
- joeybabcock ESC ST-Link (no-solder pogo, 5V-via-dash-red-wire): https://joeybabcock.me/blog/electric-scooters/how-to-stlink-fix-bricked-ninebot-max-g30-controller-esc/
- NinebotCrypto / miauth: https://github.com/scooterhacking/NinebotCrypto · https://github.com/dnandha/miauth
- M4M control cable (HIGO-B4-B M365 ≠ G30): https://more4motion.com/products/main-control-cable-for-ninebot-segway-kick-scooter-max-g30-g30d-g30le-and-max-2-0-with-iot
