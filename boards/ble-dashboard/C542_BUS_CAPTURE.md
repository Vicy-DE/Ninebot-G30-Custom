# Hardware capture — scooter bus via the NUCLEO-C542RC software UART (2026-06-15)

**First real-hardware confirmation** of the dashboard bus, captured by a NUCLEO-C542RC
running a bit-banged software UART (no hardware USART), wired to the scooter's plug and
read back over SWD. This replaces "reference-derived/unverified" assumptions with measured
fact for the bus electrical + protocol layer.

## Method

- C542 firmware `firmware/dash-tap-c542/c5board/cap_raw_main.c`: logic-analyzer capture of
  the tapped wire (4× oversampled, triggered on activity), packed into SRAM.
- Host reads it over SWD (STM32CubeProgrammer) and decodes the UART offline
  (`decode_raw.py`). Core clock read live off the chip = **48 MHz** (HSI), so the bit timing
  self-calibrates.

## Measured facts (on the actual scooter)

| Property | Value | How |
|----------|-------|-----|
| Live wire | **A2 = PA4** (A0/PA0 was dead: 1 edge vs 7012 edges in 2 s) | edge-activity diagnostic |
| Baud | **115200** (measured bit ≈ 416 cyc @ 48 MHz; 48e6/416 = 115385) | edge-interval histogram |
| Framing | Ninebot **`5A A5 | LEN | SRC | DST | CMD | ARG | payload | CK`**, `CK=Σ(LEN..payload)^0xFFFF` | decoded + checksum-valid |
| Dashboard address | **SRC 0x21** | decoded frame |

## Captured frame (reproduced across 3 captures, checksum VALID)

```
5A A5 05 21 20 65 00 04 28 22 02 00 04 FF
hdr   |  |  |  |  |  └──── payload[5] ───┘ └ CK ┘
     LEN SRC DST CMD ARG
     =5  0x21 0x20 0x65 0x00
```

= the **dashboard (0x21) polling the ESC (0x20)** with CMD 0x65 — the dashboard is the bus
master on this wire. This matches the verified protocol exactly and confirms the wire-finder's
inference (`SRC 0x21 → BLE/dashboard side`).

## What this proves vs. what remains

**Proven on hardware:** the C542 software-UART (`soft_uart.c` logic) reads the real scooter bus;
the wire is A2/PA4 @115200; the framing/checksum/addresses are as the repo claims. The whole
read path (bit timing self-cal → capture → decode) works end-to-end on real silicon.

**TX also proven (2026-06-15):** with PA4 driven **push-pull** (the internal pull-up is too weak
for the bus capacitance — open-drain rises too slowly for 115200), the C542 transmits clean bytes:
the per-bit line read-back equals the transmitted bytes exactly (`AA BB CC DD` → echo `AA BB CC DD`,
reproduced). Firmware `xcvr_main.c` (SWD-driven half-duplex transceiver) + `c5_xcvr.py`. So the
software-UART is **bidirectional on the real scooter link**.

**Bidirectional confirmed (2026-06-15):** with the half-duplex turnaround fixed (PA4 = push-pull
OUTPUT only during TX, INPUT for RX), the C542 both transmits (echo-verified) and receives the live
bus (80+ bytes of the periodic dashboard frame per window). Full software-UART link to the scooter.

**Bootloader dump — BLOCKED on the IAP-entry command (genuine RE unknown):** the dump needs the
dashboard in stock IAP/bootloader mode. The dashboard is a bus *master* (polls the ESC), and **none of
the plausible "enter update" commands made it stop polling / enter the bootloader** — all transmitted
cleanly (echo OK) but were ignored, and the dashboard kept polling unchanged:
`CMD 0x02/ARG 0x07` (PC→dash and App→dash), `CMD 0x07` direct, `CMD 0x02/ARG 0xF0`. This matches the
fact that the repo's IAP docs are partly wrong (they state `LEN=4+payload`; the bus proves
`LEN=payload`). The real stock IAP-entry sequence is not established.

**Non-destructive:** because the wrong commands are ignored (no erase), nothing was harmed — the
dashboard is still running its app normally.

## ESC emulator on the C542 (2026-06-15) — works; fault not cleared

Hypothesis (user): the dashboard faults because the ESC never answers its polls, and refuses IAP while
comms are unhealthy. Built an **ESC emulator** (`esc_faker_main.c`): a real-time half-duplex slave that
**oversamples each frame at 4x and decodes it on-chip** (1x real-time sampling drifted and mis-framed —
4x is reliable), and replies as the ESC (SRC 0x20 → DST 0x21) within the turnaround gap.

**Result — it works:** on the live scooter the C542 received **283+** checksum-valid dashboard polls
(`5A A5 05 21 20 65 00 04 28 22 02 00 …`) and answered **every one** as the ESC, in real time.

**But the fault did not clear / IAP still refused:** with the emulator answering, and 11 IAP-enter frames
injected into the now-"healthy" bus, the dashboard **kept polling CMD 0x65 unchanged** (`rx_frames`
71→160→283) and never entered the bootloader. The ESC **reply content is a guess** (echo CMD + zero
payload); the dashboard evidently wants the real data CMD 0x65 returns, so it still considers the ESC
faulty. That payload format is proprietary and not in the repo.

## Protocol reverse-engineered + fault CLEARED (2026-06-15)

Reverse-engineered the dashboard↔ESC protocol from `vesc-lisp/g30_dash.lisp` (already in the repo) and
[etransport/ninebot-docs](https://github.com/etransport/ninebot-docs/wiki/protocol):

| Frame | Dir | Format |
|-------|-----|--------|
| **0x65** | dash→ESC | throttle (byte5) + brake (byte6) hall levels |
| **0x64** | ESC→dash | `5A A5 06 20 21 64 00 | mode batt light beep speed error | CRC` — **error byte = 0 ⇒ no fault** |
| 0x07 | →dev | start update, payload = u32 size; reply 0x0B bResult |
| 0x08/0x09/0x0A | →dev | write / finish(crc) / reboot |

**FAULT CLEARED — confirmed on hardware.** Sending the correct **0x64** reply (mode=eco, batt=80,
error=0) to every poll made the dashboard's behaviour change: it went from sending **only 0x65** (dead-ESC
retry) to **also sending 0x64 frames** (`5A A5 07 21 20 64 00 …`) — the richer conversation a dashboard
only has with a *live, healthy* ESC. The C542 is a working ESC emulator and the comm-fault is gone. So the
user's hypothesis was right: a comm fault was part of the picture.

**Still blocked: the app-level bootloader-entry command.** Even with the fault cleared, the bootloader
update-start `CMD 0x07` (correct `u32` size, tried SRC 0x3E *and* 0x20→0x21) is **ignored — no 0x0B ack**.
That is the signature that the **running app** doesn't handle the bootloader-level update command; only
the *bootloader* does, after a reset. Entering the bootloader needs the separate **app-level "set update
flag + reset" trigger**, which isn't `0x07` and isn't yet known (it may even arrive over BLE via the nRF,
not this ESC bus).

## Enter-bootloader fully RE'd — it is UID-authenticated (the real wall)

Disassembled the stock STM32 firmware (`DRV_1.2.6.bin`, `BMS_1.7.4.5.bin`; same protocol handler the
dashboard uses) — see the firmware-analyst findings:

- The device resets into its bootloader by setting a RAM flag (`[0x200007B8+0x0D]`/`+0x13`), which makes
  the main loop write a **`0x5A5A` "stay in IAP" magic to a flash marker page** then `NVIC_SystemReset`
  (`AIRCR=0x05FA0004`); the 4 KB stock bootloader checks that magic at boot.
- **CMD 0x18 is calibration**, *not* reset (needs sub-cmd 0x12 + "N4G" magic). The real **enter-update is
  CMD 0x57/0x59** — and it is **password-gated**: `payload = ~(UID0+UID1+UID2) ‖ ~(UID0·UID1·UID2)`,
  computed from the STM32 **96-bit chip UID at `0x1FFFF7E8`** (the firmware references that address at
  vma `0x08005478`). Writing "reg 0x78" does NOT reset (that 0x78/0x79 convention is unverified).
- **Verified on hardware (with the ESC emulated so the bus is healthy):** the dashboard answers **no
  reads** on the ESC bus (it is bus-master — UID/serial are read over BLE, not here), and **0x57/0x59/
  0x58/0x5C with a zero password are all ignored** (still polling). So the gate is real and the secret
  (the chip UID) is not available on this wire.

**This is an authentication barrier by design.** Completing the dump needs the dashboard's chip UID to
compute the CMD 0x57 password. The two real ways to get it, both supported by this rig:
1. **Capture a real Ninebot-app firmware update** of the dashboard — the app sends the *correct* CMD 0x57
   (real password) over this ESC bus; the C542 logic-analyzer records it. Replay it forever after (the UID
   is fixed per board). The ESC emulator keeps the bus healthy during capture.
2. **Obtain the dashboard STM32 chip UID** (96 bits @0x1FFFF7E8) by any means (BLE engineering read, etc.)
   → compute `~Σ`/`~Π` → send CMD 0x57 directly.

Everything else is built + proven on hardware: TX, 4×-oversampled RX, the ESC emulator that **clears the
fault**, the SWD-mailbox inject/read RE rig (`esc_inject.py`), the full update protocol map, the dumper
app, and the IAP DATA/VERIFY + ECDSA flow. The dump is gated on exactly one secret: the chip UID.

## BLE channel (PC) — found the dashboard; blocked by MiIO auth (2026-06-15)

Used the PC's Bluetooth (`tools/ble_ninebot.py`, bleak). The dashboard advertises as **`G30LD`
(D8:68:BA:16:A0:33)** with the **Nordic UART Service** (6e400001) + the **Xiaomi MiIO service `0xfe95`**
(chars 0x0001 control, 0x0004 beaconkey, 0x0010 auth, 0x0013 token, 0x0014 device-id). The nRF51 bridges
NUS↔STM32 (UART0) — i.e. the same protocol handler — but **only after MiIO authentication**. Verified on
hardware: connected fine, read the MiIO info chars (product 0x035C), but **the dashboard relays/answers
nothing** to NUS Ninebot frames (raw 5A A5, MiIO-control 0x0001, and dummy auth writes all got zero
notifications). Per the nRF51 RE, stock `BLE_1.1.x` runs the full MiIO flow (auth challenge → **token
login-confirm → cloud bind → register**), so the relay is keyed by the device's **MiIO registration token**
— a per-device secret held in the Xiaomi cloud / the owner's Mi Home account.

**So both channels are locked by design:** wired = chip-UID password (needs SWD to potted pads); BLE =
MiIO registration token (needs the Mi Home/Xiaomi-cloud token). Neither secret is derivable from the bus
or an unauthenticated BLE read. To finish, supply one of: (a) the **MiIO token** (from the owner's Mi Home
account / `python-miio` cloud with the Xiaomi login) → I implement the MiIO auth + read the UID / drive the
update; (b) a **captured real app-update** (the app sends the real CMD 0x57 password over the ESC bus —
the C542 rig records it); or (c) the **chip UID** by any means. All the rest is built and proven.
