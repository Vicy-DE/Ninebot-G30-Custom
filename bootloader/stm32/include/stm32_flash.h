/**
 * @file stm32_flash.h
 * @brief STM32F103 flash programming interface for bootloader.
 */

#ifndef STM32_FLASH_H
#define STM32_FLASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Unlock the flash memory for programming.
 * Must be called before any erase/write operations.
 */
void flash_unlock(void);

/**
 * Lock the flash memory (re-enable write protection).
 * Call after all programming is complete.
 */
void flash_lock(void);

/**
 * Erase a single 1 KB flash page.
 * @param page_addr  Start address of the page (must be 1KB-aligned)
 * @return 0 on success, -1 on error
 */
int flash_erase_page(uint32_t page_addr);

/**
 * Erase the entire application region (APP_START_ADDR to APP_START_ADDR + APP_MAX_SIZE).
 * @return 0 on success, -1 on error
 */
int flash_erase_app_region(void);

/**
 * Write data to flash (half-word aligned).
 * STM32F103 flash can only be written in 16-bit half-words.
 *
 * @param addr  Destination address (must be 2-byte aligned)
 * @param data  Source data buffer
 * @param len   Number of bytes to write (rounded up to next half-word)
 * @return 0 on success, -1 on error
 */
int flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * Read data from flash.
 * @param addr  Source address
 * @param buf   Destination buffer
 * @param len   Number of bytes to read
 */
void flash_read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * Set the update request flag in the config page.
 * On next boot, the bootloader will enter update mode.
 */
void flash_set_update_flag(void);

/**
 * Clear the update request flag.
 */
void flash_clear_update_flag(void);

/**
 * Check if the update request flag is set.
 * @return 1 if set, 0 if not
 */
int flash_is_update_requested(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32_FLASH_H */
