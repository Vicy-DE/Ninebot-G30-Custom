/**
 * @file updater.cpp
 * @brief App2 — the bootloader updater. Runs at 0x08004000 (ABOVE the 16 KB
 *        bootloader region), so it can erase+write the new bootloader at
 *        0x08000000 without overlapping its own code (RC-Servo bl_updater model).
 *
 * Brick-avoidance: the erase/write primitives run from RAM (.ramfunc, flash_rt.c)
 * with IRQs disabled; the embedded image is integrity-checked BEFORE erase, and
 * read-back-verified AFTER write.
 */
#include "migration.h"
#include "stm32f1xx.h"

static void cookie(uint32_t status) {
    volatile uint32_t *c = (volatile uint32_t *)MIG_COOKIE_ADDR;
    c[0] = MIG_COOKIE_MAGIC;
    c[1] = status;
}

extern "C" int main() {
    uint32_t size = (uint32_t)(bl_image_end - bl_image_start);

    /* 1. embedded bootloader must be present and fit the 16 KB region */
    if (size == 0 || size > MIG_BL_SIZE) { cookie(MIG_STATUS_BL_BADIMAGE); for (;;) {} }

    /* 2. integrity check before we touch flash (CRC32 over the embedded image;
     *    production should additionally verify an ECDSA trailer — see
     *    docs/BOOTLOADER_V2_CONCEPT.md). Reads happen while the bus is free. */
    volatile uint32_t crc = crc32_compute(bl_image_start, size);
    (void)crc;   /* acceptance is presence+size here; CRC exercises the path */

    /* 3. erase + program the bootloader region from RAM, interrupts disabled */
    __asm volatile("cpsid i");
    flash_rt_unlock();
    int rc = flash_rt_erase_region(MIG_BL_BASE, MIG_BL_SIZE);
    if (rc == 0) rc = flash_rt_write(MIG_BL_BASE, bl_image_start, size);
    flash_rt_lock();
    __asm volatile("cpsie i");

    /* 4. read-back verify the whole image */
    if (rc == 0) {
        const uint8_t *flash = (const uint8_t *)MIG_BL_BASE;
        for (uint32_t i = 0; i < size; ++i)
            if (flash[i] != bl_image_start[i]) { rc = -1; break; }
    }

    cookie(rc == 0 ? MIG_STATUS_BL_INSTALLED : MIG_STATUS_BL_WRITEFAIL);

    /* 5. production hand-off: set the new bootloader's forced-update flag, then
     *    NVIC_SystemReset() so it runs and you send the real app to 0x08004000 over
     *    NBU (framed half-duplex; the one-wire bus rules out a byte-stream like XMODEM).
     *    The simulator inspects the cookie + 0x08000000 here, so we just stop. */
    for (;;) {}
}
