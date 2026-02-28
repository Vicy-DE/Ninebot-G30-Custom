/**
 * @file nrf51_flash.h
 * @brief nRF51822 flash programming interface.
 */

#ifndef NRF51_FLASH_H
#define NRF51_FLASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Erase a single 1 KB flash page.
 * @param page_addr  Page-aligned address to erase
 * @return 0 on success, -1 on error
 */
int nrf_flash_erase_page(uint32_t page_addr);

/**
 * Erase the entire application region.
 * @return 0 on success, -1 on error
 */
int nrf_flash_erase_app_region(void);

/**
 * Write data to flash (word-aligned, 32-bit writes).
 * @param addr  Destination address (must be 4-byte aligned)
 * @param data  Source data
 * @param len   Length in bytes (rounded up to 4-byte boundary)
 * @return 0 on success, -1 on error
 */
int nrf_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * Read data from flash.
 */
void nrf_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * Set update flag in bootloader settings page.
 */
void nrf_flash_set_update_flag(void);

/**
 * Clear update flag.
 */
void nrf_flash_clear_update_flag(void);

/**
 * Check if update flag is set.
 * @return 1 if set, 0 if not
 */
int nrf_flash_is_update_requested(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF51_FLASH_H */
