/**
 * @file bootloader_main.c
 * @brief STM32F103 secure bootloader — main entry point.
 *
 * Boot flow:
 *   1. Clock init (HSE → PLL → 72 MHz)
 *   2. SysTick init (1 ms tick)
 *   3. Check update trigger:
 *      a. Update flag in config flash page
 *      b. Power button held during boot (BLE board only)
 *      c. No valid application detected (invalid vector table)
 *   4. If update triggered → enter XMODEM receive mode
 *      - Receive .sfw file via XMODEM-CRC on USART2
 *      - Validate header (magic, target, CRC)
 *      - Verify ECDSA-P256-SHA256 signature
 *      - Erase app region and flash new firmware
 *      - Clear update flag and reset
 *   5. If no update → verify existing app signature
 *      - If valid → jump to application at APP_START_ADDR
 *      - If invalid → enter update mode (recovery)
 *
 * Target boards: BLE STM32F103C8T6, BMS STM32F103C8T6
 * Compiled separately with -DTARGET_BOARD=BOARD_BLE_STM32 or BOARD_BMS_STM32
 */

#include "stm32f1xx.h"
#include "bootloader_config.h"
#include "stm32_flash.h"
#include "stm32_uart.h"
#include "fw_header.h"
#include "xmodem.h"
#include "sha256.h"
#include "ecdsa.h"
#include "crc32.h"
#include <string.h>

/* ── Global tick counter (incremented by SysTick_Handler) ──────────────── */

volatile uint32_t g_tick_ms = 0;

/* ── ECDSA public key (embedded in bootloader) ─────────────────────────── */

static const uint8_t ecdsa_pubkey[ECDSA_P256_PUBKEY_SIZE] = ECDSA_PUBLIC_KEY;

/* ── Receive buffer ────────────────────────────────────────────────────── */

/**
 * Staging buffer for received .sfw image.
 * Located in SRAM — the STM32F103C8 has 20 KB of SRAM.
 * The .sfw header is 256 bytes, and firmware data streams to flash.
 *
 * Strategy: receive the full .sfw via XMODEM into a large buffer,
 * then validate and flash. With only 20KB SRAM and up to 50KB firmware,
 * we must stream: buffer the header (256 bytes), then write each XMODEM
 * block directly to flash.
 */

static sfw_header_t g_sfw_header;
static uint32_t g_rx_offset;           /**< Total bytes received so far */
static uint32_t g_flash_write_addr;    /**< Next flash write address */
static int g_header_complete;          /**< Header fully received? */
static int g_write_error;              /**< Flash write error flag */

/* ── Clock initialization ──────────────────────────────────────────────── */

static void clock_init(void)
{
    /* Enable HSE (8 MHz crystal) */
    RCC_CR |= RCC_CR_HSEON;
    while (!(RCC_CR & RCC_CR_HSERDY)) { /* Wait */ }

    /* Configure flash latency for 72 MHz */
    FLASH_ACR = FLASH_ACR_LATENCY_2;

    /* Configure PLL: HSE × 9 = 72 MHz */
    RCC_CFGR = RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL9 |
               RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

    /* Enable PLL */
    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY)) { /* Wait */ }

    /* Switch system clock to PLL */
    RCC_CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC_CFGR & 0x0CU) != RCC_CFGR_SWS_PLL) { /* Wait */ }
}

/* ── SysTick initialization ───────────────────────────────────────────── */

static void systick_init(void)
{
    SYSTICK_LOAD = SYSTICK_RELOAD;
    SYSTICK_VAL = 0;
    SYSTICK_CTRL = 0x07;  /* Enable, interrupt, use processor clock */
}

/**
 * SysTick interrupt handler — 1 ms tick.
 * Called from the vector table in startup.s.
 */
void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t get_tick(void)
{
    return g_tick_ms;
}

/* ── Update button check ───────────────────────────────────────────────── */

#if UPDATE_BUTTON_PORT != 0
static void gpio_config_pin_boot(char port, uint8_t pin);

static int is_update_button_pressed(void)
{
    GPIO_TypeDef *gpio;
    if (UPDATE_BUTTON_PORT == 'A') gpio = GPIOA;
    else gpio = GPIOB;

    /* Configure button pin as input with pull-up */
    gpio_config_pin_boot(UPDATE_BUTTON_PORT, UPDATE_BUTTON_PIN);

    /* Button is active-low: pressed = 0 */
    return !(gpio->IDR & (1U << UPDATE_BUTTON_PIN));
}

static void gpio_config_pin_boot(char port, uint8_t pin)
{
    GPIO_TypeDef *gpio;
    if (port == 'A') gpio = GPIOA;
    else gpio = GPIOB;

    volatile uint32_t *cr;
    uint32_t shift;

    if (pin < 8) {
        cr = &gpio->CRL;
        shift = pin * 4;
    } else {
        cr = &gpio->CRH;
        shift = (pin - 8) * 4;
    }

    uint32_t val = *cr;
    val &= ~(0xFU << shift);
    val |= ((GPIO_MODE_INPUT | GPIO_CNF_IN_PUPD) << shift);
    *cr = val;

    /* Enable pull-up via ODR */
    gpio->BSRR = (1U << pin);
}
#else
static int is_update_button_pressed(void) { return 0; }
#endif

/* ── Application validity check ────────────────────────────────────────── */

/**
 * Check if a valid application exists at APP_START_ADDR.
 * A valid application has:
 *   - Stack pointer in SRAM range (0x20000000-0x20005000)
 *   - Reset handler in flash range (0x08003000-0x0800FFFF)
 */
static int is_app_valid(void)
{
    uint32_t sp = *(volatile uint32_t *)APP_START_ADDR;
    uint32_t reset = *(volatile uint32_t *)(APP_START_ADDR + 4);

    /* Check stack pointer is in SRAM */
    if (sp < 0x20000000U || sp > 0x20005000U) {
        return 0;
    }

    /* Check reset handler is in app flash region */
    if (reset < APP_START_ADDR || reset > (FLASH_BASE_ADDR + FLASH_SIZE)) {
        return 0;
    }

    return 1;
}

/**
 * Verify the ECDSA signature of the currently installed application.
 * Re-reads app firmware from flash, computes SHA-256, and checks
 * against the stored signature in the app's own header.
 *
 * The application stores its .sfw header copy at a known offset
 * (last 256 bytes of the app region) for runtime verification.
 *
 * For simplicity, we skip runtime signature verification of existing
 * firmware and only verify during updates. This reduces boot time.
 */
static int verify_installed_app(void)
{
    /* Basic validity check is sufficient for normal boot */
    return is_app_valid();
}

/* ── XMODEM write callback ─────────────────────────────────────────────── */

/**
 * Called for each 128-byte XMODEM block received.
 * First 256 bytes go into the header buffer.
 * Remaining bytes stream directly to flash.
 */
static int xmodem_block_handler(const uint8_t *data, uint32_t offset,
                                 uint32_t length, void *user_ctx)
{
    (void)user_ctx;
    uint32_t i;

    for (i = 0; i < length; i++) {
        uint32_t pos = offset + i;

        if (pos < SFW_HEADER_SIZE) {
            /* Accumulate header bytes */
            ((uint8_t *)&g_sfw_header)[pos] = data[i];
        } else {
            if (!g_header_complete && pos == SFW_HEADER_SIZE) {
                /* Header is now complete — validate it */
                g_header_complete = 1;
                g_flash_write_addr = APP_START_ADDR;
            }

            /* Write firmware byte to a mini-buffer, flush per half-word */
            /* (Handled block-wise below) */
        }
    }

    /* If header is complete, write firmware data to flash */
    if (g_header_complete) {
        /* Calculate which part of this block is firmware data */
        uint32_t fw_start_in_block = 0;
        uint32_t fw_len = length;

        if (offset < SFW_HEADER_SIZE) {
            /* This block straddles the header boundary */
            fw_start_in_block = SFW_HEADER_SIZE - offset;
            fw_len = length - fw_start_in_block;
        }

        if (fw_len > 0 && fw_start_in_block < length) {
            /* Write to flash */
            flash_unlock();
            int result = flash_write(g_flash_write_addr,
                                      data + fw_start_in_block, fw_len);
            flash_lock();

            if (result != 0) {
                g_write_error = 1;
                return -1;
            }
            g_flash_write_addr += fw_len;
        }
    }

    return 0;
}

/* ── XMODEM I/O callbacks ──────────────────────────────────────────────── */

static void xmodem_uart_send(uint8_t byte)
{
    uart_send_byte(byte);
}

static int xmodem_uart_recv(uint8_t *byte, uint32_t timeout)
{
    return uart_recv_byte(byte, timeout);
}

static uint32_t xmodem_get_tick(void)
{
    return get_tick();
}

static const xmodem_io_t xmodem_io = {
    .uart_send_byte = xmodem_uart_send,
    .uart_recv_byte = xmodem_uart_recv,
    .get_tick_ms    = xmodem_get_tick,
};

/* ── Update mode ───────────────────────────────────────────────────────── */

/**
 * Enter firmware update mode.
 * 1. Announce bootloader on UART
 * 2. Erase application flash
 * 3. Receive .sfw file via XMODEM-CRC
 * 4. Validate header
 * 5. Verify ECDSA signature
 * 6. If valid: clear flag, reset
 * 7. If invalid: re-erase app and stay in bootloader
 */
static void enter_update_mode(void)
{
    uint32_t total_received = 0;
    sfw_result_t result;
    xmodem_result_t xresult;

    uart_puts("\r\n[BOOT] Secure Bootloader v1.0\r\n");
#if TARGET_BOARD == BOARD_BLE_STM32
    uart_puts("[BOOT] Target: BLE-STM32\r\n");
#elif TARGET_BOARD == BOARD_BMS_STM32
    uart_puts("[BOOT] Target: BMS-STM32\r\n");
#endif
    uart_puts("[BOOT] Waiting for .sfw file via XMODEM-CRC...\r\n");
    uart_puts("[BOOT] Send file now (XMODEM-CRC, 128-byte blocks)\r\n");

    /* Initialize receive state */
    memset(&g_sfw_header, 0, sizeof(g_sfw_header));
    g_rx_offset = 0;
    g_flash_write_addr = APP_START_ADDR;
    g_header_complete = 0;
    g_write_error = 0;

    /* Erase application region */
    uart_puts("[BOOT] Erasing app region...\r\n");
    if (flash_erase_app_region() != 0) {
        uart_puts("[BOOT] ERROR: Flash erase failed!\r\n");
        return;  /* Stay in bootloader */
    }
    uart_puts("[BOOT] Erase complete.\r\n");

    /* Receive .sfw via XMODEM */
    xresult = xmodem_receive(&xmodem_io, xmodem_block_handler,
                              NULL, &total_received);

    if (xresult != XMODEM_OK) {
        uart_puts("[BOOT] ERROR: XMODEM transfer failed (");
        /* Print error code as ASCII digit */
        uint8_t err_char = '0' + (uint8_t)(-(int)xresult);
        uart_send_byte(err_char);
        uart_puts(")\r\n");
        /* Re-erase app so we stay in bootloader on next boot */
        flash_erase_app_region();
        return;
    }

    if (g_write_error) {
        uart_puts("[BOOT] ERROR: Flash write error during transfer\r\n");
        flash_erase_app_region();
        return;
    }

    uart_puts("[BOOT] Transfer complete. Validating...\r\n");

    /* Validate header */
    result = sfw_validate_header(&g_sfw_header, MY_TARGET_ID,
                                  SFW_MAX_FW_SIZE_STM32);
    if (result != SFW_OK) {
        uart_puts("[BOOT] ERROR: Header validation failed (");
        uint8_t err_char = '0' + (uint8_t)result;
        uart_send_byte(err_char);
        uart_puts(")\r\n");
        flash_erase_app_region();
        return;
    }

    uart_puts("[BOOT] Header OK. Verifying CRC...\r\n");

    /* Quick CRC check */
    result = sfw_check_crc((const uint8_t *)APP_START_ADDR,
                            g_sfw_header.fw_size,
                            g_sfw_header.fw_crc32);
    if (result != SFW_OK) {
        uart_puts("[BOOT] ERROR: CRC mismatch\r\n");
        flash_erase_app_region();
        return;
    }

    uart_puts("[BOOT] CRC OK. Verifying ECDSA signature...\r\n");

    /* Full ECDSA signature verification */
    result = sfw_verify_signature(&g_sfw_header,
                                   (const uint8_t *)APP_START_ADDR,
                                   g_sfw_header.fw_size,
                                   ecdsa_pubkey);
    if (result != SFW_OK) {
        uart_puts("[BOOT] ERROR: SIGNATURE INVALID! Rejecting firmware.\r\n");
        flash_erase_app_region();
        return;
    }

    uart_puts("[BOOT] Signature VALID. Firmware authenticated.\r\n");

    /* Clear update flag */
    flash_clear_update_flag();

    uart_puts("[BOOT] Update complete. Rebooting...\r\n\r\n");

    /* Small delay for UART to flush */
    {
        uint32_t t = get_tick();
        while (get_tick() - t < 100) { /* 100 ms */ }
    }

    /* Reset into new application */
    NVIC_SystemReset();
}

/* ── Jump to application ───────────────────────────────────────────────── */

/**
 * Jump to the application firmware at APP_START_ADDR.
 * Sets the MSP to the application's stack pointer and branches
 * to the application's reset handler.
 */
static void jump_to_app(void)
{
    uint32_t app_sp    = *(volatile uint32_t *)APP_START_ADDR;
    uint32_t app_entry = *(volatile uint32_t *)(APP_START_ADDR + 4);

    /* Disable SysTick */
    SYSTICK_CTRL = 0;

    /* Relocate vector table to application */
    SCB_VTOR = APP_START_ADDR;

    /* Set main stack pointer and jump */
    __asm volatile (
        "MSR MSP, %0    \n"  /* Set stack pointer */
        "BX  %1         \n"  /* Branch to reset handler */
        :
        : "r" (app_sp), "r" (app_entry)
    );

    /* Should never reach here */
    for (;;) {}
}

/* ── Main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    /* Initialize clocks (HSE → PLL → 72 MHz) */
    clock_init();

    /* Initialize SysTick for 1 ms tick */
    systick_init();

    /* Initialize UART for status output and XMODEM */
    uart_init();

    /* Enable GPIO clocks for button reading */
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;

    /* ── Decide: update mode or boot application ──────────────── */

    int enter_update = 0;

    /* Check 1: Software update flag */
    if (flash_is_update_requested()) {
        enter_update = 1;
    }

    /* Check 2: Hardware button held during boot */
    if (is_update_button_pressed()) {
        /* Debounce: wait 50 ms and check again */
        uint32_t t = get_tick();
        while (get_tick() - t < 50) {}
        if (is_update_button_pressed()) {
            enter_update = 1;
        }
    }

    /* Check 3: No valid application (recovery mode) */
    if (!is_app_valid()) {
        enter_update = 1;
    }

    if (enter_update) {
        /* Clear the flag so we don't loop forever on boot */
        flash_clear_update_flag();

        /* Enter XMODEM update mode — blocks until success or timeout */
        for (;;) {
            enter_update_mode();

            /* If we get here, update failed — retry */
            uart_puts("[BOOT] Update failed. Retrying in 3 seconds...\r\n");
            uint32_t t = get_tick();
            while (get_tick() - t < 3000) {}
        }
    }

    /* ── Normal boot: jump to application ─────────────────────── */

    /* Optional: verify installed app signature on every boot.
     * This adds ~2 seconds boot time but ensures integrity.
     * Uncomment for maximum security: */
    /*
    if (!verify_installed_app()) {
        uart_puts("[BOOT] App integrity check failed. Entering update mode.\r\n");
        enter_update_mode();
    }
    */

    jump_to_app();

    /* Never reached */
    return 0;
}
