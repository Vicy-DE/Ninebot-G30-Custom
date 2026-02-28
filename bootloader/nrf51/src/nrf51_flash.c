/**
 * @file nrf51_flash.c
 * @brief nRF51822 NVMC flash programming implementation.
 *
 * The nRF51822 uses the NVMC (Non-Volatile Memory Controller) peripheral
 * for flash operations. Unlike STM32, writes are 32-bit word-aligned
 * and the NVMC mode must be set explicitly (read/write/erase).
 */

#include "nrf51_flash.h"
#include "nrf51.h"
#include "bootloader_config.h"

/* ── Wait for NVMC ready ───────────────────────────────────────────────── */

static void nvmc_wait_ready(void)
{
    while (NRF_NVMC_READY == 0) {
        /* Spin until ready */
    }
}

/* ── Public API ────────────────────────────────────────────────────────── */

int nrf_flash_erase_page(uint32_t page_addr)
{
    /* Safety: never erase MBR, SoftDevice, or bootloader pages */
    if (page_addr < APP_START_ADDR) {
        return -1;
    }
    if (page_addr >= BOOTLOADER_START) {
        return -1;
    }

    nvmc_wait_ready();

    /* Enable erase mode */
    NRF_NVMC_CONFIG = NVMC_CONFIG_EEN;
    nvmc_wait_ready();

    /* Erase the page */
    NRF_NVMC_ERASEPAGE = page_addr;
    nvmc_wait_ready();

    /* Return to read mode */
    NRF_NVMC_CONFIG = NVMC_CONFIG_REN;
    nvmc_wait_ready();

    /* Verify erased */
    if (*(volatile uint32_t *)page_addr != 0xFFFFFFFFU) {
        return -1;
    }

    return 0;
}

int nrf_flash_erase_app_region(void)
{
    uint32_t addr;

    for (addr = APP_START_ADDR;
         addr < APP_START_ADDR + APP_MAX_SIZE;
         addr += NRF_FLASH_PAGE_SIZE) {
        if (nrf_flash_erase_page(addr) != 0) {
            return -1;
        }
    }
    return 0;
}

int nrf_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t word;

    /* Safety check */
    if (addr < APP_START_ADDR) {
        return -1;
    }
    if (addr >= BOOTLOADER_START) {
        return -1;
    }

    nvmc_wait_ready();

    /* Enable write mode */
    NRF_NVMC_CONFIG = NVMC_CONFIG_WEN;
    nvmc_wait_ready();

    /* Write 32-bit words */
    for (i = 0; i < len; i += 4) {
        /* Assemble word (little-endian), pad with 0xFF */
        word = 0xFFFFFFFFU;
        if (i < len)     word = (word & 0xFFFFFF00U) | data[i];
        if (i + 1 < len) word = (word & 0xFFFF00FFU) | ((uint32_t)data[i + 1] << 8);
        if (i + 2 < len) word = (word & 0xFF00FFFFU) | ((uint32_t)data[i + 2] << 16);
        if (i + 3 < len) word = (word & 0x00FFFFFFU) | ((uint32_t)data[i + 3] << 24);

        *(volatile uint32_t *)(addr + i) = word;
        nvmc_wait_ready();

        /* Verify */
        if (*(volatile uint32_t *)(addr + i) != word) {
            NRF_NVMC_CONFIG = NVMC_CONFIG_REN;
            return -1;
        }
    }

    /* Return to read mode */
    NRF_NVMC_CONFIG = NVMC_CONFIG_REN;
    nvmc_wait_ready();

    return 0;
}

void nrf_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        buf[i] = *(volatile uint8_t *)(addr + i);
    }
}

void nrf_flash_set_update_flag(void)
{
    uint32_t flag = UPDATE_FLAG_MAGIC;

    /* Erase bootloader settings page */
    nvmc_wait_ready();
    NRF_NVMC_CONFIG = NVMC_CONFIG_EEN;
    nvmc_wait_ready();
    NRF_NVMC_ERASEPAGE = BL_SETTINGS_ADDR;
    nvmc_wait_ready();
    NRF_NVMC_CONFIG = NVMC_CONFIG_REN;
    nvmc_wait_ready();

    /* Write flag */
    nrf_flash_write(BL_SETTINGS_ADDR, (const uint8_t *)&flag, sizeof(flag));
}

void nrf_flash_clear_update_flag(void)
{
    nvmc_wait_ready();
    NRF_NVMC_CONFIG = NVMC_CONFIG_EEN;
    nvmc_wait_ready();
    NRF_NVMC_ERASEPAGE = BL_SETTINGS_ADDR;
    nvmc_wait_ready();
    NRF_NVMC_CONFIG = NVMC_CONFIG_REN;
    nvmc_wait_ready();
}

int nrf_flash_is_update_requested(void)
{
    uint32_t flag = *(volatile uint32_t *)BL_SETTINGS_ADDR;
    return (flag == UPDATE_FLAG_MAGIC) ? 1 : 0;
}
