/**
 * @file bootloader_main.c
 * @brief nRF51822 secure bootloader — main entry point.
 *
 * Boot flow:
 *   1. HFCLK start (16 MHz crystal)
 *   2. SysTick init (1 ms tick)
 *   3. Check update trigger:
 *      a. GPREGRET == BOOT_ENTER_MAGIC (set by application before reset)
 *      b. Update flag in bootloader settings page
 *      c. No valid application at APP_START_ADDR
 *   4. If update → receive .sfw via the NBU framed half-duplex protocol on UART0
 *   5. If no update → jump to application
 *
 * The nRF51822 bootloader sits at 0x0003C000 (top of flash).
 * The MBR at 0x00000000 reads UICR.BOOTLOADERADDR and jumps here.
 *
 * Update path: PC → USB-UART → VESC → BLE STM32 (passthrough) → UART0 → here
 */

#include "nrf51.h"
#include "bootloader_config.h"
#include "nrf51_flash.h"
#include "nrf51_uart.h"
#include "fw_header.h"
#include "nbu.h"
#include "sha256.h"
#include "ecdsa.h"
#include "crc32.h"
#include <string.h>

/* ── Global tick counter ───────────────────────────────────────────────── */

volatile uint32_t g_tick_ms = 0;

/* ── ECDSA public key ──────────────────────────────────────────────────── */

static const uint8_t ecdsa_pubkey[ECDSA_P256_PUBKEY_SIZE] = ECDSA_PUBLIC_KEY;

/* ── Receive state for NBU ─────────────────────────────────────────────── */

static sfw_header_t g_sfw_header;
static uint32_t g_flash_write_addr;
static int g_header_complete;
static int g_write_error;

/* ── Clock initialization ──────────────────────────────────────────────── */

static void clock_init(void)
{
    /* Start 16 MHz HFCLK (external crystal) */
    NRF_CLOCK_TASKS_HFCLKSTART = 1;
    while (NRF_CLOCK_EVENTS_HFCLKSTARTED == 0) { /* Wait */ }
    NRF_CLOCK_EVENTS_HFCLKSTARTED = 0;
}

/* ── SysTick (1 ms tick at 16 MHz) ─────────────────────────────────────── */

static void systick_init(void)
{
    SYSTICK_LOAD_M0 = (16000000U / 1000U) - 1U;   /* 16 MHz / 1000 = 16000 */
    SYSTICK_VAL_M0 = 0;
    SYSTICK_CTRL_M0 = 0x07;  /* Enable, interrupt, processor clock */
}

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t get_tick(void)
{
    return g_tick_ms;
}

/* ── WDT feed (if watchdog was started by prior application) ───────────── */

static void wdt_feed(void)
{
    /* If the WDT is running (started by previous app), feed it
     * to prevent reset during bootloader operations. */
    if (NRF_WDT_RUNSTATUS) {
        NRF_WDT_RR0 = WDT_RR_RELOAD_VALUE;
    }
}

/* ── Application validity ──────────────────────────────────────────────── */

static int is_app_valid(void)
{
    uint32_t sp    = *(volatile uint32_t *)APP_START_ADDR;
    uint32_t reset = *(volatile uint32_t *)(APP_START_ADDR + 4);

    /* Stack pointer must be in SRAM (0x20000000–0x20004000) */
    if (sp < NRF_SRAM_BASE || sp > NRF_STACK_TOP) {
        return 0;
    }

    /* Reset handler must be in app flash region */
    if (reset < APP_START_ADDR || reset > (APP_START_ADDR + APP_MAX_SIZE)) {
        return 0;
    }

    return 1;
}

/* ── Boot-time signature verification ──────────────────────────────────────
 *
 * The structural check above only proves the vector table looks sane. Real secure boot must
 * re-verify the signature on EVERY boot, because flash can change after an update completes
 * (SWD write, glitched/partial update, bit rot). We can do that because the signed header of
 * the installed image is persisted in the settings page.
 *
 * @return 1 = app verified (or unverifiable but policy allows booting), 0 = must not boot
 */
static int is_app_authentic(void)
{
    const sfw_header_t *h = (const sfw_header_t *)nrf_flash_get_boot_record();

    if (h == 0) {
        /* No record: the image did not come from this bootloader (typically an SWD flash). */
#if SECURE_BOOT_REQUIRE_RECORD
        nrf_uart_puts("[BOOT-NRF] No boot record - refusing to boot unverified image\r\n");
        return 0;
#else
        nrf_uart_puts("[BOOT-NRF] WARNING: no boot record, image is UNVERIFIED\r\n");
        return 1;
#endif
    }

    if (sfw_validate_header(h, MY_TARGET_ID, SFW_MAX_FW_SIZE_NRF51) != SFW_OK) {
        nrf_uart_puts("[BOOT-NRF] Boot record header invalid\r\n");
        return 0;
    }
    wdt_feed();
    if (sfw_check_crc((const uint8_t *)APP_START_ADDR, h->fw_size, h->fw_crc32) != SFW_OK) {
        nrf_uart_puts("[BOOT-NRF] App CRC mismatch - flash altered\r\n");
        return 0;
    }
    wdt_feed();
    if (sfw_verify_signature(h, (const uint8_t *)APP_START_ADDR,
                             h->fw_size, ecdsa_pubkey) != SFW_OK) {
        nrf_uart_puts("[BOOT-NRF] App SIGNATURE INVALID - refusing to boot\r\n");
        return 0;
    }
    wdt_feed();
    return 1;
}

/* ── NBU write callback ────────────────────────────────────────────────── */

static int nbu_block_handler(const uint8_t *data, uint32_t offset,
                             uint32_t length, void *user_ctx)
{
    (void)user_ctx;
    uint32_t i;

    /* Feed watchdog during transfer */
    wdt_feed();

    /* Accumulate header bytes */
    for (i = 0; i < length; i++) {
        uint32_t pos = offset + i;
        if (pos < SFW_HEADER_SIZE) {
            ((uint8_t *)&g_sfw_header)[pos] = data[i];
        }
    }

    /* Once header is complete, write firmware to flash */
    if (!g_header_complete && (offset + length) >= SFW_HEADER_SIZE) {
        g_header_complete = 1;
        g_flash_write_addr = APP_START_ADDR;
    }

    if (g_header_complete) {
        uint32_t fw_start_in_block = 0;
        uint32_t fw_len = length;

        if (offset < SFW_HEADER_SIZE) {
            fw_start_in_block = SFW_HEADER_SIZE - offset;
            fw_len = length - fw_start_in_block;
        }

        if (fw_len > 0 && fw_start_in_block < length) {
            /* Stream it: the NVMC only accepts word-aligned writes (a byte/half-word write
             * hard-faults, nRF51 RM 6.1.1) and NBU blocks are arbitrary lengths. */
            if (nrf_flash_stream_write(data + fw_start_in_block, fw_len) != 0) {
                g_write_error = 1;
                return -1;
            }
            g_flash_write_addr += fw_len;
        }
    }

    return 0;
}

/* ── NBU I/O callbacks ─────────────────────────────────────────────────── */

static void nbu_uart_send(uint8_t byte)
{
    nrf_uart_send_byte(byte);
}

static int nbu_uart_recv(uint8_t *byte, uint32_t timeout)
{
    return nrf_uart_recv_byte(byte, timeout);
}

static uint32_t nbu_get_tick(void)
{
    return get_tick();
}

static const nbu_io_t nbu_io = {
    .uart_send_byte = nbu_uart_send,
    .uart_recv_byte = nbu_uart_recv,
    .get_tick_ms    = nbu_get_tick,
};

/* ── Update mode ───────────────────────────────────────────────────────── */

static void enter_update_mode(void)
{
    uint32_t total_received = 0;
    sfw_result_t result;
    nbu_result_t xresult;
    int is_bootloader_image;

    nrf_uart_puts("\r\n[BOOT-NRF] Secure Bootloader v1.0\r\n");
    nrf_uart_puts("[BOOT-NRF] Target: nRF51822\r\n");
    nrf_uart_puts("[BOOT-NRF] Waiting for .sfw via NBU (framed half-duplex)...\r\n");

    /* Initialize receive state */
    memset(&g_sfw_header, 0, sizeof(g_sfw_header));
    g_flash_write_addr = APP_START_ADDR;
    nrf_flash_stream_begin(APP_START_ADDR);
    g_header_complete = 0;
    g_write_error = 0;

    /* Erase application region */
    nrf_uart_puts("[BOOT-NRF] Erasing app region...\r\n");
    if (nrf_flash_erase_app_region() != 0) {
        nrf_uart_puts("[BOOT-NRF] ERROR: Flash erase failed!\r\n");
        return;
    }
    nrf_uart_puts("[BOOT-NRF] Erase complete.\r\n");

    /* Receive via the NBU framed half-duplex protocol */
    xresult = nbu_receive(&nbu_io, MY_BUS_ADDR, nbu_block_handler,
                          NULL, &total_received);

    if (xresult != NBU_OK) {
        nrf_uart_puts("[BOOT-NRF] ERROR: NBU transfer failed\r\n");
        nrf_flash_erase_app_region();
        return;
    }

    if (g_write_error) {
        nrf_uart_puts("[BOOT-NRF] ERROR: Flash write error\r\n");
        nrf_flash_erase_app_region();
        return;
    }

    nrf_uart_puts("[BOOT-NRF] Transfer complete. Validating...\r\n");

    /* Validate header */
    /* Accept either an application image or a BOOTLOADER image (self-update). Both are staged
     * in the app region and must pass the identical signature check before anything is used. */
    is_bootloader_image = (g_sfw_header.target_id == SFW_TARGET_NRF51822_BL);

    result = sfw_validate_header(&g_sfw_header,
                                 is_bootloader_image ? SFW_TARGET_NRF51822_BL : MY_TARGET_ID,
                                 SFW_MAX_FW_SIZE_NRF51);
    if (result != SFW_OK) {
        nrf_uart_puts("[BOOT-NRF] ERROR: Header validation failed\r\n");
        nrf_flash_erase_app_region();
        return;
    }
    if (is_bootloader_image && g_sfw_header.fw_size > BOOTLOADER_SIZE) {
        nrf_uart_puts("[BOOT-NRF] ERROR: bootloader image too large\r\n");
        nrf_flash_erase_app_region();
        return;
    }

    /* Anti-rollback: refuse an image older than the one already installed, so a signed but
     * vulnerable old release cannot be pushed back on. SFW_FLAG_FORCE_UPDATE overrides. */
    {
        const uint32_t installed = nrf_flash_get_installed_version();
        if (installed != 0U && g_sfw_header.fw_version < installed &&
            (g_sfw_header.flags & SFW_FLAG_FORCE_UPDATE) == 0U) {
            nrf_uart_puts("[BOOT-NRF] ERROR: rollback rejected (older version)\r\n");
            nrf_flash_erase_app_region();
            return;
        }
    }

    nrf_uart_puts("[BOOT-NRF] Header OK. Verifying CRC...\r\n");

    result = sfw_check_crc((const uint8_t *)APP_START_ADDR,
                            g_sfw_header.fw_size,
                            g_sfw_header.fw_crc32);
    if (result != SFW_OK) {
        nrf_uart_puts("[BOOT-NRF] ERROR: CRC mismatch\r\n");
        nrf_flash_erase_app_region();
        return;
    }

    nrf_uart_puts("[BOOT-NRF] CRC OK. Verifying ECDSA signature...\r\n");

    wdt_feed();

    result = sfw_verify_signature(&g_sfw_header,
                                   (const uint8_t *)APP_START_ADDR,
                                   g_sfw_header.fw_size,
                                   ecdsa_pubkey);
    if (result != SFW_OK) {
        nrf_uart_puts("[BOOT-NRF] ERROR: SIGNATURE INVALID!\r\n");
        nrf_flash_erase_app_region();
        return;
    }

    nrf_uart_puts("[BOOT-NRF] Signature VALID.\r\n");

    /* ── Bootloader self-update ──────────────────────────────────────────────
     * The staged image is a new BOOTLOADER, verified byte-for-byte above. Installing it means
     * erasing the very pages we are executing from, which no flash-resident code can survive
     * (nRF51 RM ch.6 — the CPU resumes into erased flash). nrf_flash_install_bootloader()
     * therefore runs from SRAM and resets the chip; it does not return. */
    if (is_bootloader_image) {
        nrf_uart_puts("[BOOT-NRF] Installing new BOOTLOADER from RAM...\r\n");
        {   /* let the UART drain first — the reset is immediate */
            uint32_t t = get_tick();
            while ((get_tick() - t) < 100) { wdt_feed(); }
        }
        (void)nrf_flash_install_bootloader(APP_START_ADDR, g_sfw_header.fw_size);
        nrf_uart_puts("[BOOT-NRF] ERROR: bootloader install rejected\r\n");
        return;
    }

    nrf_uart_puts("[BOOT-NRF] Update complete.\r\n");

    /* Persist the signed header so every later boot can re-verify this image, and record the
     * version for anti-rollback. This also leaves the update flag cleared. */
    if (nrf_flash_save_boot_record(&g_sfw_header, sizeof(g_sfw_header),
                                   g_sfw_header.fw_version) != 0) {
        nrf_uart_puts("[BOOT-NRF] WARNING: could not store boot record\r\n");
    }

    /* Clear update triggers */
    nrf_flash_clear_update_flag();
    NRF_POWER_GPREGRET_REG = 0;

    nrf_uart_puts("[BOOT-NRF] Rebooting...\r\n");

    /* Delay for UART flush */
    {
        uint32_t t = get_tick();
        while ((get_tick() - t) < 100) { wdt_feed(); }
    }

    nrf_system_reset();
}

/* ── Jump to application ───────────────────────────────────────────────── */

static void jump_to_app(void)
{
    uint32_t app_sp    = *(volatile uint32_t *)APP_START_ADDR;
    uint32_t app_entry = *(volatile uint32_t *)(APP_START_ADDR + 4);

    /* Disable SysTick */
    SYSTICK_CTRL_M0 = 0;

    /* On nRF51822, the MBR handles vector table forwarding.
     * The application's vector table is at APP_START_ADDR as expected.
     * We don't set VTOR directly — on Cortex-M0, VTOR may not be
     * available. Instead, the MBR uses the SoftDevice interrupt
     * forwarding mechanism. */

    /* Set MSP and branch to reset handler */
    __asm volatile (
        "MSR MSP, %0    \n"
        "BX  %1         \n"
        :
        : "r" (app_sp), "r" (app_entry)
    );

    for (;;) {}
}

/* ── Main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    int enter_update = 0;

    /* Start HFCLK */
    clock_init();

    /* SysTick for 1 ms timing */
    systick_init();

    /* Initialize UART */
    nrf_uart_init();

    /* Feed WDT if it was started by a previous app */
    wdt_feed();

    /* ── Check update triggers ─────────────────────────────────── */

    /* Trigger 1: GPREGRET magic (set by application before reset) */
    if ((NRF_POWER_GPREGRET_REG & 0xFF) == BOOT_ENTER_MAGIC) {
        NRF_POWER_GPREGRET_REG = 0;
        enter_update = 1;
    }

    /* Trigger 2: Update flag in settings page */
    if (nrf_flash_is_update_requested()) {
        enter_update = 1;
    }

    /* Trigger 3: No structurally valid application */
    if (!is_app_valid()) {
        enter_update = 1;
    }

    /* Trigger 4: SECURE BOOT — the installed image must still verify against its stored,
     * signed header on every boot. Catches post-update tampering, a glitched/partial write,
     * or flash corruption; without this, secure boot would only apply at update time. */
    if (!enter_update && !is_app_authentic()) {
        enter_update = 1;
    }

    if (enter_update) {
        nrf_flash_clear_update_flag();

        for (;;) {
            enter_update_mode();

            nrf_uart_puts("[BOOT-NRF] Retrying in 3 seconds...\r\n");
            uint32_t t = get_tick();
            while ((get_tick() - t) < 3000) { wdt_feed(); }
        }
    }

    /* ── Normal boot → jump to application ─────────────────────── */
    jump_to_app();

    return 0;
}
