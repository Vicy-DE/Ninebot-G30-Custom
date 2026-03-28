---
applyTo: "**/*.c,**/*.h,**/*.S,**/CMakeLists.txt"
---

# Requirements Documentation — Instructions for Copilot

## When to execute

**On every feature request — before writing any code.**

## Target file

`Documentation/Requirements/requirements.md`

---

## Requirement entry format

```markdown
## <N>. <Feature Name> — <Key Technology / Interface>

| Item | Detail |
|---|---|
| **Module / Component** | <bootloader / ble / bms / nrf51 / protocol / tools / etc.> |
| **Interface** | <UART / GPIO / SPI / I2C / BLE / XMODEM / ECDSA / N/A> |
| **Board** | <BLE STM32 / BMS STM32 / nRF51822 / VESC / All> |
| **Requirements** | <Detailed prose: what must be implemented, constraints.> |
```

---

## Traceability Matrix (end of file)

```markdown
## Traceability Matrix

| Req # | Feature | Depends On |
|---|---|---|
| N | <Feature short name> | <Req #, … or —> |
```

---

## Rules

- **MUST** update `requirements.md` before starting any implementation work.
- **MUST** assign the next available sequential requirement number.
- **MUST** update the Traceability Matrix in the same edit.
- **MUST** mark obsolete requirements with `~~strikethrough~~` and `*(removed YYYY-MM-DD)*`.
- **MUST NOT** change existing requirement numbers.
