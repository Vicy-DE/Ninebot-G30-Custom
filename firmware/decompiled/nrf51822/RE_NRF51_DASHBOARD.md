# nRF51822 dashboard firmware — reverse-engineering report (`BLE_1.1.7`, 2026-07-26)

Re-done from scratch against the **correct chip** after
[`boards/ble-dashboard/MCU_IDENTIFICATION.md`](../../../boards/ble-dashboard/MCU_IDENTIFICATION.md)
proved the dashboard is **nRF51822-only** (no STM32). Everything here is recovered from the stock
binary; addresses are VMA with the app loaded at **`0x00018000`**.

Reproduce the disassembly with:
`arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x18000 BLE_1.1.7.bin`

---

## 1. Image facts

| Property | Value |
|---|---|
| Core | **ARM Cortex-M0** (ARMv6-M) |
| App base | `0x00018000` (immediately after the Nordic **S110** SoftDevice) |
| Initial SP | `0x20003DE8` → **16 KB RAM part** (nRF51822-**QFAA**) |
| Reset vector | `0x00018155` (thumb → `0x00018154`) |
| Size | 34 252 B |
| Identity | `Scooter_G30_SAT` @`0x3300`; SDK defaults `NBScooter0001`, `N3M-Ninebot-Mini0001` |
| Functions | ~205 (`push {…,lr}` prologues) |

## 2. Pin map  ← **the payoff for custom firmware**

| Pin | Role | Confidence | Evidence |
|-----|------|-----------|----------|
| **P0.04** | **TM1637 `DIO`** (data) | **certain** | `tm1637_write_byte` drives pin 4 per data bit |
| **P0.05** | **TM1637 `CLK`** (clock) | **certain** | pin 5 toggled low→high once per bit |
| **P0.15** | **Ninebot bus** (TX or RX, swaps) | **certain** | `uart_init` args |
| **P0.20** | **Ninebot bus** (RX or TX, swaps) | **certain** | `uart_init` args |
| P0.25 | variant-dependent output | high | `PIN_CNF[25]=3` only when a config byte == 1 or 2 |
| P0.00, P0.03, P0.08 | GPIO (button / LED / aux) | medium | `PIN_CNF[0]`, `[3]`, `[8]` configured |

## 3. Display: TM1637, 6 grids — fully recovered

```
tm1637_start()       @0x00018DA0    tm1637_stop()        @0x00018DD8
tm1637_write_byte()  @0x00018E20    tm1637_read_ack()    @0x00018E68
tm1637_display_off() @0x00019D96    tm1637_update()      @0x00019DAA
gpio_set(pin)        @0x0001F29C    gpio_clear(pin)      @0x0001F27C
delay()              @0x000180F4
7-seg font table                    @VMA 0x0002046F (file 0x846F)
```

**Font** (common-cathode, `0-9` then `A-F`):
`3F 06 5B 4F 66 6D 7D 07 7F 6F | 77 7C 39 5E 79 71`

**`tm1637_write_byte(b)`** — bit-banged, **LSB-first**, 8 bits then an ACK read:
`PIN_CNF[4]=PIN_CNF[5]=3` (output) → per bit: `CLK↓`, set `DIO` = bit, `data>>=1`, `CLK↑`.

**`tm1637_update(uint8_t seg[6], uint8_t brightness)`** — the exact TM1637 command sequence:
1. `START`, `0x40` (data cmd, auto-increment), `STOP`
2. `START`, `0xC0` (address, digit 0), **6 segment bytes**, `STOP`
3. `START`, `0x88 | brightness` (display ON), `STOP`

`tm1637_display_off()` writes `0x80`.

## 4. Ninebot bus UART — half-duplex on two pins

`uart_init(tx_pin /*r0*/, rx_pin /*r1*/)` @**`0x0001FDB4`**:

```
PIN_CNF[tx] = 3 (output)          PIN_CNF[rx] = 4 (input, buffer connected)
UART0.PSELTXD (0x4000250C) = tx   UART0.PSELRXD (0x40002514) = rx
UART0.BAUDRATE(0x40002524) = 0x01D7E000   -> 115200
UART0.ENABLE  (0x40002500) = 4
TASKS_STARTTX, TASKS_STARTRX = 1 ; clear EVENTS_RXDRDY
```

Two callers **swap the same pin pair** — i.e. bus turnaround:

| Function | Direction | mode flag `[+5]` |
|---|---|---|
| `uart_mode_tx20()` @`0x0001A128` | TX=**P0.20**, RX=**P0.15** | `1` |
| `uart_mode_tx15()` @`0x0001A140` | TX=**P0.15**, RX=**P0.20** | `0` |

> Matches the live-bus capture (115200 8N1, `5A A5`, dashboard = SRC `0x21`). The stock firmware
> reverses TX/RX rather than tri-stating — replicate this when driving the bus half-duplex.

## 5. Ninebot protocol endpoint (the nRF51 speaks it directly)

- Frame builder: `movs #0x5A` @`0x000184CC` immediately followed by `movs #0xA5` @`0x000184D0`
  (3 such preamble pairs in the image).
- Address immediates: `0x20` (ESC) ×35, `0x21` (BLE/dash) ×13, `0x3E` (app) ×6.
- Parser region ≈ `0x00018450` (consistent with the earlier `RE_FINDINGS.md` note).

This is a **full protocol endpoint**, not a transparent radio bridge — which is why no second MCU
is needed on the board.

## 5b. Protocol re-verified against the builder/validator code (2026-07-26)

Not just inferred from captures — the **checksum algorithm is visible in the firmware**. At
`0x000184B0`, immediately before the `5A A5` writer:

```asm
184b0: mvns r3,r3        ; r3 = ~sum          <- ones' complement == XOR 0xFFFF
184b2: uxth r3,r3        ; truncate to 16 bits
184b6: ldrb r0,[r0,#31]  ; received checksum high byte
184b8: lsls r0,r0,#8
184ba: adds r0,r1,r0     ; lo | (hi<<8)       <- little-endian
184be: cmp  r3,r0
184c0: bne  0x18532      ; mismatch -> frame rejected
...
184cc: movs r0,#0x5A / strb r0,[r1,#0]
184d0: movs r0,#0xA5 / strb r0,[r1,#1]
```

⇒ `CK = (~Σ bytes) & 0xFFFF`, stored **little-endian**, preamble `5A A5` — confirming the codec in
`firmware/dashboard-nrf51/`, which round-trips the live-captured frame byte-for-byte in `sim/`.

## 5c. Power button / power management

**Corrections first:** `P0.08` is **not** the button. `PIN_CNF[8] = 0x0C` (input **+ pull-up**) is read
**once at init** (`0x0001A158`); the level is stored as `1` (high) or `2` (low) in a RAM byte, and that
byte later decides whether `PIN_CNF[25]` is driven (`0x000186A0`). It is a **board-variant strap**.

The button is handled the Nordic way — **GPIOTE `PORT` + `PIN_CNF.SENSE`**, not a dedicated polled pin:

| Evidence | Address | Meaning |
|---|---|---|
| `movs r4,#1; lsls r4,r4,#17` then OR into `PIN_CNF[pin]` | `0x0001FC44` | sets **`SENSE` = 0b10 (sense-high)** on a *parameterised* pin |
| `str r1,[r0,#60]` with base `0x40006140` | `0x00018CF4` | clears **GPIOTE `EVENTS_PORT`** (`0x4000617C`) |
| `ldr rX,[base+16]`, `eors`, `ands` mask | `0x00018D28` | reads **GPIO `IN`** (`0x50000510`), XOR vs previous, AND per-pin mask ⇒ **debounced change detection** (Nordic `app_button` pattern) |
| `OUTCLR = 8` (P0.03 low), `OUTSET = 1<<25` (P0.25 high) | `0x0001C85E` | discrete outputs — LED / hold-latch |

`PIN_CNF.SENSE` + `EVENTS_PORT` is precisely the nRF51 mechanism for **waking from System OFF**, so the
same input both powers the dashboard up and (held) triggers shutdown.

> **Not yet pinned:** the button's exact GPIO number. It is passed as an argument through the
> library helper, so it comes from a runtime config struct rather than a literal. Resolve it either by
> reading `PIN_CNF[0..31]` over SWD on a live board (the sensed pin will have `SENSE` set) or by
> continuity from the dash cable's **green** wire.
>
> Note the green wire is also read **externally**: on this VESC build the VESC itself reads the button
> with a pull-up (`pin-mode-in-pu` in `vesc-lisp/g30_dash.lisp`), so the button is available on both
> sides.

## 6. Peripherals in use (corrected nRF51 map)

| Base | Peripheral | Refs | Purpose |
|---|---|---|---|
| `0x40002000` | **UART0** | 11 | Ninebot bus |
| `0x40007000` | **ADC** | 6 | analog sense (battery / lever) |
| `0x40009000` | **TIMER1** | 6 | timing |
| `0x40010000` | **WDT** | 7 | watchdog |
| `0x40011000` | **RTC1** | 16 | tick / scheduling |
| `0x40006000` | **GPIOTE** | 2 | edge-triggered input (button) |
| `0x40000000` | POWER/CLOCK | 4 | clock + DCDC + RAM retention |
| `0x10001000` | **UICR** | 7 | user config (bootloader addr / variant) |
| `0x50000000` | GPIO P0 | 20 | pins above |

## 7. SoftDevice (S110) usage — BLE side

SVC calls recovered (S110 numbering): `sd_ble_enable`(96), `sd_ble_evt_get`(97),
**`sd_ble_uuid_vs_add`(99)** = vendor UUID → Nordic UART Service, `sd_ble_gap_ppcp_set`(118),
`sd_ble_gap_conn_param_update`(113), `sd_ble_gap_authenticate`(122), `sd_ble_gap_auth_key_reply`(124),
**`sd_ble_gatts_service_add`(160) ×2**, **`sd_ble_gatts_characteristic_add`(162) ×3**,
`sd_ble_gatts_value_set`(164), **`sd_ble_gatts_hvx`(166)** = notify, `sd_ble_gatts_sys_attr_set`(169),
plus **`sd_flash_write`/`sd_flash_page_erase`** (SoC SVCs ~39-41) → **persistent settings in flash**
(this is where odometer/config live).

2 services + 3 characteristics matches the observed GATT (NUS: service + RX-write + TX-notify),
with the second service being the Xiaomi `0xfe95` block seen on-air.

## 8. What this unlocks

1. **Custom dashboard firmware is an nRF51 (Cortex-M0) project** — see
   [`firmware/dashboard-nrf51/`](../../dashboard-nrf51/).
2. **The stock display can be driven exactly** (pins + protocol + font all recovered).
3. **Dumping no longer needs the auth-blocked IAP**: the nRF51 is reachable over **SWD**, and unlike
   STM32C5 it *is* supported by OpenOCD/pyOCD — see [`tools/nrf51/`](../../../tools/nrf51/).
4. Any UID-derived update password must be re-derived from **`FICR.DEVICEID` (`0x10000060`)**, not the
   STM32 `0x1FFFF7E8`.

## 9. Open items

- Exact semantics of `P0.00 / P0.03 / P0.08` (button vs LED vs charger-detect) — resolve by toggling
  on hardware or tracing the PCB.
- Which config byte selects the `P0.25` variant (two board/model revisions share this image).
- The flash-persistence record layout (offsets of odometer/config within the page).
