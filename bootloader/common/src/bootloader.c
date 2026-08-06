/**
 * @file bootloader.c
 * @brief Platform-independent secure bootloader logic.
 *
 * This file contains the shared bootloader functionality that is
 * common across all target platforms (STM32F103, nRF51822). All
 * hardware access goes through the platform.h abstraction layer.
 *
 * Boot flow:
 *   1. platform_init()     → clocks, UART, GPIO
 *   2. Check update trigger → flag, button, invalid app
 *   3. If update → NBU receive, validate .sfw, flash, reboot
 *   4. If no update → verify app, jump
 *
 * Inspired by the RC-Servo bootloader architecture.
 */

#include "platform.h"
#include "fw_header.h"
#include "nbu.h"
#include "sha256.h"
#include "ecdsa.h"
#include "crc32.h"
#include <string.h>

/* ── Bus address (Ninebot) for framed NBU updates ──────────────────────── */

/** Map the SFW target id to its Ninebot bus address.
 *  Only the dashboard (nRF51822, address 0x21) is a target — the STM32 boards were removed. */
static uint8_t my_bus_addr(void)
{
    switch (platform_get_target_id()) {
    case SFW_TARGET_NRF51822: return 0x21;   /* dashboard */
    default:                  return 0x21;
    }
}

/* ── ECDSA public key (provided by platform) ──────────────────────────── */

/* Key is embedded in platform-specific code and accessed via
 * platform_get_ecdsa_pubkey(). */

/* ── Receive state for NBU streaming ───────────────────────────────────── */

static sfw_header_t g_sfw_header;
static uint32_t g_flash_write_addr;
static int g_header_complete;
static int g_write_error;

/* ── NBU block handler ─────────────────────────────────────────────────── */

/**
 * @brief Handle each accepted in-order NBU data chunk as it arrives.
 *
 * First 256 bytes are accumulated into the .sfw header buffer.
 * Subsequent bytes are written directly to flash at the app region.
 *
 * @param[in] data      Pointer to chunk data.
 * @param[in] offset    Cumulative byte offset of this chunk in the .sfw stream.
 * @param[in] length    Number of valid bytes in this chunk.
 * @param[in] user_ctx  Unused context pointer.
 * @return 0 on success, -1 on flash write error.
 *
 * @sideeffects Writes received firmware data to flash.
 */
static int nbu_block_handler(const uint8_t *data, uint32_t offset,
                             uint32_t length, void *user_ctx)
{
    (void)user_ctx;
    uint32_t i;

    /* Feed watchdog during transfer */
    platform_wdt_feed();

    /* Accumulate header bytes (first 256 bytes of stream) */
    for (i = 0; i < length; i++) {
        uint32_t pos = offset + i;
        if (pos < SFW_HEADER_SIZE) {
            ((uint8_t *)&g_sfw_header)[pos] = data[i];
        }
    }

    /* Detect header-complete transition */
    if (!g_header_complete && (offset + length) >= SFW_HEADER_SIZE) {
        g_header_complete = 1;
        g_flash_write_addr = platform_get_app_start_addr();
    }

    /* Write firmware data to flash (everything after the header) */
    if (g_header_complete) {
        uint32_t fw_start_in_block = 0;
        uint32_t fw_len = length;

        if (offset < SFW_HEADER_SIZE) {
            fw_start_in_block = SFW_HEADER_SIZE - offset;
            fw_len = length - fw_start_in_block;
        }

        if (fw_len > 0 && fw_start_in_block < length) {
            if (platform_flash_write(g_flash_write_addr,
                                     data + fw_start_in_block, fw_len) != 0) {
                g_write_error = 1;
                return -1;
            }
            g_flash_write_addr += fw_len;
        }
    }

    return 0;
}

/* ── NBU I/O callbacks (route through platform) ────────────────────────── */

static void nbu_uart_send(uint8_t byte)
{
    platform_uart_send_byte(byte);
}

static int nbu_uart_recv(uint8_t *byte, uint32_t timeout)
{
    return platform_uart_recv_byte(byte, timeout);
}

static uint32_t nbu_get_tick(void)
{
    return platform_get_tick_ms();
}

static const nbu_io_t nbu_io = {
    .uart_send_byte = nbu_uart_send,
    .uart_recv_byte = nbu_uart_recv,
    .get_tick_ms    = nbu_get_tick,
};

/* ── Firmware update mode ──────────────────────────────────────────────── */

/**
 * @brief Enter firmware update mode.
 *
 * Announces the bootloader on UART, erases application flash,
 * receives a .sfw file via the NBU framed half-duplex protocol, validates the
 * header, checks CRC-32, verifies the ECDSA-P256-SHA256 signature,
 * and reboots on success.
 *
 * On any failure, the app region is erased so the bootloader
 * stays resident on next boot.
 *
 * @sideeffects Erases and writes flash. Sends UART status messages.
 *              Resets the processor on success.
 */
static void enter_update_mode(void)
{
    uint32_t total_received = 0;
    sfw_result_t result;
    nbu_result_t xresult;

    /* Print bootloader banner */
    platform_uart_puts("\r\n[BOOT] Secure Bootloader v1.0\r\n");
    platform_uart_puts("[BOOT] Target: ");
    platform_uart_puts(platform_get_target_name());
    platform_uart_puts("\r\n");
    platform_uart_puts("[BOOT] Waiting for .sfw via NBU (framed half-duplex)...\r\n");
    platform_uart_puts("[BOOT] Send file now: nbu_send.py @115200 8N1\r\n");

    /* Initialize receive state */
    memset(&g_sfw_header, 0, sizeof(g_sfw_header));
    g_flash_write_addr = platform_get_app_start_addr();
    g_header_complete = 0;
    g_write_error = 0;

    /* Erase application region */
    platform_uart_puts("[BOOT] Erasing app region...\r\n");
    if (platform_flash_erase_app() != 0) {
        platform_uart_puts("[BOOT] ERROR: Flash erase failed!\r\n");
        return;
    }
    platform_uart_puts("[BOOT] Erase complete.\r\n");

    /* Receive .sfw via the NBU framed half-duplex protocol */
    xresult = nbu_receive(&nbu_io, my_bus_addr(), nbu_block_handler,
                          NULL, &total_received);

    if (xresult != NBU_OK) {
        platform_uart_puts("[BOOT] ERROR: NBU transfer failed (");
        uint8_t err_char = '0' + (uint8_t)(-(int)xresult);
        platform_uart_send_byte(err_char);
        platform_uart_puts(")\r\n");
        platform_flash_erase_app();
        return;
    }

    if (g_write_error) {
        platform_uart_puts("[BOOT] ERROR: Flash write error during transfer\r\n");
        platform_flash_erase_app();
        return;
    }

    platform_uart_puts("[BOOT] Transfer complete. Validating...\r\n");

    /* Validate .sfw header */
    result = sfw_validate_header(&g_sfw_header,
                                  platform_get_target_id(),
                                  platform_get_max_fw_size());
    if (result != SFW_OK) {
        platform_uart_puts("[BOOT] ERROR: Header validation failed (");
        uint8_t err_char = '0' + (uint8_t)result;
        platform_uart_send_byte(err_char);
        platform_uart_puts(")\r\n");
        platform_flash_erase_app();
        return;
    }

    platform_uart_puts("[BOOT] Header OK. Verifying CRC...\r\n");

    /* CRC-32 check */
    result = sfw_check_crc(
        (const uint8_t *)platform_get_app_start_addr(),
        g_sfw_header.fw_size,
        g_sfw_header.fw_crc32);
    if (result != SFW_OK) {
        platform_uart_puts("[BOOT] ERROR: CRC mismatch\r\n");
        platform_flash_erase_app();
        return;
    }

    platform_uart_puts("[BOOT] CRC OK. Verifying ECDSA signature...\r\n");

    platform_wdt_feed();

    /* ECDSA-P256-SHA256 signature verification */
    result = sfw_verify_signature(
        &g_sfw_header,
        (const uint8_t *)platform_get_app_start_addr(),
        g_sfw_header.fw_size,
        platform_get_ecdsa_pubkey());
    if (result != SFW_OK) {
        platform_uart_puts("[BOOT] ERROR: SIGNATURE INVALID! Rejecting firmware.\r\n");
        platform_flash_erase_app();
        return;
    }

    platform_uart_puts("[BOOT] Signature VALID. Firmware authenticated.\r\n");

    /* Clear update flag so we don't re-enter update mode */
    platform_clear_update_flag();

    platform_uart_puts("[BOOT] Update complete. Rebooting...\r\n\r\n");

    /* Flush UART */
    platform_delay_ms(100);

    /* Reset into new application */
    platform_reset();
}

/* ── Main entry point ──────────────────────────────────────────────────── */

int main(void)
{
    /* Initialize platform hardware (clocks, UART, GPIO, SysTick) */
    platform_init();

    /* Decide: update mode or boot application */
    if (platform_update_requested()) {
        platform_clear_update_flag();

        /* Enter NBU update mode — retry on failure */
        for (;;) {
            enter_update_mode();

            platform_uart_puts("[BOOT] Update failed. Retrying in 3 seconds...\r\n");
            platform_delay_ms(3000);
        }
    }

    /* Normal boot: jump to application */
    platform_deinit();
    platform_jump_to_app();
}
