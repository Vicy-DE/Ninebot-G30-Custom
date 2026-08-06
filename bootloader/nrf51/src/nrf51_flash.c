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
    /* nRF51 RM 6.1.1: "Only word aligned writes are allowed. Byte or half word aligned writes
     * will result in a hard fault." Fail loudly instead of faulting. */
    if ((addr & 3U) != 0U) {
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
    /* Clear the flag IN PLACE (1->0 needs no erase) so the boot record on the same page
     * survives. Erasing here would destroy the stored signature and make the next boot
     * unverifiable. */
    volatile uint32_t *flag = (volatile uint32_t *)(BL_SETTINGS_ADDR + BL_FLAG_OFFSET);
    if (*flag == 0U) {
        return;                                  /* already consumed */
    }
    nvmc_wait_ready();
    NRF_NVMC_CONFIG = NVMC_CONFIG_WEN;
    nvmc_wait_ready();
    *flag = 0U;
    nvmc_wait_ready();
    NRF_NVMC_CONFIG = NVMC_CONFIG_REN;
    nvmc_wait_ready();
}

int nrf_flash_save_boot_record(const void *hdr, uint32_t hdr_len, uint32_t fw_version)
{
    const uint32_t *src = (const uint32_t *)hdr;
    uint32_t i;

    if (hdr_len == 0U || (hdr_len % 4U) != 0U ||
        (BL_RECORD_HEADER_OFFSET + hdr_len) > BL_SETTINGS_SIZE) {
        return -1;
    }

    /* Erase the settings page, then lay down magic + version + the signed header. The update
     * flag is left erased (0xFFFFFFFF != UPDATE_FLAG_MAGIC), i.e. "no update requested". */
    nvmc_wait_ready();
    NRF_NVMC_CONFIG = NVMC_CONFIG_EEN;
    nvmc_wait_ready();
    NRF_NVMC_ERASEPAGE = BL_SETTINGS_ADDR;
    nvmc_wait_ready();

    NRF_NVMC_CONFIG = NVMC_CONFIG_WEN;
    nvmc_wait_ready();
    *(volatile uint32_t *)(BL_SETTINGS_ADDR + BL_RECORD_MAGIC_OFFSET) = BL_BOOT_RECORD_MAGIC;
    nvmc_wait_ready();
    *(volatile uint32_t *)(BL_SETTINGS_ADDR + BL_RECORD_VERSION_OFFSET) = fw_version;
    nvmc_wait_ready();
    for (i = 0; i < hdr_len / 4U; i++) {
        *(volatile uint32_t *)(BL_SETTINGS_ADDR + BL_RECORD_HEADER_OFFSET + i * 4U) = src[i];
        nvmc_wait_ready();
    }
    NRF_NVMC_CONFIG = NVMC_CONFIG_REN;
    nvmc_wait_ready();

    return 0;
}

const void *nrf_flash_get_boot_record(void)
{
    if (*(volatile uint32_t *)(BL_SETTINGS_ADDR + BL_RECORD_MAGIC_OFFSET) != BL_BOOT_RECORD_MAGIC) {
        return 0;
    }
    return (const void *)(BL_SETTINGS_ADDR + BL_RECORD_HEADER_OFFSET);
}

uint32_t nrf_flash_get_installed_version(void)
{
    if (*(volatile uint32_t *)(BL_SETTINGS_ADDR + BL_RECORD_MAGIC_OFFSET) != BL_BOOT_RECORD_MAGIC) {
        return 0U;                               /* nothing installed by us yet */
    }
    return *(volatile uint32_t *)(BL_SETTINGS_ADDR + BL_RECORD_VERSION_OFFSET);
}

int nrf_flash_is_update_requested(void)
{
    uint32_t flag = *(volatile uint32_t *)BL_SETTINGS_ADDR;
    return (flag == UPDATE_FLAG_MAGIC) ? 1 : 0;
}

/* ── Word-aligned streaming writer ─────────────────────────────────────────
 *
 * The NBU transfer delivers arbitrary-length blocks, but the NVMC only accepts word-aligned
 * 32-bit writes (a byte/half-word write hard-faults, nRF51 RM 6.1.1). This streamer buffers
 * the 0-3 leftover bytes of a block and emits them together with the start of the next one,
 * so the flash address always stays word aligned and no byte is written twice.
 */

static uint32_t s_stream_addr;
static uint8_t  s_stream_carry[4];
static uint32_t s_stream_carry_len;

void nrf_flash_stream_begin(uint32_t addr)
{
    s_stream_addr = addr;
    s_stream_carry_len = 0U;
}

int nrf_flash_stream_write(const uint8_t *data, uint32_t len)
{
    uint32_t i = 0U;

    /* Top up a partial word from the previous block first. */
    while (s_stream_carry_len > 0U && s_stream_carry_len < 4U && i < len) {
        s_stream_carry[s_stream_carry_len++] = data[i++];
    }
    if (s_stream_carry_len == 4U) {
        if (nrf_flash_write(s_stream_addr, s_stream_carry, 4U) != 0) {
            return -1;
        }
        s_stream_addr += 4U;
        s_stream_carry_len = 0U;
    }

    /* Whole words straight through. */
    {
        const uint32_t whole = ((len - i) / 4U) * 4U;
        if (whole > 0U) {
            if (nrf_flash_write(s_stream_addr, data + i, whole) != 0) {
                return -1;
            }
            s_stream_addr += whole;
            i += whole;
        }
    }

    /* Keep the tail for the next call. */
    while (i < len) {
        s_stream_carry[s_stream_carry_len++] = data[i++];
    }
    return 0;
}

int nrf_flash_stream_finish(void)
{
    if (s_stream_carry_len == 0U) {
        return 0;
    }
    while (s_stream_carry_len < 4U) {
        s_stream_carry[s_stream_carry_len++] = 0xFFU;   /* pad; erased flash is 0xFF */
    }
    if (nrf_flash_write(s_stream_addr, s_stream_carry, 4U) != 0) {
        return -1;
    }
    s_stream_addr += 4U;
    s_stream_carry_len = 0U;
    return 0;
}

uint32_t nrf_flash_stream_addr(void)
{
    return s_stream_addr;
}
