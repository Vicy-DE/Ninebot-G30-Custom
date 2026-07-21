---
description: Re-disassemble the stock dumps and verify protocol/pinout claims
argument-hint: "[DRV_1.2.6|BLE_1.1.7|BMS_1.7.4.5|...]"
---

Run the arch-aware RE harness and reconcile findings with the docs.

1. `.venv/Scripts/python.exe tools/analysis/disassemble_firmware.py $ARGUMENTS` (omit arg for all 6; add `--json` for machine output).
2. Confirm per binary: detected arch/base, `5A A5` header parser presence, peripheral literal refs,
   115200 BRR words, encrypted-image flagging.
3. Compare against `docs/protocol.md` and `boards/*/PINOUT.md`. Any mismatch → update
   `Documentation/VERIFICATION_REPORT.md` (claim → evidence → PASS/FAIL/CORRECTED) and
   `firmware/decompiled/RE_FINDINGS.md`.
4. Remember the established ground truth: `BLE_*.bin` = nRF51 (not STM32); `BMS_1.3.4` = encrypted;
   DRV/BMS USART RX is polled, not ISR. Don't reassert refuted claims.
