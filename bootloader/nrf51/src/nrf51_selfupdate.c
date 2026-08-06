/**
 * @file nrf51_selfupdate.c
 * @brief RAM-resident bootloader self-update ("bootloader installer") for the nRF51822.
 *
 * ## Why this file exists — what may run from flash and what may not
 *
 * From the nRF51 Series Reference Manual, ch. 6 (NVMC):
 *
 *   - *"The CPU is halted while the NVMC is writing to the NVM."*
 *   - *"The CPU is halted while the NVMC performs the erase operation."*
 *
 * So the core simply **stalls** for the duration of an NVMC operation and resumes afterwards.
 * That means ordinary flash-resident code **may** drive erases/writes of *other* pages — the
 * bootloader at `0x0003C000` programming the application at `0x00018000` needs no RAM copy,
 * because its own instructions are still present when the CPU resumes.
 *
 * The one case that breaks is **erasing or rewriting the pages you are executing from**: after
 * the erase those instructions are gone (all bits become '1'), so the CPU resumes into blank
 * flash. Therefore the self-update loop below is placed in `.ramfunc`, which the startup
 * code copies into RAM along with `.data`, and runs entirely from SRAM.
 *
 * Other NVMC rules honoured here (same chapter):
 *   - *"Only word aligned writes are allowed. Byte or half word aligned writes will result in a
 *     hard fault."* → we copy 32-bit words only.
 *   - *"The NVMC is only able to write bits in the NVM that are erased, that is, set to '1'."*
 *     → every target page is erased before being written.
 *   - *"The user must make sure that writing and erasing is not enabled at the same time"* →
 *     CONFIG is assigned (never OR-ed) and returned to REN when done.
 *   - `ERASEPCR1`/`ERASEPAGE` erase pages in code region 1; region 0 (the SoftDevice, below
 *     `UICR.CLENR0 = 0x18000`) is protected by the MPU and cannot be erased from here at all.
 */

#include "nrf51_flash.h"
#include "nrf51.h"
#include "bootloader_config.h"

/**
 * The whole erase+program+reset sequence, executed from SRAM.
 *
 * Everything it needs is either a parameter or an absolute peripheral address — it must not
 * call, read or branch into flash while the bootloader pages are erased, so the NVMC polling
 * is inlined by hand and nothing here is a call to another translation unit.
 *
 * @param staged   source image, still in flash (outside the bootloader region)
 * @param words    length in 32-bit words
 * @sideeffects erases and rewrites the bootloader region, then resets the chip
 */
__attribute__((section(".ramfunc"), noinline, used))
static void ram_install(const uint32_t *staged, uint32_t words)
{
    uint32_t addr;
    uint32_t i;

    /* 1. Erase every bootloader page. From here on the caller's code no longer exists. */
    NRF_NVMC_CONFIG = NVMC_CONFIG_EEN;
    while (NRF_NVMC_READY == 0U) { }
    for (addr = BOOTLOADER_START;
         addr < BOOTLOADER_START + BOOTLOADER_SIZE;
         addr += NRF_FLASH_PAGE_SIZE) {
        NRF_NVMC_ERASEPAGE = addr;
        while (NRF_NVMC_READY == 0U) { }
    }

    /* 2. Program the staged image, word by word (word-aligned only — see header comment). */
    NRF_NVMC_CONFIG = NVMC_CONFIG_WEN;
    while (NRF_NVMC_READY == 0U) { }
    for (i = 0; i < words; i++) {
        *(volatile uint32_t *)(BOOTLOADER_START + i * 4U) = staged[i];
        while (NRF_NVMC_READY == 0U) { }
    }

    NRF_NVMC_CONFIG = NVMC_CONFIG_REN;
    while (NRF_NVMC_READY == 0U) { }

    /* 3. Reset into the freshly written bootloader. */
    SCB_AIRCR_M0 = (0x05FAUL << 16) | (1UL << 2);   /* VECTKEY | SYSRESETREQ */
    for (;;) { }
}

int nrf_flash_install_bootloader(uint32_t staged_addr, uint32_t length)
{
    /* The staged image must be word aligned, non-empty, fit the bootloader slot, and live
     * OUTSIDE the region we are about to erase. */
    if ((staged_addr & 3U) != 0U || (length & 3U) != 0U) {
        return -1;
    }
    if (length == 0U || length > BOOTLOADER_SIZE) {
        return -1;
    }
    if (staged_addr < APP_START_ADDR ||
        (staged_addr + length) > BOOTLOADER_START) {
        return -1;                      /* would overlap the bootloader or the SoftDevice */
    }

    __asm volatile ("cpsid i");         /* no interrupt may vector into erased flash */

    ram_install((const uint32_t *)staged_addr, length / 4U);

    return -1;                          /* unreachable: ram_install() resets the chip */
}
