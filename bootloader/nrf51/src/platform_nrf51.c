/**
 * @file platform_nrf51.c
 * @brief Platform implementation for nRF51822 (BLE Bluetooth SoC).
 *
 * Implements the platform.h interface for the nRF51822 target.
 * Uses existing nrf51_uart and nrf51_flash drivers for low-level I/O.
 * The nRF51 bootloader sits at 0x0003C000 (top of flash) and is
 * invoked by the Nordic MBR.
 */

#include "platform.h"
#include "nrf51.h"
#include "bootloader_config.h"
#include "nrf51_uart.h"
#include "nrf51_flash.h"
#include "fw_header.h"
#include "ecdsa.h"

/* ── System tick (shared with nrf51_uart.c) ────────────────────────────── */

volatile uint32_t g_tick_ms = 0;

/* ── SysTick handler (called from vector table in startup.s) ───────────── */

void SysTick_Handler(void)
{
    g_tick_ms++;
}

/* ── Internal clock setup ──────────────────────────────────────────────── */

/**
 * @brief Start 16 MHz HFCLK from external crystal.
 *
 * @sideeffects Configures CLOCK peripheral and waits for HFCLK ready.
 */
static void clock_init(void)
{
    NRF_CLOCK_TASKS_HFCLKSTART = 1;
    while (NRF_CLOCK_EVENTS_HFCLKSTARTED == 0) { /* Wait */ }
    NRF_CLOCK_EVENTS_HFCLKSTARTED = 0;
}

/**
 * @brief Configure SysTick for 1 ms interrupt at 16 MHz.
 *
 * @sideeffects Configures SysTick peripheral.
 */
static void systick_init(void)
{
    SYSTICK_LOAD_M0 = (16000000U / 1000U) - 1U;
    SYSTICK_VAL_M0 = 0;
    SYSTICK_CTRL_M0 = 0x07;  /* Enable, interrupt, processor clock */
}

/* ── Platform interface implementation ─────────────────────────────────── */

void platform_init(void)
{
    clock_init();
    systick_init();
    nrf_uart_init();

    /* Feed WDT if it was started by a previous app */
    platform_wdt_feed();
}

void platform_deinit(void)
{
    /* Disable SysTick */
    SYSTICK_CTRL_M0 = 0;
}

void platform_uart_send_byte(uint8_t byte)
{
    nrf_uart_send_byte(byte);
}

int platform_uart_recv_byte(uint8_t *byte, uint32_t timeout)
{
    return nrf_uart_recv_byte(byte, timeout);
}

void platform_uart_puts(const char *str)
{
    nrf_uart_puts(str);
}

int platform_flash_erase_app(void)
{
    return nrf_flash_erase_app_region();
}

int platform_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    return nrf_flash_write(addr, data, len);
}

int platform_update_requested(void)
{
    /* Trigger 1: GPREGRET magic from application */
    if ((NRF_POWER_GPREGRET_REG & 0xFF) == BOOT_ENTER_MAGIC) {
        NRF_POWER_GPREGRET_REG = 0;
        return 1;
    }

    /* Trigger 2: Update flag in settings page */
    if (nrf_flash_is_update_requested()) {
        return 1;
    }

    /* Trigger 3: No valid application */
    if (!platform_is_app_valid()) {
        return 1;
    }

    return 0;
}

void platform_clear_update_flag(void)
{
    nrf_flash_clear_update_flag();
    NRF_POWER_GPREGRET_REG = 0;
}

int platform_is_app_valid(void)
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

void platform_jump_to_app(void)
{
    uint32_t app_sp    = *(volatile uint32_t *)APP_START_ADDR;
    uint32_t app_entry = *(volatile uint32_t *)(APP_START_ADDR + 4);

    /*
     * On Cortex-M0, VTOR may not be available. The Nordic MBR
     * handles interrupt forwarding to the application/SoftDevice.
     */

    __asm volatile (
        "MSR MSP, %0    \n"
        "BX  %1         \n"
        :
        : "r" (app_sp), "r" (app_entry)
    );

    for (;;) {}  /* Never reached */
}

uint32_t platform_get_app_start_addr(void)
{
    return APP_START_ADDR;
}

uint32_t platform_get_tick_ms(void)
{
    return g_tick_ms;
}

void platform_delay_ms(uint32_t ms)
{
    uint32_t start = g_tick_ms;
    while ((g_tick_ms - start) < ms) {
        platform_wdt_feed();
    }
}

void platform_wdt_feed(void)
{
    if (NRF_WDT_RUNSTATUS) {
        NRF_WDT_RR0 = WDT_RR_RELOAD_VALUE;
    }
}

/* ── ECDSA public key (embedded in bootloader flash) ───────────────────── */

static const uint8_t ecdsa_pubkey[ECDSA_P256_PUBKEY_SIZE] = ECDSA_PUBLIC_KEY;

const uint8_t *platform_get_ecdsa_pubkey(void)
{
    return ecdsa_pubkey;
}

const char *platform_get_target_name(void)
{
    return "nRF51822";
}

uint8_t platform_get_target_id(void)
{
    return MY_TARGET_ID;
}

uint32_t platform_get_max_fw_size(void)
{
    return SFW_MAX_FW_SIZE_NRF51;
}

void platform_reset(void)
{
    nrf_system_reset();
    /* nrf_system_reset is noreturn but GCC may not see through it */
    for (;;) {}
}
