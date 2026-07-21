# Coding Conventions — Ninebot G30 Max Custom

Detailed C comment/side-effect rules and script-organization rules for this repo.

---

## Part A — Function Comments & Side Effects


## RULE: Every Function Must Have a Documentation Comment

Every function — public or static — MUST have a Doxygen-style documentation comment immediately above the definition:

```c
/**
 * @brief One-line summary ending with a period.
 *
 * Optional longer description explaining behaviour, edge cases, or
 * algorithm notes.  Wrap at 80 columns.
 *
 * @param[in]     name   Description of input parameter.
 * @param[out]    name   Description of output parameter.
 * @param[in,out] name   Description of input/output parameter.
 * @return Description of return value.  Use "void" implicitly (omit @return).
 *
 * @sideeffects Modifies global `tick_ms`.
 *              Writes to USART1 TX register.
 */
```

### Comment Checklist

1. `@brief` — mandatory, one sentence, imperative mood ("Compute …", not "Computes …").
2. `@param` — one per parameter, tagged `[in]`, `[out]`, or `[in,out]`.
3. `@return` — describe what the return value means (omit for `void`).
4. `@sideeffects` — mandatory if the function is *not* side-effect free (see below).

---

## RULE: Functions Must Be Side-Effect Free Unless Tagged `_sideeffects`

A **side-effect free** function:
- Does NOT modify global / static / file-scope variables.
- Does NOT write to hardware registers (GPIO, UART, Flash, SysTick, etc.).
- Does NOT perform I/O (serial, flash, memory-mapped registers).
- May only read its parameters and return a value (+ use local variables).

### Naming Convention

| Function type | Naming rule | Example |
|---------------|-------------|---------|
| Side-effect free | Normal name | `crc16_xmodem()`, `sfw_validate_header()` |
| Has side effects | Append `_sideeffects` to the name **OR** use a well-known pattern | `flash_erase_page()`, `uart_init()` |

### Well-Known Side-Effect Exceptions

The following function name patterns are inherently understood to have side effects and do **not** need the `_sideeffects` suffix:

- `*_init`, `*_deinit` — initialization / teardown
- `*_Handler` — interrupt handlers (e.g., `SysTick_Handler`, `USART1_IRQHandler`)
- `main` — entry point
- `*_task` — FreeRTOS task functions
- `*_cb`, `*_callback` — callback functions
- `flash_*` — flash memory operations (erase, write, lock, unlock)
- `uart_*` — UART operations (send, receive, init)
- `gpio_*` — GPIO operations
- `i2c_*` — I2C operations
- `NVIC_*` — NVIC functions

Any **other** function with side effects that does not match the patterns above **MUST** be named with a `_sideeffects` suffix.

### When to Use `@sideeffects`

Even for well-known exception names, the `@sideeffects` tag in the doc comment is **always required** when side effects exist.

```c
/**
 * @brief Send a null-terminated string over UART.
 *
 * @param[in] s  Null-terminated string to transmit.
 *
 * @sideeffects Writes bytes to USART2 TX register.
 */
void uart_puts(const char *s);
```

---

## RULE: Header-File Declarations

Header files declare the public API. Each declaration MUST have a doc comment. The full `@param`/`@return`/`@sideeffects` block goes in the **header**, not repeated in the `.c` definition:

```c
/* In .h */
/**
 * @brief Validate a signed firmware header.
 *
 * @param[in] hdr        Pointer to parsed .sfw header.
 * @param[in] target_id  Expected target board ID.
 * @param[in] max_size   Maximum allowed firmware size.
 * @return SFW_OK if valid, error code otherwise.
 */
sfw_result_t sfw_validate_header(const sfw_header_t *hdr,
                                  uint8_t target_id,
                                  uint32_t max_size);
```

In the `.c` file, a shorter comment referencing the header is acceptable:

```c
/* See fw_header.h for full documentation. */
sfw_result_t sfw_validate_header(const sfw_header_t *hdr,
                                  uint8_t target_id,
                                  uint32_t max_size)
{
    ...
}
```


---

## Part B — Scripts


## RULE: Script Locations

Scripts are organized by purpose:

| Directory | Purpose | Examples |
|-----------|---------|----------|
| `tools/flasher/` | Firmware flashing and UART communication | `ninebot_flasher.py`, `nbu_send.py` |
| `tools/signing/` | Firmware signing and key management | `sign_firmware.py`, `generate_keys.py` |
| `tools/analysis/` | Firmware analysis and reverse engineering | `disassemble_firmware.py`, `analyze_bootloader.py` |
| `Target/` | Hardware test/debug scripts | `test_ble_boot.py`, `capture_protocol.py` |

**MUST:** Place new scripts in the appropriate directory based on their purpose.

### Exceptions (do NOT move)

| Path pattern | Reason |
|---|---|
| `bootloader/**` | Embedded source — not scripts |
| `firmware/**` | Firmware source — not scripts |
| `lib/**` | Library source — not scripts |

---

## RULE: Use Script-Relative Paths

When a script needs to reference files relative to the project root, use the script's own location as the anchor — **never** `os.getcwd()` or `$PWD`.

### Python

```python
import os
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.join(SCRIPT_DIR, "..", "..")
```

### PowerShell

```powershell
$ScriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
$ProjectRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
```

---

## RULE: Naming Convention

- Use `snake_case` for Python scripts: `ninebot_flasher.py`, `nbu_send.py`
- Use `kebab-case` for shell scripts: `build-all.ps1`
- Exception: legacy scripts may keep their established names

---

## RULE: Never Create Temporary Scripts in the Workspace Root

- Use the terminal directly for one-off commands
- If a diagnostic script is needed, place it in `tools/analysis/` with a `diag_` prefix

---

## tools/ Inventory

| Script | Directory | Language | Purpose |
|--------|-----------|----------|---------|
| `ninebot_flasher.py` | flasher | Python | Flash via Ninebot IAP protocol |
| `nbu_send.py` | flasher | Python | Flash via NBU (framed half-duplex) |
| `initial_flash.py` | flasher | Python | First-time SWD flash helper |
| `update_bootloader.py` | flasher | Python | Bootloader self-update |
| `sign_firmware.py` | signing | Python | Sign firmware with ECDSA-P256 |
| `generate_keys.py` | signing | Python | Generate ECDSA key pair |
| `verify_firmware.py` | signing | Python | Verify .sfw signature |
| `disassemble_firmware.py` | analysis | Python | Disassemble .bin files |
| `analyze_bootloader.py` | analysis | Python | Analyze bootloader binary |
| `deep_disassemble.py` | analysis | Python | Deep analysis of firmware |

## Target/ Inventory

| Script | Language | Purpose |
|--------|----------|---------|
| (to be created) | Python | Hardware integration tests |
