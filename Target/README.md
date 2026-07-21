# Target/ — Hardware Integration & Test Scripts

This directory holds **on-hardware** test/debug scripts that talk to a real scooter board over UART.
It is referenced by the `/test` workflow command and `docs/guides/CODING.md`.

> This directory previously did not exist even though the workflow referenced it — created
> 2026-06-02 (see `Documentation/VERIFICATION_REPORT.md`, item S3).

## Conventions

- Naming: `test_<feature>.py` (Python) or `test_<feature>.ps1` (PowerShell orchestration).
- Use `serial.Serial`; expose `--port` / `--baud` arguments (default `COM3` / `115200`).
- Each test case prints `PASS` or `FAIL` with a reason; exit code `0` only on a full pass.
- Cover: happy path, error paths (invalid input / timeout / corrupt packets), boundary values,
  regression, Ninebot protocol compliance (header / checksum / addressing), and every affected board.
- Save run output + a report under `Documentation/Tests/<YYYY-MM-DD>_<feature>.*` (see `/test`).

## Distinction from `tools/`

- `tools/flasher/` — flashing & firmware transfer.    `tools/signing/` — ECDSA signing/keys.
- `tools/analysis/` — static firmware analysis (no hardware).    **`Target/` — live-hardware tests.**

_No test scripts yet — add them as features are implemented._
