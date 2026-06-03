# Change Log — Ninebot G30 Max Custom Firmware

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
