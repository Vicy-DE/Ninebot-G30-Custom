---
description: Generate, run, and document hardware/integration tests
argument-hint: "<feature>"
---

Generate and run tests for `$ARGUMENTS`, then document results.

1. **Write** the test to `Target/test_<feature>.py` (use `serial.Serial`, `--port`/`--baud` args default 115200,
   each case prints PASS/FAIL + reason, exit 0 only on full pass). Cover: happy path, error paths
   (invalid input/timeout/corrupt packets), boundary values, regression, Ninebot protocol compliance
   (header/checksum/addressing), and all affected boards.
2. **Run**: `py -3 Target/test_<feature>.py --port <COM> 2>&1 | Tee-Object Documentation/Tests/<YYYY-MM-DD>_<feature>_results.txt`.
3. **Document**: write `Documentation/Tests/<YYYY-MM-DD>_<feature>.md` with the summary table, per-case
   Input/Expected/Actual/Result, raw output, and remarks.
4. All tests must pass before the task is done. Fix failures, don't hide them.
