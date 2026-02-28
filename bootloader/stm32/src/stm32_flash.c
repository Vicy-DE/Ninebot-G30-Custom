/**
 * @file stm32_flash.c
 * @brief STM32F103 flash programming implementation.
 *
 * Implements page erase and half-word write for the application region.
 * The bootloader region (pages 0-11) is never touched.
 */

#include "stm32_flash.h"
#include "stm32f1xx.h"
#include "bootloader_config.h"

/* ── Internal helpers ──────────────────────────────────────────────────── */

static void flash_wait_busy(void)
{
    while (FLASH_SR & FLASH_SR_BSY) {
        /* Spin */
    }
}

/* ── Public API ────────────────────────────────────────────────────────── */

void flash_unlock(void)
{
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
}

void flash_lock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
}

int flash_erase_page(uint32_t page_addr)
{
    /* Safety: never erase bootloader pages */
    if (page_addr < APP_START_ADDR) {
        return -1;
    }
    if (page_addr >= (FLASH_BASE_ADDR + FLASH_SIZE)) {
        return -1;
    }

    flash_wait_busy();

    FLASH_CR |= FLASH_CR_PER;
    FLASH_AR = page_addr;
    FLASH_CR |= FLASH_CR_STRT;

    flash_wait_busy();

    FLASH_CR &= ~FLASH_CR_PER;

    /* Verify the page is erased (first word should be 0xFFFFFFFF) */
    if (*(volatile uint32_t *)page_addr != 0xFFFFFFFFU) {
        return -1;
    }

    return 0;
}

int flash_erase_app_region(void)
{
    uint32_t addr;
    int result;

    flash_unlock();

    for (addr = APP_START_ADDR;
         addr < APP_START_ADDR + APP_MAX_SIZE;
         addr += FLASH_PAGE_SIZE) {
        result = flash_erase_page(addr);
        if (result != 0) {
            flash_lock();
            return -1;
        }
    }

    flash_lock();
    return 0;
}

int flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint16_t half_word;

    /* Safety: never write to bootloader region */
    if (addr < APP_START_ADDR) {
        return -1;
    }

    flash_unlock();

    for (i = 0; i < len; i += 2) {
        flash_wait_busy();

        /* Assemble half-word (little-endian) */
        half_word = data[i];
        if (i + 1 < len) {
            half_word |= (uint16_t)data[i + 1] << 8;
        } else {
            half_word |= 0xFF00U;  /* Pad last byte with 0xFF */
        }

        FLASH_CR |= FLASH_CR_PG;
        *(volatile uint16_t *)(addr + i) = half_word;

        flash_wait_busy();

        FLASH_CR &= ~FLASH_CR_PG;

        /* Verify */
        if (*(volatile uint16_t *)(addr + i) != half_word) {
            flash_lock();
            return -1;
        }
    }

    flash_lock();
    return 0;
}

void flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        buf[i] = *(volatile uint8_t *)(addr + i);
    }
}

void flash_set_update_flag(void)
{
    uint32_t flag = UPDATE_FLAG_MAGIC;

    flash_unlock();
    flash_erase_page(CONFIG_START_ADDR);
    flash_write(CONFIG_START_ADDR, (const uint8_t *)&flag, sizeof(flag));
    flash_lock();
}

void flash_clear_update_flag(void)
{
    flash_unlock();
    flash_erase_page(CONFIG_START_ADDR);
    flash_lock();
}

int flash_is_update_requested(void)
{
    uint32_t flag = *(volatile uint32_t *)UPDATE_FLAG_ADDR;
    return (flag == UPDATE_FLAG_MAGIC) ? 1 : 0;
}
