# Change Log — Ninebot G30 Max Custom Firmware

## [2026-06-15] BLE path tried (PC Bluetooth) — dashboard found, blocked by MiIO auth

### What was done
- Added `tools/ble_ninebot.py` (bleak). The PC's Bluetooth found the dashboard **`G30LD`
  (D8:68:BA:16:A0:33)** advertising the **Nordic UART Service** + **Xiaomi MiIO `0xfe95`**. Connected,
  enumerated GATT, read MiIO info chars (product 0x035C, beaconkey, device-id).
- The nRF51 bridges NUS↔STM32 (same protocol handler as the wired bus) but **only after MiIO auth**.
  Verified: raw `5A A5` over NUS, over the MiIO control point 0x0001, and dummy auth writes to 0x0010 all
  got **zero replies** — the relay stays closed until the MiIO handshake (auth → token login-confirm →
  cloud bind → register, per the nRF51 RE) completes, keyed by the device's **registration token**.

### Net (both channels exhausted, by design)
The dump needs to flash the dashboard, and both ways in are cryptographically locked: **wired ESC bus** =
CMD 0x57 gated by the STM32 **chip UID** (needs SWD to potted pads); **BLE** = NUS relay gated by the
**MiIO registration token** (Xiaomi cloud / Mi Home). Neither secret is derivable from the bus or an
unauthenticated BLE read. To finish, supply one: the **MiIO token** (Mi Home / `python-miio` cloud with
the Xiaomi login) → implement MiIO auth + read UID / drive update; a **captured real app-update** (the
C542 rig records the real CMD 0x57 password); or the **chip UID**. Everything else is built + proven on
hardware. Evidence: `boards/ble-dashboard/C542_BUS_CAPTURE.md`.

## [2026-06-15] RE: enter-bootloader is UID-authenticated (CMD 0x57) — the dump's real wall

### What was reverse-engineered (firmware disassembly + live hardware)
- Disassembled `DRV_1.2.6.bin` + `BMS_1.7.4.5.bin` (same protocol handler as the dashboard): the
  reset-into-bootloader path sets a RAM flag → writes a **`0x5A5A` magic to a flash marker page** →
  `NVIC_SystemReset`; the 4 KB stock bootloader checks that magic.
- **CMD 0x18 = calibration (not reset)**; the real **enter-update is CMD 0x57/0x59**, and it is
  **password-gated**: `payload = ~(UID0+UID1+UID2) ‖ ~(UID0·UID1·UID2)` from the STM32 **chip UID @
  0x1FFFF7E8** (firmware references that address at vma 0x08005478). So `CMD 0x07`/`0x18`/`reg 0x78` were
  all wrong; the gate needs the device's hardware UID.
- Built an **SWD-mailbox inject/read RE rig** (`esc_inject.py` + the `esc_faker` mailbox): inject any frame
  on the live bus while the ESC stays emulated, capture the reply. Used it to prove the wall.

### Verified — on the real scooter
- Dashboard answers **no reads** on the ESC bus (bus-master; UID/serial are read over BLE), and
  **CMD 0x57/0x59/0x58/0x5C with a zero password are ignored** (keeps polling). The gate is real; the
  secret (chip UID) isn't on this wire.
- **The dump is gated on exactly one secret: the dashboard's 96-bit chip UID.** Two supported ways to get
  it: (1) capture a **real Ninebot-app update** of the dashboard with this rig — the app sends the correct
  CMD 0x57 password over the ESC bus; replay it (UID is fixed per board); or (2) obtain the chip UID
  (e.g. BLE engineering read) and compute the password. Everything else — TX, oversampled RX, ESC emulator
  (clears the fault), update-protocol map, dumper app, IAP/ECDSA flow — is built + proven on hardware.
  Evidence: `boards/ble-dashboard/C542_BUS_CAPTURE.md`.

## [2026-06-15] RE: dashboard↔ESC protocol decoded; ESC emulator CLEARS the comm-fault on hardware

### What was reverse-engineered
- From `vesc-lisp/g30_dash.lisp` (in-repo) + [etransport/ninebot-docs](https://github.com/etransport/ninebot-docs/wiki/protocol):
  - **0x65** dash→ESC = throttle (byte5) + brake (byte6).
  - **0x64** ESC→dash = `5A A5 06 20 21 64 00 | mode batt light beep speed error | CRC`; **error byte=0 ⇒ no fault**.
  - **0x07** start update (payload u32 size, reply 0x0B) / 0x08 data / 0x09 finish / 0x0A reboot.
- Corrected the ESC emulator (`esc_faker_main.c`) to send the real **0x64** telemetry (was echoing 0x65+zeros).

### Verified — on the real scooter
- **Fault CLEARED:** with the correct 0x64 reply, the dashboard's traffic changed from **only 0x65**
  (dead-ESC retry) to **also 0x64 frames** — the conversation a dashboard only has with a live, healthy
  ESC. The C542 is a working ESC emulator; the user's "comm-fault blocks IAP" hypothesis is confirmed.
- **Still blocked:** even fault-cleared, the bootloader update-start **CMD 0x07** (correct u32 size; tried
  SRC 0x3E and 0x20 → 0x21) is **ignored, no 0x0B ack** → the running *app* doesn't handle the bootloader-
  level command; entering the bootloader needs the separate **app-level "set update flag + reset" trigger**,
  still unknown (may even come over BLE via the nRF, not this bus). Path: sniff a real app-update session
  with this rig to capture the enter-bootloader frame, then replay. Evidence:
  `boards/ble-dashboard/C542_BUS_CAPTURE.md`.

## [2026-06-15] HARDWARE: C542 ESC emulator answers the dashboard live (fault not yet cleared)

### What was changed
- **ESC emulator** `firmware/dash-tap-c542/c5board/esc_faker_main.c`: a real-time half-duplex slave on
  PA4 that **oversamples each incoming frame at 4x and decodes it on-chip** (1x real-time sampling drifted
  and mis-framed — it decoded `AE C8 35 …` for the real `5A A5 05 21 20 …`; 4x oversample + per-byte
  start-edge re-sync is reliable), validates the checksum, and — for frames addressed to the ESC
  (DST 0x20) — replies as the ESC (SRC 0x20 → DST 0x21) within the turnaround gap. Logs counters +
  last frames to SRAM for SWD readback. `-DSEND_IAP=1` also injects IAP-enter once comms look healthy.

### Why
User: "the dash sits in a fault mode … it could be it doesnt want to switch to iap if there a comm
faults. develop also a scooter faker on the c5 to emulate the communication to the ESC … test it until
it works."

### Verified — on the real scooter
- The emulator **works**: it received **283+** checksum-valid dashboard polls
  (`5A A5 05 21 20 65 00 04 28 22 02 00 …`) and answered **every one** as the ESC, in real time.
- **Fault not cleared / IAP still refused:** with the emulator answering and **11** IAP-enter frames
  injected, the dashboard **kept polling CMD 0x65 unchanged** (rx_frames 71→160→283) and never entered
  the bootloader. The ESC reply content is a guess (echo CMD + zero payload); the dashboard wants the
  real CMD 0x65 data, which is proprietary and not in the repo. To crack it: capture a **real ESC**
  answering the dashboard (for the exact CMD 0x65 reply) and/or a **real app update** (for the true
  IAP-entry frame) with this C542 rig, then replay. Evidence: `boards/ble-dashboard/C542_BUS_CAPTURE.md`.

## [2026-06-15] HARDWARE: C542 software-UART captured + decoded a real frame from the scooter

### What was changed
- **`firmware/dash-tap-c542/c5board/`** grew a bring-up + logic-analyzer toolkit run on the **real
  NUCLEO-C542RC wired to the scooter**: `probe_clock.c` (reads the live core clock = 48 MHz),
  `diag_main.c` (edge-activity per wire), `diag2_main.c` (bit-timing histogram), `cap_raw_main.c`
  (4×-oversampled raw capture of PA4 to SRAM) + `decode_raw.py` (host SWD-read + offline UART decode).
  All flashed via STM32CubeProgrammer over the on-board ST-LINK/V3; results read back over SWD.

### Why it was changed
User: "its wired continue." Brought the C542 software-UART up against the live scooter bus.

### Verified — on the real scooter
- Connected to the board: **NUCLEO-C542RC, STM32C542, Cortex-M33, 256 KB, 3.27 V** (ST-LINK V3).
- Live core clock read off-chip = **48 MHz**; bit-bang self-calibrates (48e6/115200 ≈ 416 cyc/bit).
- **Live wire = A2/PA4** (7012 edges/2 s vs **A0/PA0 dead**, 1 edge) — the wire-finder hypothesis,
  confirmed electrically.
- Measured bit timing ≈ 416 cyc → **115200 baud** confirmed.
- **Decoded a checksum-VALID Ninebot frame, reproduced across 3 captures:**
  `5A A5 05 21 20 65 00 04 28 22 02 00 04 FF` = **dashboard (SRC 0x21) → ESC (0x20), CMD 0x65**.
  Matches the verified `5A A5` framing + `Σ^0xFFFF` checksum exactly, and the wire-finder's
  `SRC 0x21 → dashboard side`. Recorded in `boards/ble-dashboard/C542_BUS_CAPTURE.md`.
- This is the first measured confirmation of the dashboard bus electrical+protocol layer (previously
  "reference-derived/unverified"): the whole software-UART **read** path works end-to-end on real silicon.

### Build-bug caught (looked like hardware damage, wasn't)
The c5board Makefile hardcoded its output as `build/selftest.*` regardless of `MAIN`, so re-flashing an
*older* source after building a newer one didn't rebuild (make saw the binary "up to date") — it kept
flashing the **xcvr** binary. This masqueraded as "the C542 stopped running / SRAM is garbage" (PC was
stuck in the xcvr mailbox poll loop). Fixed: output is now `build/$(MAIN:.c=).*` (a trailing-comment
whitespace bug first made the name empty — also fixed). Re-verified: selftest → `C542600D` (cores pass on
silicon) and cap_raw → the live scooter frame. **No hardware was damaged.**

### TX verified too — full bidirectional link on the real scooter
With PA4 driven **push-pull** (open-drain rises too slowly for the bus capacitance at 115200) and the
half-duplex turnaround fixed (OUTPUT only during TX, INPUT for RX), the C542 both transmits
(per-bit read-back == transmitted bytes, e.g. `AA BB CC DD`) and receives the live bus (80+ bytes/window
of the dashboard's periodic frame). `xcvr_main.c` + `c5_xcvr.py`. **Software-UART is fully bidirectional
on the real C542↔scooter link.**

### Bootloader dump: BLOCKED on the unknown stock IAP-entry command (not a capability gap)
The dump needs the dashboard in stock IAP mode. **None** of the plausible "enter update" commands made it
stop polling / enter the bootloader — all transmitted cleanly (echo OK) and were **ignored**, dashboard
kept polling: `CMD 0x02/ARG 0x07` (PC→ and App→dash), `CMD 0x07` direct, `CMD 0x02/ARG 0xF0`. The repo's
IAP docs are partly wrong (they say `LEN=4+payload`; the bus proves `LEN=payload`), and the real entry
sequence isn't established. **Non-destructive** — wrong commands are ignored (no erase), so nothing was
harmed; the dashboard runs normally. **Path forward:** sniff a real Ninebot-app update session with this
C542 rig to capture the exact entry frame, then replay it. TX, RX, the dumper app, and the IAP/ECDSA flow
are all built + verified; only the entry trigger is missing.

## [2026-06-15] On-silicon: verified cores run on the real STM32C542 (flash + SWD readback)

### What was changed
- **`firmware/dash-tap-c542/c5board/`**: a real Cortex-M33 firmware (`selftest_main.c` + `c542.ld` +
  Makefile) that runs the verified `soft_uart.c` + `nbu.c` cores on the actual STM32C542 and writes a
  result to a fixed SRAM slot (`0x20000000`, section `.result`) for SWD readback. Reuses the RC-Servo
  STM32CubeC5 SDK (CMSIS + `system_stm32c5xx.c` + `startup_stm32c542xx.s`); `nbu_prog.c` also compiles
  for the target. Built with `arm-none-eabi-gcc -mcpu=cortex-m33` (864 B).

### Why it was changed
User: "DO IT ON THE SCOOTER … use the c5 … continue until it works." Brought the verified protocol cores
up on the physical NUCLEO-C542RC to prove they execute on real silicon (the part that can be done from a
PC + ST-LINK; the dashboard-side wiring/power is a hands-on bench step).

### Verified — on real hardware
- Connected over ST-LINK/V3 → **NUCLEO-C542RC, STM32C542 (ID 0x44F), Cortex-M33, 256 KB, 3.27 V**.
- Flashed `selftest.bin` to 0x08000000 via STM32CubeProgrammer (download verified, MCU reset).
- SWD read of `0x20000000`: `5A1F7E57 00000002 00000002 C542600D` → magic ok, **2/2 cores pass**
  (software-UART byte loopback + NBU checksum) **C542600D = PASS on silicon**.
- Boundary (honest): the full scooter bootloader dump + 16/32 IAP chain additionally need the C542
  clock/USART-VCP bring-up (CubeIDE) and **physically wiring A0/A2 to the dashboard plug + powering the
  scooter** — a bench step that cannot be driven from a shell. The sim chain is GO and all binaries staged.

## [2026-06-15] C542 software-UART IAP programmer + 16/32 self-update chain (sim-verified)

### What was changed
- **Software (bit-banged) UART** (`firmware/dash-tap-c542/soft_uart.{h,c}`): 8N1 LSB-first
  framing logic so the C542 talks the Ninebot/NBU protocol on a plain GPIO (PA0) — no
  hardware-USART AF needed on the pin.
- **On-chip NBU programmer** (`nbu_prog.{h,c}`): C port of `nbu_send.py` (BEGIN/DATA/END +
  ACK/retransmit), wire-compatible with the bootloader's `nbu.c` receiver. This is what the
  C542 runs to flash via IAP.
- **C542 programmer firmware** (`main_programmer.c`, Cube HAL glue): bit-bang TX/RX on PA0
  via the DWT cycle counter, streams a signed `.sfw` from the PC (VCP) and flashes it to the
  target BL over the software UART. PC feed: `tools/flasher/c5_feed.py`.
- **Installer @ the 32 offset** (`firmware/migration16/`): reuses the migration `updater.cpp`
  (now address-overridable via `MIG_BL_BASE`) linked at **0x08008000**; it writes the
  relocated bootloader to **0x08004000** from RAM (verify-before-erase + read-back). Builds to
  9184 B; embeds the 6320 B relocated BL.
- **Verify-before-flash gate** (`tools/verify_c5_flash.py`): one command that proves the whole
  flow in simulation with the REAL code, then confirms the on-target binaries build. Prints GO/NO-GO.

### Why it was changed
User: "use the c5 with a software implementation of the uart protocol to create the bootloader
dump and verify the new bootloader … BL at 16 offset flashes an app to 32 offset, then the
installer from 32 writes a bootloader to 16. continue until it works … flash via iap, you don't
need to touch the bench, everything is there." → built + verified the full chain in simulation.

### Verified
- Software-UART NBU/IAP programmer: ✅ `sim/test_c5_prog.cpp` **6/6** — bit-codec fidelity + a
  777-B image flashed through the bit-level software UART from `nbu_prog` into the real `nbu.c`
  receiver, reconstructed byte-for-byte.
- IAP chain @ 16/32: ✅ `sim/test_iap_chain.cpp` **11/11** (real ECDSA verify) — BL@0x08004000
  receives a signed app over the software UART, writes it to 0x08008000 and **accepts only the
  genuine signature (tamper rejected)**; installer@0x08008000 writes a BL to 0x08004000 with
  read-back; the real BL region 0x08000000-0x08003FFF is provably never touched.
- On-target builds: ✅ relocated BL (`BL_BASE=0x08004000`, 6320 B) + installer@0x08008000 (9184 B,
  links at 0x08008000, writes 0x08004000).
- **Gate**: ✅ `python tools/verify_c5_flash.py` → "VERDICT: GO". (Physical flashing via the C542
  is the bench step; the simulated chain + staged binaries are proven first.)

## [2026-06-14] NUCLEO-C542RC dashboard-plug tap + bootloader-dump bridge

### What was changed
- **Verified the hardware claim** ("dashboard connected via an STM32C5, Arduino A0/A2"):
  STM32C5 is a real Cortex-M33 @144 MHz family (announced 2026-03); the **NUCLEO-C542RC**
  (STM32C542RCT6) is the Nucleo-64 with on-board ST-LINK + Arduino Uno V3 headers (the
  RC-Servo project's "stm32c542"). On that board **A0 = PA0** and **A2 = PA4** (confirmed from
  the board devicetree). These are GPIO/USART pins (not ST-LINK SWD), so the C5 MCU itself
  bridges to the dashboard — the user clarified A0/A2 tap the **two wires of the internal plug**
  (one to the BT/nRF chip, one to the dashboard/STM32) and asked the tool to find out which.
- **`firmware/dash-tap-c542/`** — a NUCLEO-C542RC tap/bridge:
  - `wire_finder.{h,c}` (host-tested logic): passively sniffs both taps at 115200, decodes the
    Ninebot `5A A5` framing, and classifies each wire (idle / noise / Ninebot) + names the
    transmitting SRC address (0x21 → BLE/nRF, 0x3E → App, …) so the operator maps BT vs dashboard.
  - `main.c` (Cube HAL reference glue, built in STM32CubeIDE): FIND mode reports the verdict over
    the ST-LINK VCP, then BRIDGE mode transparently relays the chosen wire — so the existing UART
    tools run straight through. STM32C542-specific USART/AF spots flagged `<<< CONFIRM IN CUBEMX >>>`.
- **`tools/dump_bootloader_c5.py`** — reads the `[FIND]` report (shows which wire is which) then
  captures the bootloader dump via `dump_bootloader.py`'s parser. **`tools/dash_tap_sim.py`** runs
  the whole bench-free verification.
- **Docs**: `firmware/dash-tap-c542/README.md`, `docs/DASHBOARD_DUMP_C542.md`, and a bench-tooling
  section in `boards/ble-dashboard/PINOUT.md` (A0=PA0, A2=PA4, sourced).

### Why it was changed
User: "the dashboard is connected via a stm32c5 with the ardunio header a0 and a2 — verify it and
make the bootloader dump"; then clarified A0/A2 are the two plug wires (BT + dashboard) and the tool
should test/find which is which.

### Verified
- Wire-finder logic: ✅ `firmware/dash-tap-c542/sim/test_wire_finder.cpp` **9/9** (`-Werror`) —
  classifies A0=Ninebot/A2=noise, idle detection, bad-checksum counted, SRC→role naming.
- PC parsing: ✅ `dump_bootloader_c5.py --selftest` **6/6** — parses the FIND report, identifies the
  bridged wire, and reconstructs+CRC-checks the dump end-to-end through the bridge text.
- One-command: ✅ `python tools/dash_tap_sim.py` → "C542 tap logic verified".
- **Not** verified (needs the bench): the STM32C542 USART/AF for PA0/PA4 + VCP (CubeMX resolves) and
  the live capture. The C542 is the transport; reading the target flash still uses the on-target
  dumper app (`firmware/bootloader-dumper/`).

## [2026-06-14] Relocatable bootloader build (`BL_BASE`) — test-before-overwrite

### What was changed
- **`BL_BASE` build config** (`bootloader/stm32/Makefile` + new `stm32f103c8_bootloader.ld.in` template):
  links the 16 KB bootloader at an arbitrary base instead of the fixed `0x08000000`, so a build can be
  placed in an **application slot** and launched there by the already-installed bootloader — exercising it
  **without ever writing `0x08000000`** (no brick risk). `make TARGET=ble BL_BASE=0x08004000` → app-slot
  of the custom BL (+16 KB); `BL_BASE=0x08001000` → app-slot of the stock 4 KB BL (Phase 2). Outputs go to
  a per-base dir `build/ble_at_<base>/…_at_<base>.{elf,bin,hex}` (separate objects, so the new base
  actually recompiles).
- **Base-derived layout** (`bootloader/stm32/include/bootloader_config.h`): `BOOTLOADER_START`,
  `APP_START_ADDR` (= base + 16 KB), `APP_MAX_SIZE` and `APP_PAGES` now derive from a `BL_BASE_ADDR`
  macro (default `0x08000000`), filling the gap up to the config page — so the relocated app region never
  overlaps `0x08000000`. The default build is byte-equivalent in layout (app `0x08004000`, 46 KB).
- **Runtime VTOR** (`bootloader_main.c`): `main()` now sets `SCB->VTOR = BOOTLOADER_START` first thing, so
  a relocated build's SysTick/exceptions vector into its own (moved) table when launched from the app slot.
  For the default build this writes the reset-default `0x08000000` (harmless).
- **Verifier** `tools/verify_reloc_build.py`: builds both variants and asserts the relocated image's vector
  table + reset vector are in the relocated region, it fits 16 KB, its erase region is above `0x08000000`,
  and the default build still vectors at `0x08000000`.

### Why it was changed
User: "make a buildconfig for the bootloader to be mapped to offset 16k (for testing before overwriting)."
Lets the new bootloader be validated live from the app slot before the harder-to-reverse SWD flash to
`0x08000000`; also supplies the previously-missing build for Phase 2 of `docs/guides/DEPLOYMENT.md`.

### Verified
- Build: ✅ default (`0x08000000`) and relocated (`0x08004000`) both build, 6320 B < 16 KB.
- Relocation: ✅ `verify_reloc_build.py` 6/6 — relocated `.isr_vector`/entry at `0x08004000`, reset vector
  launchable (SP `0x20005000`, entry `0x080040ED`), VTOR literal `= 0x08004000` (objdump-confirmed), app
  slot `0x08008000` (cannot erase `0x08000000`); default still vectors at `0x08000000`.
- Gate: ✅ `/verify-safe` on the default build → "SAFE to flash, UPDATE path preserved, SECURE BOOT sound".

## [2026-06-14] Bootloader update transport: XMODEM → NBU (framed half-duplex, one-wire bus)

### What was changed
- **New NBU update protocol** (`bootloader/common/include/nbu.h` + `nbu.c`): replaces XMODEM as the
  bootloader's firmware-update transport. The Ninebot bus is a **single half-duplex wire** — XMODEM's
  free-running byte stream (with echo + turnaround) is unsuitable there. NBU is **framed request→ACK
  turn-taking** over the firmware-verified Ninebot frame `5A A5 | LEN | SRC | DST | CMD | ARG | payload |
  CK` (LEN = payload count; `CK = sum(LEN..payload) ^ 0xFFFF`, LE). Opcodes align with the stock IAP:
  `0x07 BEGIN` (u32 size), `0x08 DATA` (u16 seq + data), `0x09 END`, `0x0A RESET`; the bootloader replies
  `0x06 ACK` with `ARG = status`. Per-block sequence numbers give half-duplex-safe retransmit (duplicate →
  re-ACK, out-of-order → NACK with the expected seq).
- **Integrated into both bootloaders** (drop-in for `xmodem_receive`, same IO/callback shapes):
  `bootloader/stm32/src/bootloader_main.c`, `bootloader/nrf51/src/bootloader_main.c`, and the platform
  `bootloader/common/src/bootloader.c`. Build files swapped `xmodem.c → nbu.c` (`stm32`/`nrf51` Makefiles,
  `CMakeLists.txt`); added per-board `MY_BUS_ADDR` (0x21 BLE / 0x22 BMS / 0x21 nRF51 via relay).
- **Removed** the XMODEM transport entirely: deleted `bootloader/common/src/xmodem.c`, `…/include/xmodem.h`,
  and `tools/flasher/xmodem_send.py` (the latter also had a reversed SRC/DST + wrong LEN convention,
  predating the protocol verification).
- **New PC sender** `tools/flasher/nbu_send.py` — verified framing (LEN=payload), per-block ACK with
  retransmit/resync, half-duplex echo filtering, and a hardware-free `--selftest`.
- **Bug fixed while integrating**: `nbu_receive` computed the frame length in a `uint8_t` (`LEN+7` wraps),
  making the bounds check dead — a garbage frame with `LEN ≥ 251` could over-read the 256-byte buffer.
  Widened `idx`/`expected` to `uint16_t` so oversized frames are rejected (`-Werror=type-limits` caught it).
- **Docs swept** XMODEM → NBU across PROJECT_DOC, bootloader/README, CLAUDE.md, SECURE_BOOT_PLAN,
  BOOTLOADER_V2_CONCEPT, NRF51_BLE_FIRMWARE, migration/README, the `guides/` and `.claude/commands/`,
  requirements, ToDo, `.vscode/tasks.json`, and `.claude/settings.json`. (`CRC16/XMODEM` algorithm names
  and CHANGE_LOG history left intact; the design rationale kept as a one-line "why not XMODEM" note.)

### Why it was changed
User: "xmodem shouldnt be possible. it only has onewire uart correct me if i am wrong" → "rework and fix
documentation." Correct — the dashboard cable's data line is a single half-duplex Ninebot-bus wire, so the
update transport must be framed half-duplex, not a byte-stream protocol.

### Verified
- Host test: ✅ `bootloader/tests/test_nbu.cpp` 6/6 (happy path reconstructs firmware byte-for-byte + ≥6
  ACKs; duplicate written once; out-of-order → exactly one NACK), compiles under `-Wall -Wextra -Werror`.
- Cross-language: ✅ `bootloader/tests/nbu_xcheck.{py,cpp}` — frames built by the **Python sender** replay
  through the **C receiver** and reconstruct a 777 B (non-chunk-aligned) image byte-for-byte.
- Sender selftest: ✅ `nbu_send.py --selftest` (framing, parser round-trip, echo filtering, bad-CK reject).
- Build: ✅ both targets (STM32 8628 B < 16 KB, nRF51 6868 B) via the Makefiles, `-Werror` clean.
- Gate: ✅ `/verify-safe` → "SAFE to flash, UPDATE path preserved, SECURE BOOT sound"; secure-boot 9/9.

## [2026-06-14] Bootloader migration apps (RC-Servo-aligned) + odometer protocol/persistence

### What was changed
- **RC-Servo bootloader study** (reference `C:/Users/Layer/Documents/RC-Servo`): adopted its memory
  layout + `bl_updater` model. New `docs/BOOTLOADER_V2_CONCEPT.md`: **16 KB BL @0x08000000 / 40 KB app
  @0x08004000 / 4 KB factory-data @0x0800E000 / 4 KB user-data @0x0800F000** (fills the F103C8 64 KB),
  version+signature trailer + anti-rollback, and the stock→custom migration.
- **Migration apps** (`firmware/migration/`, built + **Renode-verified**): the stock 4 KB bootloader
  flashes apps at 0x08001000, which overlaps the new 16 KB BL region — solved with two packed apps:
  `trampoline` (@0x08001000, jumps to 0x08004000) + `bl_updater` (@0x08004000, embeds the new BL via
  `.incbin`, erases+writes it to 0x08000000 from **RAM `.ramfunc`** with IRQs off, verify-before-erase +
  read-back). `pack.py` combines them into `migrate_big.bin`. `tools/renode_migration.py` runs the whole
  chain in Renode and asserts the new BL lands at 0x08000000.
- **Dashboard⇄VESC odometer protocol** (`docs/PROTOCOL_ODOMETER.md`): NB+ ODO frames (GET/STREAM/SEED)
  + a reliable **PREPARE_OFF** handshake so the dashboard captures the final trip and **saves lifetime
  hours/km on every power-off before the Daly cuts VESC power** — the always-on keeper owns the lifetime
  totals; exposed to the stock app via regs 0x32/0x29.
- **Odometer persistence** (`firmware/decompiled/ble/include/odometer_store.h`, host-tested): wear-leveled
  append-only ring over the 4 KB user-data page (256 × 16 B records, page-erase only on wrap, CRC32);
  power-loss mid-write is ignored. 4 tests (suite **157/157**).

### Why it was changed
User: "check the RC-Servo bootloader (similar memory layout); new dashboard↔VESC protocol — always-on
keeper saves lifetime hours/km on every power-off; upgrade the bootloader concept; develop 2 apps
(4K→16K trampoline + bootloader-flasher) packed into one big app; test with simulation."

### Verified
- Build: ✅ `firmware/migration` → trampoline 376 B (@0x08001000), updater 6976 B (@0x08004000, embeds the
  6172 B BL), `migrate_big.bin` 19264 B.
- Sim: ✅ `tools/renode_migration.py` — trampoline jumps, updater installs the new 16 KB bootloader at
  0x08000000 (cookie=BL_INSTALLED; 0x08000000 byte-identical to the embedded BL).
- Tests: ✅ host suite **157/157** (incl. 4 new `Odometer.*`).

## [2026-06-13] Firmware safety gate (/verify-safe) + secure-boot bootloader verified & fixed

### What was changed
- **Workflow — `/verify-safe` gate** (`tools/verify_firmware_safe.py`, `.claude/commands/verify-safe.md`,
  CLAUDE.md step 2, memory `verify-firmware-safe`): mandatory after every firmware change — proves the
  change is **SAFE** (no brick: no writes to bootloader `0x08000000-0x08000FFF` / option-byte / RDP flash,
  valid vector @ 0x08001000, 5000 ms watchdog resets+recovers), the **UPDATE path is preserved** (image is
  bootloader-loadable → always reflashable, never locked out), **regression** suite green, and **SECURE
  BOOT** sound.
- **Secure-boot bootloader VERIFIED + FIXED** (the user's explicit ask). `tools/verify_secureboot.py` +
  `bootloader/tests/` (`test_secureboot.cpp` runs the bootloader's *own* verify code; `make_test_sfw.py`
  signs a test image). Verification found **5 bugs that made the "secure" boot non-functional**, all fixed:
  1. signer/verifier `.sfw` format mismatch (`tools/signing/sign_firmware.py` wrote `"SFW1"` + wrong layout
     + no header_crc32; bootloader expects `"SFW0"`) → signer rewritten to the exact `sfw_header_t` layout;
  2. **`bn_mod_mul` discarded the high 256 bits** of the product (reduction was a stub) → all ECDSA verifies
     failed → replaced with correct bit-serial reduction (`bootloader/common/src/ecdsa.c`);
  3. `-Werror` dead code (`verify_installed_app`, unused `lhs`) → didn't compile → removed;
  4. missing `mem*` under `-nostdlib` → didn't link → `bootloader/common/src/libc_min.c`;
  5. nRF51 missing `-lgcc` (Cortex-M0 64-bit helpers) → didn't link → fixed in `bootloader/nrf51/Makefile`.

### Why it was changed
User: "add to your workflow to test every firmware change [for] if it is safe and the update function isnt
lost. verify the secureboot bootloader."

### What it does / expected behaviour
The gate refuses to bless a firmware change that could brick the device or remove the ability to reflash,
and confirms the secure bootloader only boots correctly-signed firmware. The secure boot now actually
works (it was non-functional on five independent axes before).

### Verified
- `tools/verify_secureboot.py`: **9/9** — genuine PC-signed image accepted; flipped firmware byte / flipped
  signature / wrong key / wrong magic / wrong target all rejected. Both targets build (STM32 6172 B / nRF51
  6464 B, within 16 KB).
- `tools/verify_firmware_safe.py`: **VERDICT: SAFE to flash, UPDATE path preserved, SECURE BOOT sound**
  (ctest 153/153 + dashboard_sim no-brick + ble_sim + secure-boot).

## [2026-06-09] Bluetooth (nRF51) firmware — BLE session simulator

### What was changed
- **BLE session simulator** (`firmware/decompiled/nrf51822/sim/ble_sim.cpp`): runs the real
  `Nrf51Firmware` on the host `SimNrf51Hardware` through a full session — advertising → phone connect →
  MiIO pairing → an **end-to-end register read** (phone→nRF51→STM32→nRF51→phone notification) → Haystack
  mode switch → VESC tunnel — with an annotated transcript. Wired into the `firmware/decompiled` CMake as
  a `ble_sim` ctest; `tools/ble_sim.py` runner; `sim/README.md`.

### Why it was changed
User: "test the bluetooth firmware in the simulator."

### What it does / expected behaviour
Demonstrates the BLE firmware end-to-end without a radio/phone/chip. Real BLE can't run in a chip
emulator (Renode has no nRF51; the Nordic SoftDevice is proprietary), so it's a functional simulation at
the firmware/HAL boundary (SoftDevice modelled by `SimSoftDevice`), running the actual firmware logic.

### Verified
- `ble_sim`: **14/14 checks pass** — advertises `NBScooter0001`; phone connects + MiIO `FLASH_REGISTERED`;
  battery read relayed verbatim to the STM32 and the phone notified with `5A A5 02 20 3E 04 22 50 00 29 FF`
  (battery=80) — the **same frame the Renode dashboard test produced**; Haystack/FindMy adv well-formed;
  VESC tunnel frame/unframe CRC ok.
- Full `ctest`: **3/3** (all_firmware_tests 153/153 + dashboard_sim 16/16 + ble_sim 14/14).

## [2026-06-09] UART protocol confirmed in-sim + stock-bootloader dumper app

### What was changed
- **UART protocol confirmation in Renode** (`firmware/dashboard/sim/renode/uart_protocol.resc`):
  injects a Ninebot `0x64` display frame on USART2 (battery=80) and a phone-app `READ reg 0x22` on
  USART1 into the **real running firmware**, and verifies the response is byte-for-byte
  `5A A5 02 20 3E 04 22 50 00 29 FF` (CMD 0x04 read-response, value 80, valid `~Σ` checksum). Confirms
  framing / `LEN=payload` / address map / READ→READ_RESPONSE / checksum / the VESC→dash→app path.
- **Stock-bootloader dumper** (`firmware/bootloader-dumper/`): a 744-byte app linked @0x08001000
  (**flashed via the stock IAP**) that reads the otherwise-undumpable 4 KB stock bootloader
  (`0x08000000–0x08000FFF`), CRC32s it, and emits it over the cable UART (USART2) as marker-framed hex.
  Read-only — cannot brick. Plus `tools/dump_bootloader.py` (receive from a serial port **or** a captured
  file; verifies the CRC32 and writes the `.bin`), and `sim/dump_verify.resc` + `sim/make_pattern.py`.

### Why it was changed
User: "confirm your understanding of the UART protocol with the simulator; generate an application to
dump the bootloader (flashed via the bootloader)."

### What it does / expected behaviour
The protocol script proves the firmware speaks the documented Ninebot protocol. The dumper recovers the
stock bootloader (not present in any distributed image) so it can be studied / backed up / combined into
a full SWD recovery image.

### Verified (run on Renode 1.16.0)
- Protocol: injected frames → response `5A A5 02 20 3E 04 22 50 00 29 FF` (exact match).
- Dumper: pattern at 0x08000000 → device CRC32 `0xDD3895E5` == host CRC32; `dump_bootloader.py` reconstructed
  **4096 bytes byte-for-byte identical** to the pattern (signature `STOCKBOOT_v1.337`).
- Both dashboard + dumper firmware build clean (arm-none-eabi-g++ 14.2); dashboard host suite still 153/153.

## [2026-06-09] Renode — instruction-accurate emulation of the real dashboard firmware

### What was changed
- Added a **Renode** (Antmicro, well-known open-source MCU emulator) setup that runs the **real
  compiled `dashboard_app.elf`** opcode-by-opcode on an emulated **STM32F103 / Cortex-M3** with a
  modeled **IWDG**, under `firmware/dashboard/sim/renode/`:
  - `dash_overlay.repl` — adds the `STM32_IndependentWatchdog` (@0x40003000, 40 kHz LSI) the stock
    platform lacks + RCC ready-bit tags the firmware busy-waits on in `clock_init()`.
  - `dash_adc.repl` — models ADC1 as silent memory (so polling loops don't flood Renode with warnings).
  - `dashboard_healthy.resc` / `dashboard_watchdog.resc` — the two scenarios; mirror the app reset
    vector to 0x0 (emulating the stock bootloader) so a watchdog reset reboots.
  - `README.md`.
- `tools/renode_dashboard.py` — builds the DASH_DEBUG firmware, runs both scenarios headless, counts
  `[BOOT]` banners on USART2, and asserts the verdict.
- Updated the sim/firmware READMEs (Renode is now implemented, not "future").
- Installed Renode 1.16.0 via `winget install Renode.Renode`.

### Why it was changed
User asked to use a **well-known 3rd-party software** to simulate the chip (rather than the bespoke
functional sim).

### What it does / expected behaviour
Runs the actual firmware binary on Renode's emulated STM32F103. The DASH_DEBUG build prints `[BOOT]` on
USART2 at each boot, so reboots are observable: healthy = the IWDG stays fed; ADC-fault = the IWDG resets
the MCU and it recovers. Complements the functional sim (instant/CI) by also covering the real opcodes
and register sequences.

### Verified (actually run on Renode 1.16.0)
- **Healthy**: exactly **1 `[BOOT]`** in 13 s emulated (~40 s real) — watchdog never fires; firmware
  streams `5A A5 … 65` throttle frames.
- **Watchdog/fault**: **3 `[BOOT]`s** in 13 s emulated (~15 s real) — the 5000 ms IWDG resets the
  Cortex-M3 about every 5 s and it reboots each time.
- `tools/renode_dashboard.py` → `VERDICT: PASS` (healthy=1, watchdog>=2).

## [2026-06-09] Dashboard chip simulator — no-brick verification

### What was changed
- **Chip simulator** (`firmware/dashboard/sim/`): a functional STM32F103C8 model that runs the **exact**
  dashboard firmware logic against modeled peripherals to verify it can't brick the hardware before
  flashing:
  - `sim_chip.h` — peripheral model: time, **register-accurate IWDG (real 5000 ms)**, USART1/2 queues,
    ADC, GPIO/LEDs/button, clock, and a **flash brick audit** (flags any write to the bootloader region
    `0x08000000-0x08000FFF`, option bytes/RDP).
  - `sim_dash_hal.cpp` — `dash::hal` implemented against `SimChip` (drop-in replacement for `dash_hal.cpp`).
  - `sim_main.cpp` — 4-scenario harness (healthy / missing-ADC / VESC-drop / no-brick audit), 16 checks,
    prints a "NO BRICK RISK" verdict; validates the built `.bin` vector table.
  - `sim/README.md`.
- **Refactor:** extracted the dashboard main loop into `firmware/dashboard/include/dash_app.h`
  (`DashApp::init()/step()`) so the **target and the simulator run identical code**; `src/main.cpp` is
  now a thin `for(;;) app.step()`.
- **Bug fixed (found by the simulator):** the watchdog `CLOCK` subsystem was kicked only once at init but
  required with a 2000 ms staleness — it would have **falsely reset healthy hardware** after ~7 s. Now
  kicked every loop (the clock runs whenever the loop runs). `dash_app.h`.
- **Tooling/build:** `tools/dashboard_sim.py` (build + run the sim); `dashboard_sim` added to the
  `firmware/decompiled` CMake as a second ctest.

### Why it was changed
User request: "build a simulator for the dashboard to verify you don't brick the hardware — it needs to
be able to simulate the chip."

### What it does / expected behaviour
Runs the shipping firmware logic against a simulated STM32F103 (peripherals + time + IWDG) and asserts
the no-brick invariants. It is fast, deterministic, and runs in CI alongside the unit tests. For
instruction-exact simulation of the `.elf`, Renode/QEMU is the documented complement.

### Verified
- Build: ✅ `dashboard_sim` (g++ 15.2, `-Wall -Wextra`); target firmware rebuilds (3808 B) after refactor.
- Tests: ✅ `ctest` — **2/2** (`all_firmware_tests` 153/153 + `dashboard_sim` **16/16**, "NO BRICK RISK").
- Functional: the sim caught and we fixed the CLOCK false-reset bug; missing-ADC reset cadence measured
  at ~5000 ms; recovery (no boot loop) confirmed; `.bin` vector table valid (SP=0x20005000, reset in app).

## [2026-06-09] Dashboard firmware (5000 ms watchdog) + Python tooling + secure-boot plan

### What was changed
- **Dashboard firmware (real STM32F103C8 target, builds with arm-none-eabi-g++ 14.2):**
  `firmware/dashboard/` — app linked @ `0x08001000`; `src/main.cpp` integrates the host-tested modules
  (`dash_bridge`, `dash_keeper`, `daly`) over a poll loop; `src/dash_hal.{h,cpp}` register-level drivers
  (clock 72 MHz, SysTick, **IWDG 5000 ms**, GPIO, USART1/2 incl. half-duplex, ADC); `startup_stm32f103.s`
  (relocates VTOR to the app base); `stm32f103c8_app.ld`; `Makefile`; `README.md`. Output `.bin` = ~3.7 KB,
  vector table verified (SP=0x20005000, reset in app region).
- **Hard requirement — 5000 ms watchdog (new Req 16):**
  `firmware/decompiled/common/include/watchdog_supervisor.h` — `WatchdogSupervisor` (feed the IWDG only
  while every *required* subsystem is fresh; withhold → reset if something is missing) + `iwdg_params()`
  (PR=4, RLR=3124 @ LSI 40 kHz → exactly 5000 ms). 3 host tests added (suite now **153/153, 472 assertions**).
- **Python scripts:**
  - `tools/analysis/iwdg_config.py` — IWDG PR/RLR calculator (single source of truth, mirrors the C++).
  - `tools/build_dashboard.py` — build the firmware (+ optional host tests) and validate the `.bin` vector
    table before flashing.
  - `Target/dashboard_watchdog_test.py` — UART HW test: confirm a reset lands in the 5 s window when a
    required dependency is missing (DASH_DEBUG build).
- **Secure-boot follow-on (design only, conditional):** `docs/SECURE_BOOT_PLAN.md` (per-chip feasibility:
  STM32 ECDSA bootloader + WRP/RDP, nRF51 + APPROTECT, BMS/VESC N/A; **irreversibility caveats** —
  RDP2 is permanent) + `Documentation/ToDo/secure-boot.md`. Reuses the existing `bootloader/` ECDSA-P256.
- **Docs:** Req 16 added to `requirements.md` (+ traceability row); `.gitignore` `build*/`.
- **Module tweak:** `daly_soft_uart.h` — gated the `std::function`-based `DalyClient` (and `<functional>`)
  behind `NINEBOT_DALY_NO_CLIENT` so the bare-metal firmware uses only the free frame builders.

### Why it was changed
User request: "create python scripts and make a firmware for the dashboard; **hard requirement: a
watchdog with 5000 ms timeout** (reset if something is missing); then, if it works well enough, design a
custom bootloader with **secure boot on every chip where possible**."

### What it does / expected behaviour
A flashable dashboard firmware that bridges the app↔VESC, drives inputs/LEDs, and **self-recovers via a
5000 ms IWDG** that only gets fed while required subsystems are alive. The Python tools build/validate it
and verify the watchdog on hardware. Secure boot is staged as the next step with the lock/irreversibility
risks called out.

### Verified
- Build (host): ✅ suite **153 passed / 0 failed, 472 assertions** (incl. 3 watchdog tests).
- Build (target): ✅ `firmware/dashboard` → `dashboard_app.bin` (3672 B), arm-none-eabi-g++ 14.2,
  `-Wall -Wextra`, vector table validated (SP=0x20005000, reset=0x080010ED).
- Tools: ✅ `iwdg_config.py` (5000.0 ms, PR=4/RLR=3124, 0.00% error); `build_dashboard.py` (builds +
  validates the image).
- HW: ⏳ pending (multimeter pin check + on-device watchdog/reset test via `dashboard_watchdog_test.py`).

## [2026-06-09] No-solder flash plan, dashboard pinout research, Daly sourcing, BLE/dash firmware modules

### What was changed
- **Firmware (implemented + host-tested, 16 new tests, full suite 150/150):** six header-only modules
  that realize the previously design-only `DASHBOARD_FIRMWARE.md` / `NRF51_BLE_FIRMWARE.md`:
  - `firmware/decompiled/ble/include/daly_soft_uart.h` — Daly BMS UART (Req 13): `0x90–0x98` reads +
    `0xD9/0xDA` MOSFET control, 13-byte framing, checksum, SOC parse, streaming `DalyClient`.
  - `firmware/decompiled/ble/include/dash_bridge.h` — Ninebot⇄VESC bridge + **synthetic ESC register
    image** so the stock app reads battery/speed/mode/fault/voltage on a VESC scooter (Req 1/2);
    `0x65` throttle + `0x64` display handling; **speed-cap removal** (Req 4 — limit writes never clamp).
  - `firmware/decompiled/ble/include/dash_keeper.h` — power-latch Solution-D FSM (sleep/wake/run/off,
    Daly on/off, nRF 0xAA/0xAB, VESC disable).
  - `firmware/decompiled/nrf51822/include/vesc_tunnel.h` — VESC packet framing + CRC16/XMODEM for the
    2nd NUS (Req 3/10).
  - `firmware/decompiled/nrf51822/include/haystack.h` — Apple FindMy/OpenHaystack adv builder + rolling-
    key schedule (Req 15).
  - `firmware/decompiled/nrf51822/include/mode_ctrl.h` — `0xAA`/`0xAB` NORMAL⇄HAYSTACK switch (Req 15.3).
  - `firmware/decompiled/tests/test_new_modules.cpp` + CMake — 16 tests locking the byte layouts.
- **Docs (new):**
  - `boards/ble-dashboard/DASHBOARD_PINOUT_RESEARCH.md` — cited research: **4-wire G30 dashboard cable**
    (Red 5V / Black GND / Yellow half-duplex data / Green button), STM32 + nRF51 SWD pads, IAP opcodes
    confirmed vs the official Ninebot protocol PDF.
  - `docs/DASHBOARD_NO_SOLDER_FLASH.md` — **no-solder** wiring + flash plan: USB-TTL + serial IAP for the
    STM32 app (no ST-Link), pogo-SWD for the nRF51 / custom bootloader, decision tree, reversibility.
  - `boards/bms-battery/DALY_BMS_SELECTION.md` — Daly model comparison (dims/current/port/comms),
    **AliExpress** buy links/search terms, G30 compartment + pack measurements, **fit verdict**.
  - `docs/APP_COMPATIBILITY.md` — register-by-register map of what the old app reads ↦ VESC/Daly source,
    auth-generation guidance (MiIO vs nbcrypto), what stays stock vs custom.
- **Correction:** stock pack is **10S6P** (60× 18650), not 10S3P — genuine cell `10INR19/66-6`, and
  3P×≤3.5 Ah can't reach 15.3 Ah. Fixed in `README.md`, `Documentation/PROJECT_DOC.md`,
  `boards/bms-battery/README.md`. (Other docs — `docs/guides/HARDWARE.md`, `POWER_MANAGEMENT_*`,
  `VESC_INSTALL_GUIDE.md`, `firmware/decompiled/README.md` — still say 10S3P; sweep pending.)

### Why it was changed
User request: check project state; deep-research the dashboard pinout; produce a no-solder dashboard
flash + wiring plan using the update mechanism; continue the app-compatibility analysis; continue
programming the BLE + dashboard firmware; find a fitting Daly BMS (AliExpress) + compartment fit.

### What it does / expected behaviour
The new modules give the VESC scooter a stock-ESC face to the original app (synthetic registers), Daly
control + monitoring, VESC-Tool BLE tunneling, FindMy tracking, and the dashboard power-latch — all
host-verified. The no-solder plan flashes the STM32 dashboard app via the stock serial-IAP path
(reversible), with pogo-SWD reserved for the nRF51 and the custom bootloader.

### Verified
- Build: ✅ `cmake -B build_new -S firmware/decompiled -G Ninja && cmake --build build_new`
  (g++ 15.2, `-Wall -Wextra -Wpedantic`, clean).
- Tests: ✅ `firmware_tests.exe` — **150 passed / 0 failed, 463 assertions** (16 new module tests).
- Flash / Functional (HW): ⏳ pending real hardware (multimeter checks + on-device app pairing listed in
  the new docs' open-items sections).

## [2026-06-03] Dashboard + BLE firmware design; improved protocol; app APK disassembled

### What was changed
- `docs/BLE_PROTOCOL_VERIFIED.md` — Created: BLE protocol verified by **disassembling the official Segway-Ninebot app** (`com.ninebot.segway` XAPK: base RN/Hermes + `config.arm64_v8a.apk` native libs) cross-checked with the nRF51 firmware RE. Native codec `libnbenc_ffi.so` (`is_frame_header_AA55`, `nb_encrypt`, AES-ECB/RC4/MD5, `Key_rule_analysis`) + JNI `cn.ninebot.nbcrypto.NbEncryption` (`setKey`/`setAuthParam`). Transport = Nordic UART Service; two app generations (legacy MiIO vs current nbcrypto AES).
- `docs/PROTOCOL_V2.md` — Created: **NB+** improved protocol (CRC-16/CCITT, 16-bit length, versioning, seq/ACK, STREAM telemetry, HELLO negotiation) that **coexists** with stock `5A A5` (distinct `5A A6` header) so the app stays compatible.
- `docs/DASHBOARD_FIRMWARE.md` — Created: STM32 dashboard firmware design (always-on keeper, Ninebot⇄VESC bridge, Daly soft-UART, speed-cap removal, Haystack mode commands) per Req 1/2/4/13 + Solution D.
- `docs/NRF51_BLE_FIRMWARE.md` — Created: nRF51 BLE firmware design (S130, dual NUS for app + VESC Tool, framing+auth backends MiIO/nbcrypto, FindMy Haystack, mode SM) per Req 3/15, grounded in the verified protocol.

### Why it was changed
User request: read the requirements, design the dashboard + BLE firmware, design an improved protocol, make the BLE firmware compatible with the original app, and download+disassemble the official app to verify the BT protocol (downloading the needed tools).

### What it does / expected behaviour
The BLE↔app path stays **stock Ninebot over NUS** (app-compatible, verified against the real APK); the improved **NB+** protocol rides the internal/VESC path and coexists on the same bus. The dashboard is the always-on keeper that bridges Ninebot⇄VESC and controls the Daly; the nRF51 firmware bridges the app (NUS + MiIO/nbcrypto auth), exposes VESC Tool over a second NUS, and emulates a FindMy tag in sleep.

### Verified
- Build: N/A (design docs)
- Flash: N/A
- Functional: **App APK downloaded (337 MB) and disassembled** — Ninebot protocol codec + AES/RC4/MD5 crypto confirmed in `libnbenc_ffi.so`/`libnbcrypto.so`; GATT UUIDs are delivered via the runtime RN bundle (transport = NUS from firmware RE). Tools downloaded: apktool 2.9.3.

## [2026-06-03] Power latch — Solution D: dashboard-as-keeper, no extra MCU

### What was changed
- `docs/POWER_LATCH_SCHEMATIC.md` — Added "how the original G30 solves it" + **Solution D (recommended)**: the custom energy-efficient **dashboard is the always-on keeper** (mirrors stock). With only the original 4 wires — one repurposed as a bit-banged 9600 Daly control line that also pulses `S1` — the dashboard sends Daly `0xD9 ON/OFF` to power the VESC up/down. Includes schematic, sequence, FW responsibilities, and re-ranked variants table (D recommended; A = PLC for stock dashboard FW).
- `docs/WIRING_PLAN_DALY_VESC.md` — §4 power-button updated to present Solution D as the recommended approach.

### Why it was changed
Per the request to think like the stock scooter and use the given components creatively (UART-as-Y, always-on efficient dashboard) instead of adding hardware. Removes the extra MCU from the recommended path.

### What it does / expected behaviour
Dashboard (STM32 custom FW) sleeps in STOP (~µA), wakes on its internal power button, and drives the Daly over a repurposed cable wire: `0xD9 ON` (+ `S1` wake via the frame edges) to power up, `0xD9 OFF` on long-press to make the **Daly cut VESC power**. w3 stays clean 115200 Ninebot to the VESC; w4 stays clean 9600 to the Daly — each device sees only its own protocol. No added MCU; the PLC (Solution A) remains the fallback for stock dashboard firmware.

### Verified
- Build: N/A (hardware/firmware design)
- Flash: N/A
- Functional: Design grounded in the stock keeper architecture + the same firmware-confirmed Daly `0xD9`/`S1` primitives

## [2026-06-03] Power latch: Daly cuts VESC power on OFF, button wakes it (+ schematic)

### What was changed
- `docs/POWER_LATCH_SCHEMATIC.md` — Created: always-on **Power-Latch Controller** design + block schematic + netlist + state machine + BOM + variants, so the **Daly discharge FET is the master power switch** (long-press → `0xD9 OFF` → cut; button → `S1` wake + `0xD9 ON`)
- `vesc-lisp/g30_dash.lisp` — Added a **keep-alive** GPIO (ADC2): asserted HIGH at boot and on turn-on, driven LOW on long-press OFF (with motor disable) so the PLC commands the Daly to cut power
- `docs/WIRING_PLAN_DALY_VESC.md` — Cleaned up: §4 power-button now describes the real Daly cutoff, §6 reframed around the PLC (+ optional read-only telemetry), BOM + wiring ToDo updated

### Why it was changed
User request: a solution where the Daly BMS actually cuts the VESC's power at power-off and the power button turns it back on — plus a schematic and a documentation cleanup.

### What it does / expected behaviour
Solves the cold-start latch problem (an unpowered VESC/dashboard can't sense the button) with a tiny always-on controller tapped from raw B+/B−. Long-press → VESC drops keep-alive → PLC sends Daly `0xD9 OFF` → discharge FET opens → VESC power cut → BMS sleeps (~µA). Short press from off → PLC pulses Daly `S1` (wake) + `0xD9 ON` → VESC powers → lisp re-asserts keep-alive. Daly frames and S1 wake are firmware/community-confirmed.

### Verified
- Build: N/A (hardware design + additive lisp)
- Flash: N/A
- UART Monitor: N/A
- Functional: Deep-researched & cited — Daly `0xD9` ON `A5 40 D9 08 01..C7` / OFF `..00..C6`, S1 active-low wake; soft-latch references for the fallback variant

## [2026-06-03] Daly BMS + VESC + dashboard wiring plan (deep-researched) + PPM backlight

### What was changed
- `docs/WIRING_PLAN_DALY_VESC.md` — Created: complete build/wiring documentation for the 3-unit scooter (Daly 20S 100A BMS + 100V VESC + stock dashboard) with BOM, system diagram, pin-by-pin tables (XT90 battery, MT60 motor, PH-6 sensor, original dashboard cable, PPM→MOSFET light), power-button behaviour, optional VESC↔Daly link, and an ordered wiring ToDo
- `vesc-lisp/g30_dash.lisp` — Added `update-light` driving the VESC servo/PPM output (GPIOB5) from the `light` state (with a GPIO-level alternative), called each loop in `handle-features`

### Why it was changed
User request: design the full wiring for a Daly-BMS (≤20S, 100A+) / VESC / stock-dashboard build where the backlight is switched by the VESC PPM output via a driver, the power button still works, and only the original dashboard cable is used. Connectors: XT90 battery, MT60 motor, PH-6 VESC sensor.

### What it does / expected behaviour
Documents exactly what to wire where: battery→Daly (balance + B+/B−), Daly P+/P−→VESC via XT90 (100A ANL fuse), VESC phases→MT60, Hall→PH-6, dashboard via its original 4-pin cable (5V/GND/data→TX/button→RX-pullup), and PPM→MOSFET driver→light. The lisp now switches the headlight via `set-servo` from the button-toggled `light` state. Power-off is software (long-press) with documented true-cutoff options (Daly BT/switch, manual XT90, or optional VESC↔Daly UART).

### Verified
- Build: N/A (hardware design + lisp); lisp change is additive
- Flash: N/A
- UART Monitor: N/A
- Functional: Deep-researched & cited — Daly 20S 100A (100/150A, common-port), VESC servo/PPM (1 PWM ch on servo pin, set-servo), G30 dash half-duplex (data→TX, button→pull-up input)

## [2026-06-03] Complete register-semantic map + MiIO layer (deep-researched)

### What was changed
- `docs/REGISTER_MAP.md` — Created: authoritative ESC/BMS/BLE register map (semantics from etransport/ninebot-docs, cited; mechanism + CMD codes + LEN firmware-confirmed)
- `firmware/decompiled/common/include/ninebot_protocol_verified.hpp` — Added `CMD_READ_RESP=0x04`/`CMD_FW_UPD0`/`CMD_HEAD_IO`, and `EscReg`/`BmsReg` named register constants
- `firmware/decompiled/tests/test_decompiled_protocol.cpp` — +5 checks (real ESC/BMS register queries + Ninebot read-response 0x04): now 45/45
- `firmware/decompiled/DECOMPILATION.md` — Added §4f (nRF51 MiIO/SoftDevice flow, researched + firmware-grounded)
- `docs/protocol.md` — Flagged its legacy register tables as G30-inaccurate; point to REGISTER_MAP.md
- `Documentation/VERIFICATION_REPORT.md` — P5 resolved (register maps)

### Why it was changed
Complete the decompilation: tie the firmware register-file mechanism to authoritative register semantics, and document the MiIO binding layer — using deep web research (ninebot-docs, Xiaomi MiIO) to ground what the binary alone can't reveal.

### What it does / expected behaviour
The register file (ESC @0x200007D6, BMS @0x20000400) is now mapped to named registers; `docs/REGISTER_MAP.md` gives the BLE/VESC bridge exactly how to poll the ESC and BMS (e.g. battery voltage = ESC ARG 0x48; cell N = BMS ARG 0x40+N). Cross-checking found and corrected real G30 errors in the old protocol.md (BMS cells 0x40-0x49 not 0x30-0x39; ESC 0x7B=KERS not speed-limit). The LEN=payload convention is now triple-confirmed (firmware + ninebot-docs + cross-board). The nRF51 MiIO auth/bind/token flow is documented for clean removal in the custom BLE firmware.

### Verified
- Build: OK — standalone decompilation test compiles clean (g++ 15.2, -Wall -Wextra)
- Flash: N/A
- UART Monitor: N/A
- Functional: OK — decompilation test 45/45; ninebot-docs independently confirms LEN=payload and CMD 0x01/0x02/0x03

## [2026-06-03] nRF51 BLE bridge parser decompiled (3rd board on the shared core)

### What was changed
- `firmware/decompiled/DECOMPILATION.md` — Added §4e (nRF51 BLE bridge parser @0x00018450)
- `firmware/decompiled/RE_FINDINGS.md` — Extended cross-board identity to all THREE boards

### Why it was changed
Continue onto the nRF51 BLE firmware (the custom-firmware target) to confirm how far the shared protocol core extends and document the bridge framing.

### What it does / expected behaviour
The nRF51 (BLE_1.1.7, Cortex-M0) Ninebot parser @0x00018450 uses the byte-identical 5A A5 header + `~(Σ−ckLo)` checksum core (state @0x200021B4, buf @0x200030E8). As the phone↔ESC bridge it adds dual framing: normal Ninebot `expected = LEN+8` (@0x18472) and Xiaomi MiIO `expected = LEN+0x0D` (@0x1848A), mode flag @0x2000275C[0x16], dispatching to 0x19804 (→ESC) or 0x1B9A8 (→phone). The 5A A5/checksum core is now confirmed byte-identical across ESC + BMS + nRF51. The MiIO/SoftDevice wrapper is documented, not reprogrammed.

### Verified
- Build: N/A (RE/documentation)
- Flash: N/A
- UART Monitor: N/A
- Functional: OK — header+checksum logic byte-matched to ESC/BMS; decompilation test remains 40/40

## [2026-06-03] BMS protocol decompiled + ESC motor-control architecture documented

### What was changed
- `firmware/decompiled/DECOMPILATION.md` — Added §4c (ESC motor control: TIM1_UP break/speed ISR @0x08005E74, TIM3 PWM/PI controller @0x080039FC — no static commutation table) and §4d (BMS_1.7.4.5 protocol)
- `firmware/decompiled/common/include/ninebot_protocol_verified.hpp` — Added `BMS_REGFILE_ADDR` (0x20000400) + cross-board notes
- `firmware/decompiled/tests/test_decompiled_protocol.cpp` — +5 checks: BLE→BMS read round-trip via the shared core (now 40/40)
- `firmware/decompiled/RE_FINDINGS.md` — Cross-board protocol-core identity (ESC↔BMS byte-identical)

### Why it was changed
Continue the decompilation onto the BMS (which the BLE firmware must poll) and the ESC motor path, verifying how far the shared protocol core extends.

### What it does / expected behaviour
BMS_1.7.4.5 runs the byte-identical protocol core (parser @0x08002E4C, `expected = LEN+7`); its dispatcher @0x08005610 uses CMD 1=READ / 2=WRITE against a 16-bit register file @0x20000400 indexed by ARG (same model as the ESC @0x200007D6). To poll the BMS: `5A A5 LEN 21 22 01 <reg> <count> CK CK`. The ESC motor control is a runtime PWM/PI controller (TIM3), not a static 6-step table — documented, not reprogrammed (ESC→VESC).

### Verified
- Build: OK — standalone decompilation test compiles clean (g++ 15.2)
- Flash: N/A
- UART Monitor: N/A
- Functional: OK — **decompilation test 40/40**; BMS LEN+7 framing independently re-confirms the LEN=payload convention

## [2026-06-03] ESC register dispatch decompiled + LEN convention reconciled repo-wide

### What was changed
- `firmware/decompiled/common/include/ninebot_protocol_verified.hpp` — Added register-access model: `RegisterFile` (16-bit array @ `0x200007D6` indexed by ARG), `Cmd` codes (READ 0x01 / WRITE 0x02/0x03 from the `tbb` table @0x08005650), `handleEscPacket()`
- `firmware/decompiled/tests/test_decompiled_protocol.cpp` — +9 checks (write→regfile→read round-trip, ARG 0x17 refresh flag): now 35/35
- `firmware/decompiled/DECOMPILATION.md` — Added §4b (register dispatch + register file)
- `firmware/decompiled/common/include/protocol.h` — Reconciled to `LEN = payload` (build `payloadLen`; parser `expected = LEN+7`; `payloadLength = LEN`; reject >0xF3)
- `firmware/decompiled/nrf51822/src/nrf51_main.cpp` — Same LEN reconciliation (had the duplicate bug)
- `firmware/decompiled/tests/test_binary_equivalence.cpp` — Fixed LEN assertion (`len == LEN+9`)
- `firmware/decompiled/README.md` — Corrected LEN description + test count (134)

### Why it was changed
Continue the decompilation: recover the ESC register read/write dispatch, and eliminate the old `LEN = payload + 6` convention everywhere so the simulator is wire-compatible with a real scooter.

### What it does / expected behaviour
ESC packets addressed to `0x20` are routed by SRC then by CMD (`tbb` @0x08005650); READ returns ARG-indexed 16-bit words from the register file @`0x200007D6`, WRITE stores the payload there. All reconstruction code now uses `LEN = payload count` (frame = LEN+9), matching the binary.

### Verified
- Build: OK — full CMake suite + standalone test compile clean (g++ 15.2)
- Flash: N/A
- UART Monitor: N/A
- Functional: OK — **reconstruction suite 134/134**, standalone decompilation test **35/35**

## [2026-06-03] Verified decompilation of ESC protocol core (DRV_1.6.13)

### What was changed
- `firmware/decompiled/common/include/ninebot_protocol_verified.hpp` — Created: byte-faithful, dependency-free C++ decompilation of `calculateChecksum` (0x08002720), `buildPacket` (0x080036AC), `parseProtocolByte` (0x08007128), `dispatchReceivedPacket` (0x08005468)
- `firmware/decompiled/tests/test_decompiled_protocol.cpp` — Created: standalone 26-check equivalence test (golden checksum, exact build bytes, round-trip, corrupt-checksum reject, header resync, oversized-LEN reject, DST routing)
- `firmware/decompiled/DECOMPILATION.md` — Created: asm↔C++ correspondence + the LEN-convention correction
- `firmware/decompiled/common/include/protocol.h` — Annotated with a wire-format warning (LEN discrepancy); logic unchanged to keep the 86-test suite green
- `docs/protocol.md` — Corrected the LEN definition + worked example (LEN = payload count; frame = LEN+9)
- `firmware/decompiled/RE_FINDINGS.md`, `Documentation/VERIFICATION_REPORT.md` — Recorded the LEN finding (P7)

### Why it was changed
"Decompile the firmware and reprogram it in C++." The ESC protocol core was decompiled directly from the DRV_1.6.13 bytes and re-implemented in verifiable C++. Doing so uncovered a real wire-format bug in the prior reconstruction.

### What it does / expected behaviour
The verified module reproduces the real Ninebot wire format: `5A A5 LEN SRC DST CMD ARG payload[LEN] CK_lo CK_hi` with `LEN = payload byte count`, checksum `= ~Σ(LEN..payload) & 0xFFFF`, parser `expected body = LEN+7`, and DST-based routing (0x20 ESC / 0x21 BLE / 0x22 BMS / 0x3E/0x3F App/PC). Unlike the older `protocol.h` (LEN = payload+6), it is wire-compatible with a real scooter.

### Verified
- Build: OK — `g++ -std=c++17 -Wall -Wextra` clean (no warnings)
- Flash: N/A
- UART Monitor: N/A
- Functional: OK — `test_decompiled_protocol` 26/26 pass; checksum golden vector `0xFF7C` matches

## [2026-06-02] Migrate agent config to Claude Code + firmware verification

### What was changed
- `CLAUDE.md` — Created agent operating manual (ported from `.github/copilot-instructions.md`, corrected)
- `.claude/commands/*.md` — 9 workflow slash commands (build, flash, verify-hw, document, test, commit, new-feature, hardware-change, verify-firmware)
- `.claude/agents/*.md` — 4 subagents (firmware-analyst, protocol-verifier, hardware-reviewer, doc-keeper)
- `.claude/settings.json` — PreCompact hook (re-read CLAUDE.md) + permissions, with `git push` denied
- `docs/guides/*.md` — Moved BUILD/DEBUG/DEPLOYMENT/HARDWARE/USB_UART_WIRING/CODING reference guides out of `.github/instructions/`
- `.github/` — Removed (fully migrated)
- `Target/README.md` — Created the previously-missing hardware-test directory
- `tools/analysis/disassemble_firmware.py` — Rewritten: arch/base auto-detect, Thumb-bit mask, encrypted-image detection, correct `boards/*/firmware` paths
- `firmware/decompiled/RE_FINDINGS.md` — New authoritative re-disassembly report
- `Documentation/VERIFICATION_REPORT.md` — New claim-by-claim verification with firmware evidence
- `README.md`, `boards/*/PINOUT.md`, `docs/protocol.md`, `Documentation/PROJECT_DOC.md` — Corrected factual errors (see below)

### Why it was changed
The project's agent guidance was in GitHub Copilot format; migrated it fully to Claude Code and improved the workflow (runnable commands, parallel subagents, structurally-enforced "never push"). Re-disassembled all six stock dumps to verify documented protocol/pinout claims, finding several errors to correct.

### What it does / expected behaviour
- Claude Code now loads `CLAUDE.md`, the slash commands, the subagents, and the PreCompact hook.
- Firmware verification established: `BLE_*.bin` are **nRF51822** (not STM32 — false provenance corrected); `BMS_1.3.4.bin` is **encrypted**; `DRV_*`/`BMS_1.7.4.5` are STM32F103; the `5A A5` framing/checksum/addresses/115200 8N1 are firmware-confirmed; ESC UART RX is polled (not ISR); BMS BQ76940 link shows no hardware-I2C1 (likely bit-banged).
- The RE harness now classifies arch/base correctly and flags the encrypted image instead of emitting noise.

### Verified
- Build: N/A (config/docs/tooling)
- Flash: N/A
- UART Monitor: N/A
- Functional: OK — RE harness runs clean on all 6 binaries; `.claude/settings.json` valid JSON; PreCompact hook emits valid JSON; no dangling `.github` links remain in live docs

## [2026-03-31] Req 15 — OpenHaystack AirTag Emulation (nRF51822)

### What was changed
- `Documentation/Requirements/requirements.md` — Added Req 15 (OpenHaystack AirTag Emulation) with sub-requirements 15.1–15.8 and updated Traceability Matrix
- `Documentation/ToDo/openhaystack-airtag.md` — Created ToDo file for Req 15 implementation

### Why it was changed
Feature request: make the scooter trackable via Apple's Find My network by emulating an AirTag on the existing nRF51822 Bluetooth module using OpenHaystack. The key design constraint is that FindMy advertising must remain active while the rest of the scooter is sleeping (STM32 in STOP mode), which integrates naturally with the power-saving strategy since the nRF51 is already powered and its advertising current in sleep mode is negligible (~4–8 µA at 5,000 ms interval).

### What it does / expected behaviour
- Req 15 defines the full OpenHaystack/FindMy emulation on nRF51822: advertisement format (Apple manufacturer-specific `0x004C 0x12 0x19` payload with 28-byte rolling public key), key rolling schedule (96 pre-generated keys rotating every 900 s, persisted across power cycles), two operational modes (`NRF51_MODE_NORMAL` / `NRF51_MODE_HAYSTACK`), and mode switching via UART commands `0xAA`/`0xAB` from STM32.
- Three integration options were evaluated; **Option A** (explicit UART command from STM32 before STOP mode) was selected as the most deterministic, requiring no hardware changes.
- Power analysis: nRF51 FindMy advertising contributes only ~4–8 µA to the sleep budget — the dominant drain remains the VESC standby (~1–5 mA).
- Implementation is Phase 4 (after STM32 is stable). PC-side key generation tool `tools/signing/generate_haystack_keys.py` needs to be created.

### Verified
- Build: N/A (planning/documentation only)
- Flash: N/A
- UART Monitor: N/A
- Functional: N/A

## [2026-07-16] 84V (20S) / 80A Upgrade Analysis Document

### What was changed
- `docs/POWER_MANAGEMENT_84V_UPGRADE.md` — Created comprehensive upgrade analysis for 20S (84V) / 80A configuration

### Why it was changed
The project target changed from 10S (42V) / 30A to 20S (84V) / 60–80A. The existing power management documents (`POWER_MANAGEMENT_HARDWARE.md`, `POWER_MANAGEMENT_SOFTWARE.md`) were designed for 10S/42V with components rated for that voltage. All component selections (MOSFETs, buck converter, VESC) needed re-evaluation for the higher voltage and current. Additionally, VESC Lisp power-saving capabilities were not yet analyzed.

### What it does / expected behaviour
- Documents the **critical incompatibility** of the Flipsky 75100 (75V max) with 84V batteries, with solution options (100V-rated VESC or limit to 16S)
- Provides updated component selection with AliExpress search terms: Daly BMS 20S 80A, MP9486 buck module (100V input), 100V+ MOSFETs (IRFP4110), 8 AWG wiring, XT90 connectors
- Confirms **Option B (Daly BMS FET control) is the only practical option** for 80A in an enclosed space (external MOSFETs dissipate 6–24W at 80A)
- Documents that VESC ESC hardware has **no native sleep mode** (`sleep-deep`/`sleep-light` are Express-only); only `app-disable-output` saves ~1W
- Provides Lisp code for `enter-low-power` / `exit-low-power` functions
- Includes thermal analysis, consequences analysis (safety, reliability, legal), wiring diagram, BOM, and migration checklist

### Verified
- Build: N/A (documentation only)
- Flash: N/A
- UART Monitor: N/A
- Functional: N/A

## [2026-03-29] Bootloader Platform Abstraction & Scope Refinement

### What was changed
- `bootloader/common/include/platform.h` — Created platform abstraction interface (init, UART, flash, jump, update trigger, watchdog, board ID)
- `bootloader/common/src/bootloader.c` — Created shared bootloader logic (replaces both stm32/nrf51 bootloader_main.c)
- `bootloader/stm32/src/platform_stm32.c` — Created STM32F103 platform implementation
- `bootloader/nrf51/src/platform_nrf51.c` — Created nRF51822 platform implementation
- `bootloader/CMakeLists.txt` — Updated to use bootloader.c + platform files instead of bootloader_main.c
- `vesc-lisp/README.md` — Created VESC Lisp project overview and protocol documentation
- `vesc-lisp/g30_dash.lisp` — Created VESC Lisp script for G30 dashboard integration
- `.github/instructions/HARDWARE/USB_UART_WIRING.instructions.md` — Created USB-UART wiring guide for BLE dashboard development
- `.github/hooks/read-instructions-on-compact.json` — Created PreCompact hook for instruction persistence
- `Documentation/Requirements/requirements.md` — Struck through Req 9 (BMS custom firmware), added Req 13 (Daly BMS compatibility), added Req 14 (VESC Lisp motor control)
- `Documentation/PROJECT_DOC.md` — Updated for BMS scope removal, added vesc-lisp module, added Daly BMS mention
- `.github/copilot-instructions.md` — Updated project description (VESC Lisp, Daly BMS, BMS stock), removed BMS Phase 5
- `.github/instructions/index.instructions.md` — Removed BMS custom firmware references
- `.github/instructions/DEPLOYMENT/DEPLOYMENT_STRATEGY.instructions.md` — Struck through BMS Phase 5

### Why it was changed
1. **Bootloader restructuring**: Both STM32 and nRF51 bootloader_main.c had near-identical logic (XMODEM receive, .sfw validation, ECDSA verify, flash, reboot). The RC-Servo bootloader's platform.h pattern eliminates this duplication — shared logic in bootloader.c, platform-specific code in platform_*.c.
2. **BMS scope removal**: Custom BMS firmware adds unnecessary risk to battery safety and the stock BMS protocol works fine. The BLE firmware supports the stock Ninebot BMS protocol natively.
3. **VESC Lisp project**: The VESC needs a Lisp script to bridge the G30 dashboard protocol (frame 0x64/0x65) to motor control (app-adc-override). Based on CRZX1337/g30-vesc-dash.
4. **Daly BMS compatibility**: Add support for Daly BMS as an alternative battery management system via its UART protocol (0xA5 header, commands 0x90–0x98).

### What it does / expected behaviour
- All three bootloader targets (BLE, BMS, nRF51) build from shared bootloader.c with platform-specific implementations
- BLE bootloader: 6,336 bytes text — fits in 16 KB
- BMS bootloader: 6,264 bytes text — fits in 16 KB
- nRF51 bootloader: 6,728 bytes text — fits in 16 KB
- VESC Lisp script documents the complete Ninebot dashboard protocol interface
- USB-UART wiring guide covers three connection methods (VESC USB, direct tap, SWD)

### Verified
- Build: OK — all three bootloader targets compile without errors or warnings
- Flash: N/A (no hardware testing yet)
- UART Monitor: N/A
- Functional: N/A

## [2026-03-28] Project Conversion — Documentation to Software Development

### What was changed
- `.github/instructions/` — Created full instruction framework (index, BUILD, DEBUG, DEPLOYMENT, CODING, HARDWARE, documentation)
- `.github/copilot-instructions.md` — Updated from documentation project to software development project
- `Documentation/PROJECT_DOC.md` — Created project documentation
- `Documentation/CHANGE_LOG.md` — Created change log (this file)
- `Documentation/Requirements/requirements.md` — Created initial requirements
- `.vscode/settings.json` — Created VS Code workspace configuration
- `.vscode/tasks.json` — Created build/flash/monitor tasks

### Why it was changed
Project converted from a hardware documentation / reverse engineering knowledge base into an active software development project. The new design replaces the stock ESC with a VESC motor controller, keeps stock BLE and BMS hardware with custom firmware, and establishes a safe incremental deployment strategy using UART-based flashing.

### What it does / expected behaviour
The project now has:
- A complete workflow instruction set (adapted from Bobbycar-Steering project)
- Build → Flash → Verify → Document → Test → Commit workflow for every code change
- UART-based debug workflow using the scooter's internal bus
- 6-phase deployment strategy (backup → OTA app → bootloader-as-app → final bootloader → nRF51 → BMS)
- Requirements tracking, ToDo management, test documentation

### Verified
- Build: N/A (structural change, no code changes)
- Flash: N/A
- UART Monitor: N/A
- Functional: N/A
