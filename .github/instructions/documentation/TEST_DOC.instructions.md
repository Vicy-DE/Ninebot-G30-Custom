---
applyTo: "**/*.c,**/*.h,**/*.S,**/CMakeLists.txt,**/*.bat,**/*.ps1,**/*.py,**/*.sh"
---

# Test Generation & Documentation — Instructions for Copilot

## When to execute

**After every implementation — before closing the task.**

---

## Step 1 — Generate tests

### Where to put test scripts
- Python hardware/integration tests → `Target/test_<feature>.py`
- PowerShell orchestration tests → `Target/test_<feature>.ps1`

### What to test — MANDATORY coverage

| Scope | What to verify |
|---|---|
| **Happy path** | Nominal input → expected output on hardware |
| **Error paths** | Invalid input, timeouts, corrupted packets |
| **Boundary conditions** | Min/max values, edge cases |
| **Regression** | Previously passing tests still pass |
| **Protocol compliance** | Ninebot protocol packet format, checksum, addressing |
| **Board coverage** | Test on all affected boards (BLE, BMS, nRF51) |

### Test script conventions
- Use `serial.Serial` for UART communication
- Add `--port` / `--baud` arguments (default 115200)
- Each test prints `PASS` or `FAIL` with a reason
- Exit code 0 on full pass, non-zero on any failure

---

## Step 2 — Execute tests

```powershell
py -3 Target/test_<feature>.py --port COM3 2>&1 | Tee-Object -FilePath "Documentation/Tests/<YYYY-MM-DD>_<feature>_results.txt"
```

**All tests MUST pass** before the task is considered done.

---

## Step 3 — Document the tests

Create a test report in `Documentation/Tests/`.

### File naming

```
Documentation/Tests/<YYYY-MM-DD>_<feature>.md
```

### Report format

```markdown
# Test Report — <Feature Name>
**Date:** YYYY-MM-DD
**Board:** BLE / BMS / nRF51 / All
**Firmware Version:** <version>
**Test Script:** `Target/test_<feature>.py`

## Summary

| Category | Total | Pass | Fail |
|----------|-------|------|------|
| Happy path | N | N | 0 |
| Error handling | N | N | 0 |
| Boundary | N | N | 0 |
| **Total** | **N** | **N** | **0** |

## Test Cases

### TC-01: <test name>
- **Input:** <what was sent>
- **Expected:** <what should happen>
- **Actual:** <what happened>
- **Result:** PASS / FAIL

## Raw Output
<paste terminal output>

## Remarks
<any observations, quirks, follow-up items>
```

---

## Rules

- **MUST** generate and run tests for every implementation task.
- **MUST** document results in `Documentation/Tests/`.
- **MUST** fix failures before closing the task.
- **MUST** include UART protocol tests for any protocol-related changes.
