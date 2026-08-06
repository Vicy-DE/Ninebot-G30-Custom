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

/**
 * Persist the signed .sfw header of the image just installed, so the bootloader can verify the
 * application on every subsequent boot (and enforce anti-rollback).
 * Erases and rewrites the settings page; leaves the update flag cleared.
 * @param hdr        pointer to the sfw_header_t
 * @param hdr_len    header size in bytes (must be a multiple of 4)
 * @param fw_version firmware version to record for rollback checks
 * @return 0 on success, -1 on bad arguments
 */
int nrf_flash_save_boot_record(const void *hdr, uint32_t hdr_len, uint32_t fw_version);

/**
 * @return pointer to the stored sfw_header_t in flash, or NULL when no valid record exists
 *         (e.g. an image flashed over SWD without going through the bootloader).
 */
const void *nrf_flash_get_boot_record(void);

/** @return the recorded firmware version, or 0 when no boot record exists. */
uint32_t nrf_flash_get_installed_version(void);

/* ── Word-aligned streaming writer ─────────────────────────────────────────
 * The NVMC only accepts word-aligned 32-bit writes — a byte/half-word write HARD FAULTS
 * (nRF51 RM 6.1.1). Incoming NBU blocks are arbitrary lengths, so stream through these.
 */

/** Start a streamed write at @p addr (must be word aligned). @sideeffects */
void nrf_flash_stream_begin(uint32_t addr);

/** Append bytes; leftover 0-3 bytes are carried into the next call. @sideeffects
 *  @return 0 on success, -1 on write failure */
int nrf_flash_stream_write(const uint8_t *data, uint32_t len);

/** Flush a trailing partial word, padded with 0xFF. @sideeffects
 *  @return 0 on success, -1 on write failure */
int nrf_flash_stream_finish(void);

/** @return the next address the stream will write to. */
uint32_t nrf_flash_stream_addr(void);

/* ── Bootloader self-update (RAM-resident) ─────────────────────────────────
 * Erasing/writing the bootloader's own pages cannot be driven by code living in those pages,
 * so this routine is linked into RAM. See docs/NRF51_FLASH_IAP.md.
 */

/**
 * Copy a verified, staged bootloader image over the running bootloader, then reset.
 * MUST be called with interrupts disabled; does not return on success.
 * @param staged_addr flash address of the staged image (word aligned, in the app region)
 * @param length      image size in bytes
 * @return -1 if the arguments are rejected (it never returns on success)
 * @sideeffects erases and rewrites BOOTLOADER_START..+BOOTLOADER_SIZE, then resets the chip
 */
int nrf_flash_install_bootloader(uint32_t staged_addr, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* NRF51_FLASH_H */
