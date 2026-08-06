# How to wire the dashboard for dumping & flashing (nRF51822 over SWD)

**Why this replaces the old plan:** the dashboard has **no STM32** — its only MCU is an **nRF51822**
([`MCU_IDENTIFICATION.md`](../boards/ble-dashboard/MCU_IDENTIFICATION.md)). That is good news: the
nRF51 *is* supported by OpenOCD, so a plain **SWD** hookup gives a full backup and a flash path, and we
no longer need the auth-gated Ninebot IAP or the BLE handshake to get in.

Two independent hookups are described:

| | Purpose | Wires |
|---|---|---|
| **A. SWD** (this page, primary) | dump + flash the nRF51 | SWDIO, SWCLK, GND (+ power) |
| **B. Bus tap** | sniff / emulate the Ninebot bus | 1 data wire + GND — see [`C542_PROGRAMMER_SCHEMATIC.md`](C542_PROGRAMMER_SCHEMATIC.md) |

---

## A. SWD wiring

### What you need
- The **NUCLEO-C542RC** you already have — its on-board **ST-LINK/V3** can program *external* targets.
  (A standalone ST-Link V2 clone works too.)
- 3 jumper wires (+1 if you power the board from the probe — see the warning below).
- The dashboard accessible: the SWD pads sit **under silicone potting**; scrape gently with a
  *plastic* pick. The pads are fragile and traces lift easily.

### Using the NUCLEO's ST-LINK for an external chip ⚠️
This is the step people miss:

1. **Remove both `CN2` jumpers** on the NUCLEO. That disconnects the on-board ST-LINK from the
   on-board STM32C5 and frees it to drive an external target.
2. Wire your target to the **`CN4` / "ST-LINK" SWD header** (6-pin: `VDD_TARGET, SWCLK, GND, SWDIO,
   NRST, SWO`).
3. Put the `CN2` jumpers **back** when you next want to flash the C542 itself.

### Connections

```
   NUCLEO-C542RC (CN2 jumpers REMOVED)              G30 dashboard PCB
   ┌───────────────────────────┐                    ┌──────────────────────────┐
   │  ST-LINK/V3   CN4 header  │                    │   nRF51822 (QFN-48)      │
   │                           │                    │                          │
   │  pin 1  VDD_TARGET  ○─────┼── (sense only) ────┼──○ 3V3  ← see power note  │
   │  pin 2  SWCLK       ○─────┼────────────────────┼──○ SWCLK / SWDCLK pad     │
   │  pin 3  GND         ○─────┼────────────────────┼──○ GND pad     (REQUIRED) │
   │  pin 4  SWDIO       ○─────┼────────────────────┼──○ SWDIO pad              │
   │  pin 5  NRST        ○     │   (leave unwired)  │                          │
   │  pin 6  SWO         ○     │   (not used)       │                          │
   └───────────────────────────┘                    └──────────────────────────┘
                                                     Display driven by TM1637 on
                                                     P0.04 (DIO) / P0.05 (CLK)
                                                     Ninebot bus on P0.15 / P0.20
```

| Probe pin | Dashboard | Required | Notes |
|---|---|---|---|
| **SWDIO** | nRF51 SWDIO pad | ✅ | data |
| **SWCLK** | nRF51 SWDCLK pad | ✅ | clock |
| **GND** | board GND pad | ✅ | **common ground is mandatory** |
| VDD_TARGET | board 3V3 | recommended | lets the probe sense target voltage; **do not** use it to power the board |
| NRST | — | ❌ | not needed; nRF51 connects/halts over SWD alone |

### Finding the pads
Community teardowns describe a group of **3 pads labelled GND / SWCLK / SWDIO** on the board, plus a
separate power pad. Those are the **nRF51's** debug pads (the old docs called them "the STM32's" —
that was the mistaken-chip assumption).

If the silkscreen is unreadable, identify them with a multimeter in continuity mode against the
nRF51822 package (pinout in [`boards/ble-dashboard/datasheets/nRF51822_product_spec.pdf`](../boards/ble-dashboard/datasheets/nRF51822_product_spec.pdf)):
GND is the pad continuous with the ground plane; the remaining two are SWDIO/SWCLK. **Getting SWDIO and
SWCLK the wrong way round does no damage** — OpenOCD simply fails to connect, so it is safe to swap and
retry.

### Powering ⚠️
- **Preferred:** power the dashboard normally — from the scooter, or 5 V on the **red** wire of the dash
  cable — and connect only SWDIO/SWCLK/GND from the probe.
- **Do not** let the probe back-power a board that is also powered by the scooter.
- Never connect 5 V to a 3V3 pad. The nRF51 is a **3.3 V** part.

---

## Step 1 — check you can talk to it (read-only, harmless)

```bash
python tools/nrf51/nrf51_swd.py info
```

Expect the device ID, `256 KB` flash, and — importantly — the **BLE MAC**, which should read
`D8:68:BA:16:A0:33` for this scooter. If it does, you are definitely talking to the right chip.

It also reports **readback protection**:
- `readback protection: OFF` → a full dump is possible. Continue.
- `READBACK PROTECTION IS ON` → flash cannot be read out; only a mass-erase would clear it, which
  **destroys the stock firmware**. Stop and reconsider — do not erase.

## Step 2 — take a verified backup **before writing anything**

```bash
python tools/nrf51/nrf51_swd.py dump
```

This reads the whole 256 KB **twice** and compares, so a flaky probe cannot silently hand you a bad
backup. It also saves `UICR` + `FICR` and a `.json` with the SHA-256. Files land in
`boards/ble-dashboard/firmware/dumps/`.

You now hold the SoftDevice, the stock app, and the bootloader — the thing the whole IAP effort was
trying to obtain.

## Step 3 — flash custom firmware (only after step 2 succeeds)

```bash
python tools/nrf51/nrf51_swd.py flash firmware/dashboard-nrf51/build/dashboard.bin --address 0x18000
```

`0x18000` is the app slot **above the S110 SoftDevice** — flashing there leaves the BLE stack intact.
The tool refuses to run unless a verified dump exists, and it **never writes UICR**, so SWD access
cannot be locked out by accident.

## Step 4 — go back to stock at any time

```bash
python tools/nrf51/nrf51_swd.py restore boards/ble-dashboard/firmware/dumps/<dump>.json
```

The checksum is re-verified before anything is written.

---

## Safety summary

- **Back up first** (step 2) — everything else is reversible only because of it.
- **Never write UICR / APPROTECT.** The tool blocks it; don't work around it, or you can lose SWD
  access permanently.
- Keep the probe's ground connected before the signal wires; disconnect it last.
- The **BMS is always energised** — this procedure does not touch it, and you should not probe it.
- Run `python tools/verify_firmware_safe.py` (`/verify-safe`) before flashing any firmware you built.
