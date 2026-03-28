---
applyTo: "**/*.c,**/*.h"
---

# Coding Conventions — Function Comments & Side Effects

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
