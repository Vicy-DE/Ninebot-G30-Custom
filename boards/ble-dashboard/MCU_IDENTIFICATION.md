# G30 dashboard — which MCU is actually on the board? (binary-evidence verdict, 2026-07-26)

> **VERDICT: there is no STM32 on the BLE dashboard.** The board's MCU is the **nRF51822 alone**.
> It drives the **TM1637** display, speaks the Ninebot `5A A5` bus protocol, and runs BLE — i.e. it is
> the *complete* dashboard controller. The long-standing repo claim *"BLE dashboard = STM32F103C8T6 +
> nRF51822"* is **NOT supported by any evidence in this repo** and is now considered **false**.

Method: binary analysis of the stock dumps only (no doc trust), cross-checked against the known-good
STM32 images (`DRV_*`, `BMS_*`) as a control. Reproduce with the scripts noted at the bottom.

---

## 1. The dashboard images are nRF51822 (Cortex-M0), not STM32

| Dump | initial SP | reset vector | flash base implied | nRF51 periph consts | STM32 periph consts |
|------|-----------|--------------|--------------------|---------------------|---------------------|
| `BLE_1.1.0` | `0x20003D10` | `0x00018155` | **`0x0000_0000`** (nRF51) | 7 (POWER/CLOCK, UART0, FICR, UICR) | 4 (coincidental data) |
| `BLE_1.1.7` | `0x20003DE8` | `0x00018155` | **`0x0000_0000`** (nRF51) | 7 (POWER/CLOCK, UART0, FICR, UICR) | 5 (coincidental data) |
| `DRV_1.2.6` *(control)* | `0x20002B18` | `0x08001101` | `0x0800_0000` (STM32) | 12 (noise) | **62** (RCC, GPIOA/B/C, USART1-3, ADC1, FLASH, UID, AIRCR) |
| `BMS_1.7.4.5` *(control)* | `0x200017C8` | `0x080010D9` | `0x0800_0000` (STM32) | 12 (noise) | **37** (RCC, FLASH, USART1/2, ADC1) |

The reset vectors are decisive: the dashboard images run from `0x00018000` (an nRF51 app placed after
the Nordic **S110 SoftDevice**), while the genuine STM32 images run from `0x08001000`. The real STM32
firmware shows 37–62 STM32 peripheral references; the dashboard images show essentially none.

Identity strings confirm these are the right board's firmware:
`Scooter_G30_SAT` @`0x3300` (BLE_1.1.7), plus SDK defaults `NBScooter0001`, `N3M-Ninebot-Mini0001`.

## 2. The nRF51 drives the display itself — a TM1637 on P0.04 / P0.05

**7-segment font table** — present in *both* dashboard images, **absent from the ESC and BMS dumps**:

```
BLE_1.1.0 @ file 0x81FF   BLE_1.1.7 @ file 0x846F  (VMA 0x0002046F)
3F 06 5B 4F 66 6D 7D 07 7F 6F | 77 7C 39 5E 79 71
 0  1  2  3  4  5  6  7  8  9 |  A  b  C  d  E  F
```

**Pin configuration** — two *adjacent* GPIOs set to output, then bit-banged (`0x50000710` = `PIN_CNF[4]`,
next word `PIN_CNF[5]`), i.e. the classic 2-wire **CLK/DIO** pair:

```asm
18da2:  ldr  r1,[pc,#48]   ; -> 0x50000710  (PIN_CNF[4])
18da6:  str  r0,[r1,#0]    ; PIN_CNF[4] = 3 (output)
18da8:  adds r1,r1,#4
18daa:  str  r0,[r1,#0]    ; PIN_CNF[5] = 3 (output)
        set(5) set(4) ... clear(4) ... clear(5)      ; START condition
```
The sibling routine at `0x18DD8` does `clear(5) clear(4) … set(5) set(4)` = **STOP** condition.

**The TM1637 update routine @ `0x00019DAA`** (BLE_1.1.7) is byte-for-byte the TM1637 protocol:

```asm
19daa: push {r4,r5,r6,lr}
19dac: mov  r6,r1          ; brightness
19dae: mov  r5,r0          ; -> 6-byte segment buffer
19db0: bl   0x18da0        ; START
19db4: movs r0,#0x40       ; DATA cmd: write, auto-increment address
19db6: bl   0x18e20        ; write_byte
19dba: bl   0x18dd8        ; STOP
19dbe: bl   0x18da0        ; START
19dc2: movs r0,#0xC0       ; ADDRESS cmd, digit 0
19dc4: bl   0x18e20
19dca: ldrb r0,[r5,#0]     ; loop: 6 segment bytes
19dcc: bl   0x18e20
19dd6: cmp  r4,#6          ; SIX grids
19dda: bl   0x18dd8        ; STOP
19dde: bl   0x18da0        ; START
19de2: movs r0,#0x88       ; DISPLAY-ON ...
19de4: orrs r0,r6          ; ... | brightness   (exactly the TM1637 spec)
19de6: bl   0x18e20
19dea: bl   0x18dd8        ; STOP
```

Recovered symbols: `0x18DA0` = `tm1637_start()`, `0x18DD8` = `tm1637_stop()`, `0x18E20` =
`tm1637_write_byte()`, `0x19D96` = `tm1637_display_off()` (writes `0x80`), `0x19DAA` =
`tm1637_update(seg[6], brightness)`.

> This independently **confirms the owner's physical observation** of a TM1637 next to the display —
> and pins it to **nRF51 P0.04 / P0.05**.

## 3. The nRF51 also speaks the Ninebot bus protocol

`movs #0x5A` @`0x184CC` immediately followed by `movs #0xA5` @`0x184D0` (frame builder; 3 such pairs),
plus the device addresses as immediates: `0x20` (ESC) ×35, `0x21` (BLE) ×13, `0x3E` (app) ×6.
Together with the UART0 references this is a full protocol endpoint, not a dumb radio.

## 4. Why the old "STM32 dashboard" claim existed

`DASHBOARD_PINOUT_RESEARCH.md` §B1 tagged it **[CONFIRMED]**, but the stated basis was an inference:
*"the ScooterHacking flashing tutorial uses an ST-Link V2 + STM32 ST-Link Utility → therefore these are
the STM32's pads."* An **ST-Link V2 is a generic SWD probe** routinely used to flash nRF51 chips (that is
exactly what ScooterHacking ReFlasher does), so this proves nothing about the chip's family. No teardown
photo, part marking, or firmware dump ever backed the claim. `RE_FINDINGS.md` had already flagged that
**no STM32 dashboard dump exists**; the architecture claim simply outlived its evidence.

## 5. Consequences for this project

1. **Nothing on the dashboard can be flashed via the STM32 IAP/bootloader path.** The custom
   `bootloader/stm32` work has **no target on this board** (it remains valid only for STM32 targets:
   the stock ESC — being replaced by VESC — and the BMS, which stays stock and is out of scope).
2. **Custom dashboard firmware must target the nRF51822 (Cortex-M0)** — i.e. what the deployment plan
   called "Phase 4" is really *the* dashboard phase; old Phases 1–3 (custom app/bootloader at
   `0x08001000` / `0x08000000` on a "BLE STM32") describe a chip that isn't there.
3. **Driving the stock display from custom firmware is now solved on paper**: reuse the TM1637 sequence
   above on P0.04/P0.05 with the recovered font table.
4. The dashboard's **chip UID gate** for the wired `CMD 0x57` update path was reasoned about as an
   *STM32* UID at `0x1FFFF7E8`. On an nRF51 the device ID is **FICR.DEVICEID[0..1] @ `0x10000060`** —
   any UID-derived password logic must be re-derived against the nRF51 image, not an STM32 one.

## 6. Reproduce

Scripts used (scratchpad, re-runnable): peripheral-constant census, 7-seg/TM1637 hunt, and
`arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x18000` on the BLE images.
Control comparison against `DRV_1.2.6` / `BMS_1.7.4.5` is what makes the peripheral counts meaningful.

**Still open (needs a photo, not a binary):** the exact nRF51822 package/variant marking (QFAA 16 KB RAM
vs QFAC 32 KB — the SP values `0x20003Dxx` fit the **16 KB** part), and whether any G30 revision ships a
different display driver. Neither affects the verdict above.
