# How to wire for **OTA** updating (no dashboard disassembly)

The other route into the dashboard's nRF51822. Where [`WIRING_NRF51_SWD.md`](WIRING_NRF51_SWD.md)
opens the case and talks to the chip's debug port, **OTA reuses the scooter's own update path** — the
dashboard flashes *itself* (the stock app calls the SoftDevice's `sd_flash_write`/`sd_flash_page_erase`).

| | Opens the case? | Wires | Auth needed | Can it brick? |
|---|---|---|---|---|
| **SWD** | yes (scrape potting) | 3 | none | recoverable by definition |
| **OTA over BLE** | **no** | **none** | **yes** (Encryption2 + button press) | yes — bootloader survives |
| **OTA over UART** | no | 2–4 | yes (same) | yes — bootloader survives |

> **Take an SWD backup first.** OTA writes the app slot without giving you a copy of what was there.
> If an OTA is interrupted you need the stock image to get back — that is exactly what
> `tools/nrf51/nrf51_swd.py dump` produces.

---

## Channel 1 — OTA over BLE (zero wires)

Nothing to wire at all: the PC's Bluetooth talks to `G30LD` directly.

```
   PC (bleak / tools/ble)  ~~~BLE~~~>  nRF51822 dashboard
        NUS 6e400001                    self-flashes via sd_flash_write
        write 6e400002 / notify 6e400003
```

Requirements:
1. Scooter **powered on** and in range.
2. The **Encryption2** handshake completed (`0x5B` → `0x5C` → `0x5D`) — see
   [`boards/ble-dashboard/MCU_IDENTIFICATION.md`](../boards/ble-dashboard/MCU_IDENTIFICATION.md) and
   `tools/ble/`. The `0x5C` phase is the *"press the power button to pair"* step.
3. Then the Ninebot IAP commands ride the authenticated channel.

## Channel 2 — OTA over UART (wired, still no disassembly)

Use this when BLE auth is uncooperative, or you want a cable you can trust. The dashboard's bus is
exposed on the **dash cable** and on the **ESC↔BLE connector** — no need to open anything.

### Option A — the dash cable (4 wires, Higo/Julet plug)

```
   USB-TTL (3.3 V logic!)             G30 dash cable
   ┌────────────────────┐             ┌──────────────────────────────┐
   │ GND  ○─────────────┼─────────────┼──○ BLACK   GND               │
   │ TX   ○──[1k]───────┼──┬──────────┼──○ YELLOW  data (half-duplex)│
   │ RX   ○─────────────┼──┘          │                              │
   │ 5V   ○ (optional)  ┼─────────────┼──○ RED     +5 V              │
   └────────────────────┘             │  ○ GREEN   power button      │
                                      └──────────────────────────────┘
```

- The data line is **one wire, half-duplex** — join the adapter's TX and RX to it, with a **~1 kΩ series
  resistor in the TX leg** so the adapter can't fight the dashboard when both drive briefly.
- **3.3 V logic only.** A 5 V adapter will damage the nRF51.
- You will see your own transmissions echoed back; that is normal for one-wire.

### Option B — the ESC↔BLE 7-pin connector (documented no-solder tap)

`Pin 1 = 5 V · Pin 2 = GND · Pin 6 = RX · Pin 7 = TX` — clip on with test hooks.

### Option C — reuse the NUCLEO-C542RC rig you already built

The C542 already speaks this bus (software UART, push-pull TX, half-duplex turnaround) and can act as
the bridge — wiring in [`C542_PROGRAMMER_SCHEMATIC.md`](C542_PROGRAMMER_SCHEMATIC.md): **A2 = PA4** to
the data wire, **GND** to ground. No new wiring needed.

---

## The OTA protocol (same on both channels)

Ninebot IAP, carried inside `5A A5` frames addressed to the BLE board:

| Step | CMD | Payload | Meaning |
|---|---|---|---|
| 1 | `0x07` | `<u32 size>` | begin update |
| 2 | `0x08` | page data (`arg` = page #) | write a page — repeat |
| 3 | `0x09` | `<u32 checksum>` | finalise / verify |
| 4 | `0x0A` | — | reboot into the new image |

Error codes on the ACK: `1` out of bounds · `2` erase error · `3` write error · `4` **not locked** ·
`5` invalid address · `6` busy · `7` bad length.

> Note the stock update path is additionally gated by a **chip-UID password** on `CMD 0x57`
> (enter-update). On the nRF51 that ID is `FICR.DEVICEID @0x10000060` — **not** the STM32 address the
> older docs assumed. Deriving it is only necessary for the *stock* updater; custom firmware you flash
> over SWD can expose whatever update path you choose.

## Recommended order of work

1. **SWD dump** — get the backup (`nrf51_swd.py dump`). One-time, opens the case.
2. **SWD flash** your firmware while iterating — fast, no auth, always recoverable.
3. **OTA** last, once the firmware is stable and you want updates without opening the scooter.

## Safety

- Never interrupt an OTA (power loss mid-write) — keep the scooter charged and close by.
- OTA only rewrites the **app slot above `0x18000`**; the S110 SoftDevice and bootloader below it stay
  intact, which is what makes recovery possible.
- Do **not** send `sd_flash_*`-driven writes at addresses below `0x18000` from custom firmware.
- Run `/verify-safe` on any image before sending it.
