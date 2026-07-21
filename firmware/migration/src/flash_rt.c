/**
 * @file flash_rt.c
 * @brief STM32F103 flash erase/program primitives that run **from RAM**
 *        (`.ramfunc`), so they can reprogram the bootloader region (0x08000000)
 *        without fetching instructions from the flash being erased.
 *
 * Mirrors the RC-Servo `bl_updater` brick-avoidance model: the actively-stalling
 * flash ops live in SRAM (`.ramfunc`, copied there at startup), interrupts are
 * disabled by the caller, and the caller does verify-before-erase + read-back.
 *
 * F103 medium-density: 1 KB pages, half-word (16-bit) programming.
 */
#include "stm32f1xx.h"
#include <stdint.h>
#include <stddef.h>

#define RAMFUNC __attribute__((section(".ramfunc"), noinline))

RAMFUNC static void flash_wait_busy(void) {
    while (FLASH_SR & FLASH_SR_BSY) { /* spin */ }
}

RAMFUNC void flash_rt_unlock(void) {
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
}

RAMFUNC void flash_rt_lock(void) {
    FLASH_CR |= FLASH_CR_LOCK;
}

/** Erase one 1 KB page at @p addr. @return 0 on success. */
RAMFUNC int flash_rt_erase_page(uint32_t addr) {
    flash_wait_busy();
    FLASH_SR = FLASH_SR_EOP;            /* clear EOP */
    FLASH_CR |= FLASH_CR_PER;
    FLASH_AR = addr;
    FLASH_CR |= FLASH_CR_STRT;
    flash_wait_busy();
    FLASH_CR &= ~FLASH_CR_PER;
    return 0;
}

/** Program @p len bytes (rounded up to a half-word) from @p data to @p addr. */
RAMFUNC int flash_rt_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t hw = data[i];
        hw |= (uint16_t)((i + 1 < len ? data[i + 1] : 0xFF) << 8);
        flash_wait_busy();
        FLASH_CR |= FLASH_CR_PG;
        *(volatile uint16_t *)(addr + i) = hw;
        flash_wait_busy();
        FLASH_CR &= ~FLASH_CR_PG;
        if (*(volatile uint16_t *)(addr + i) != hw) return -1;   /* verify */
    }
    return 0;
}

/** Erase a region [addr, addr+len) page-by-page (1 KB pages). */
RAMFUNC int flash_rt_erase_region(uint32_t addr, uint32_t len) {
    for (uint32_t off = 0; off < len; off += 1024) {
        if (flash_rt_erase_page(addr + off) != 0) return -1;
    }
    return 0;
}
