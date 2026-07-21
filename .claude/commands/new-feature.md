---
description: Start a feature — write requirements + a ToDo file before any code
argument-hint: "<feature name>"
---

Before writing any code for `$ARGUMENTS`, set up tracking:

**1. `Documentation/Requirements/requirements.md`** — add the next sequential requirement:
```markdown
## <N>. <Feature> — <Key tech/interface>
| Item | Detail |
|---|---|
| Module / Component | <bootloader/ble/bms/nrf51/protocol/tools> |
| Interface | <UART/GPIO/SPI/I2C/BLE/XMODEM/ECDSA/N/A> |
| Board | <BLE STM32 / BMS STM32 / nRF51822 / VESC / All> |
| Requirements | <what must be implemented, constraints> |
```
Update the Traceability Matrix in the same edit. Never renumber existing requirements; mark obsolete ones `~~struck~~ *(removed YYYY-MM-DD)*`.

**2. `Documentation/ToDo/<feature-kebab>.md`** — create from the template: Created date, Requirement #,
Board, Status, Deployment Phase, task checklist (incl. build/flash/verify/test/document/commit), Notes.
Tick boxes as you go; never delete ToDo files (they are implementation history).
