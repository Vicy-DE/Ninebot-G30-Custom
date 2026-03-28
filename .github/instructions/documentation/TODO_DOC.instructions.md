---
applyTo: "**/*.c,**/*.h,**/*.S,**/CMakeLists.txt"
---

# ToDo Documentation — Instructions for Copilot

## When to execute

**Before starting any feature implementation — after updating requirements.**

## Target directory

`Documentation/ToDo/<feature-name>.md` (kebab-case filename)

---

## Template

```markdown
# ToDo — <Feature Name>

**Created:** YYYY-MM-DD
**Requirement:** Req #N (<link to requirements.md>)
**Board:** BLE / BMS / nRF51 / All
**Status:** Not Started / In Progress / Verified / Done
**Deployment Phase:** 0 / 1 / 2 / 3 / 4 / 5

## Tasks

- [ ] <Task 1 description>
  - [ ] Sub-task A
  - [ ] Sub-task B
- [ ] <Task 2 description>
- [ ] Build and fix all errors
- [ ] Flash to target board
- [ ] Verify via UART monitor
- [ ] Run tests (`Target/test_<feature>.py`)
- [ ] Update CHANGE_LOG.md
- [ ] Update PROJECT_DOC.md
- [ ] Save test report (`Documentation/Tests/`)
- [ ] Commit (never push)

## Notes

<Design decisions, assumptions, risks, reference to protocol docs or datasheets.>
```

---

## Rules

- **MUST** create a ToDo file before starting feature implementation.
- **MUST** include standard final tasks (build, flash, verify, document, test, commit).
- **MUST** tick checkboxes immediately when each task is completed (not batch at end).
- **MUST** include the deployment phase this feature belongs to.
- **MUST NOT** delete ToDo files when done — they serve as implementation history.
