/**
 * @file platform.h
 * @brief Platform abstraction layer for the secure bootloader.
 *
 * Defines the hardware-independent interface that each target platform
 * (STM32F103, nRF51822) must implement. The shared bootloader logic
 * in bootloader.c calls only these functions.
 *
 * Inspired by the RC-Servo bootloader platform.h pattern.
 *
 * Each platform provides:
 *   - Clock and peripheral initialization
 *   - UART send/receive (for XMODEM and status messages)
 *   - Flash erase/write (for firmware storage)
 *   - Application jump (set VTOR, MSP, branch)
 *   - Update trigger detection (button, flag, invalid app)
 *   - Watchdog feed (if applicable)
 *   - Board identification (target name, ID, max firmware size)
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Initialization / teardown ─────────────────────────────────────────── */

/**
 * @brief Initialize platform hardware.
 *
 * Clock configuration (HSE/PLL for STM32, HFCLK for nRF51),
 * SysTick (1 ms tick), GPIO clocks, and UART peripheral.
 * Called once at boot before any other platform function.
 *
 * @sideeffects Configures clocks, SysTick, GPIO, and UART peripherals.
 */
void platform_init(void);

/**
 * @brief De-initialize platform hardware before jumping to application.
 *
 * Disables SysTick, resets peripherals to default state.
 * Called immediately before platform_jump_to_app().
 *
 * @sideeffects Disables SysTick and de-configures UART.
 */
void platform_deinit(void);

/* ── UART I/O ──────────────────────────────────────────────────────────── */

/**
 * @brief Send a single byte over the update UART (blocking).
 *
 * @param[in] byte  Byte to transmit.
 *
 * @sideeffects Writes to UART TX register.
 */
void platform_uart_send_byte(uint8_t byte);

/**
 * @brief Receive a single byte from the update UART with timeout.
 *
 * @param[out] byte     Pointer to store received byte.
 * @param[in]  timeout  Timeout in milliseconds (0 = wait forever).
 * @return 0 on success, -1 on timeout.
 *
 * @sideeffects Reads from UART RX register.
 */
int platform_uart_recv_byte(uint8_t *byte, uint32_t timeout);

/**
 * @brief Send a null-terminated string over UART.
 *
 * @param[in] str  Null-terminated string to transmit.
 *
 * @sideeffects Writes to UART TX register.
 */
void platform_uart_puts(const char *str);

/* ── Flash operations ──────────────────────────────────────────────────── */

/**
 * @brief Erase the entire application flash region.
 *
 * @return 0 on success, -1 on error.
 *
 * @sideeffects Erases flash pages in the application region.
 */
int platform_flash_erase_app(void);

/**
 * @brief Write data to application flash.
 *
 * @param[in] addr  Destination address (must be properly aligned).
 * @param[in] data  Source data buffer.
 * @param[in] len   Number of bytes to write.
 * @return 0 on success, -1 on error.
 *
 * @sideeffects Writes to flash memory.
 */
int platform_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/* ── Update trigger ────────────────────────────────────────────────────── */

/**
 * @brief Check if a firmware update has been requested.
 *
 * Checks all update trigger sources:
 *   - Software flag in config/settings page
 *   - Hardware button held during boot (if applicable)
 *   - No valid application detected
 *
 * @return 1 if update mode should be entered, 0 otherwise.
 */
int platform_update_requested(void);

/**
 * @brief Clear the software update request flag.
 *
 * @sideeffects Erases/clears the update flag in flash config page.
 */
void platform_clear_update_flag(void);

/* ── Application validation and launch ─────────────────────────────────── */

/**
 * @brief Check if a valid application exists in flash.
 *
 * Verifies the application vector table: stack pointer in SRAM range,
 * reset handler in flash range.
 *
 * @return 1 if application appears valid, 0 otherwise.
 */
int platform_is_app_valid(void);

/**
 * @brief Jump to the application firmware.
 *
 * Sets the vector table offset, loads the application's stack pointer,
 * and branches to the reset handler. Does not return.
 *
 * @sideeffects Relocates VTOR, sets MSP, branches to app.
 */
void platform_jump_to_app(void) __attribute__((noreturn));

/**
 * @brief Get the application start address (flash base of app region).
 *
 * @return Application start address.
 */
uint32_t platform_get_app_start_addr(void);

/* ── Timing ────────────────────────────────────────────────────────────── */

/**
 * @brief Get the current system tick count in milliseconds.
 *
 * @return Milliseconds since boot (wrapping is acceptable).
 */
uint32_t platform_get_tick_ms(void);

/**
 * @brief Delay for specified milliseconds (blocking).
 *
 * @param[in] ms  Delay duration in milliseconds.
 */
void platform_delay_ms(uint32_t ms);

/* ── Watchdog ──────────────────────────────────────────────────────────── */

/**
 * @brief Feed the watchdog timer (if running).
 *
 * On nRF51, the WDT may have been started by a previous application
 * and cannot be stopped. Must be fed periodically.
 *
 * On STM32, the IWDG may or may not be running. Safe to call always.
 *
 * @sideeffects Writes to watchdog reload register.
 */
void platform_wdt_feed(void);

/* ── Board identification ──────────────────────────────────────────────── */

/**
 * @brief Get the human-readable target board name.
 *
 * @return Constant string, e.g. "BLE-STM32", "BMS-STM32", "nRF51822".
 */
const char *platform_get_target_name(void);

/**
 * @brief Get the .sfw target ID for this board.
 *
 * @return Target ID matching SFW_TARGET_* constants.
 */
uint8_t platform_get_target_id(void);

/**
 * @brief Get the maximum firmware size for this platform.
 *
 * @return Maximum firmware binary size in bytes.
 */
uint32_t platform_get_max_fw_size(void);

/**
 * @brief Get the ECDSA-P256 public key for signature verification.
 *
 * @return Pointer to 64-byte raw public key (X||Y, 32 bytes each).
 */
const uint8_t *platform_get_ecdsa_pubkey(void);

/**
 * @brief Trigger a system reset.
 *
 * @sideeffects Resets the processor.
 */
void platform_reset(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
