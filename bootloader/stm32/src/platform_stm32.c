/**
 * @file platform_stm32.c
 * @brief Platform implementation for STM32F103C8T6 (BLE and BMS boards).
 *
 * Implements the platform.h interface for the STM32F103C8T6 target.
 * Uses existing stm32_uart and stm32_flash drivers for low-level I/O.
 * Board selection via -DTARGET_BOARD=BOARD_BLE_STM32 or BOARD_BMS_STM32.
 */

#include "platform.h"
#include "stm32f1xx.h"
#include "bootloader_config.h"
#include "stm32_uart.h"
#include "stm32_flash.h"
#include "fw_header.h"
#include "ecdsa.h"

/* ── System tick (shared with stm32_uart.c) ────────────────────────────── */

volatile uint32_t g_tick_ms = 0;

/* ── SysTick handler (called from vector table in startup.s) ───────────── */

void SysTick_Handler(void)
{
    g_tick_ms++;
}

/* ── Internal clock setup ──────────────────────────────────────────────── */

/**
 * @brief Configure system clocks: HSE 8 MHz → PLL × 9 → 72 MHz.
 *
 * @sideeffects Configures RCC, flash latency, and PLL.
 */
static void clock_init(void)
{
    /* Enable HSE (8 MHz crystal) */
    RCC_CR |= RCC_CR_HSEON;
    while (!(RCC_CR & RCC_CR_HSERDY)) { /* Wait */ }

    /* Flash latency: 2 wait states for 72 MHz */
    FLASH_ACR = FLASH_ACR_LATENCY_2;

    /* PLL: HSE × 9 = 72 MHz, APB1 = 36 MHz, APB2 = 72 MHz */
    RCC_CFGR = RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL9 |
               RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

    /* Enable PLL */
    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY)) { /* Wait */ }

    /* Switch to PLL */
    RCC_CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC_CFGR & 0x0CU) != RCC_CFGR_SWS_PLL) { /* Wait */ }
}

/**
 * @brief Configure SysTick for 1 ms interrupt at 72 MHz.
 *
 * @sideeffects Configures SysTick peripheral.
 */
static void systick_init(void)
{
    SYSTICK_LOAD = (72000000U / 1000U) - 1U;
    SYSTICK_VAL = 0;
    SYSTICK_CTRL = 0x07;  /* Enable, interrupt, processor clock */
}

/* ── Update button check (BLE board only) ──────────────────────────────── */

#if UPDATE_BUTTON_PORT != 0
/**
 * @brief Configure GPIO pin as input with pull-up.
 *
 * @param[in] port  Port letter ('A' or 'B').
 * @param[in] pin   Pin number (0–15).
 *
 * @sideeffects Configures GPIO CRL/CRH and sets pull-up via BSRR.
 */
static void gpio_config_input_pullup(char port, uint8_t pin)
{
    GPIO_TypeDef *gpio = (port == 'A') ? GPIOA : GPIOB;
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

    gpio->BSRR = (1U << pin);  /* Enable pull-up */
}

static int is_update_button_pressed(void)
{
    GPIO_TypeDef *gpio = (UPDATE_BUTTON_PORT == 'A') ? GPIOA : GPIOB;

    gpio_config_input_pullup(UPDATE_BUTTON_PORT, UPDATE_BUTTON_PIN);

    /* Active-low: pressed = 0 */
    return !(gpio->IDR & (1U << UPDATE_BUTTON_PIN));
}
#endif

/* ── Platform interface implementation ─────────────────────────────────── */

void platform_init(void)
{
    clock_init();
    systick_init();

    /* Enable GPIO clocks */
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;

    uart_init();
}

void platform_deinit(void)
{
    /* Disable SysTick */
    SYSTICK_CTRL = 0;
}

void platform_uart_send_byte(uint8_t byte)
{
    uart_send_byte(byte);
}

int platform_uart_recv_byte(uint8_t *byte, uint32_t timeout)
{
    return uart_recv_byte(byte, timeout);
}

void platform_uart_puts(const char *str)
{
    uart_puts(str);
}

int platform_flash_erase_app(void)
{
    return flash_erase_app_region();
}

int platform_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    int result;
    flash_unlock();
    result = flash_write(addr, data, len);
    flash_lock();
    return result;
}

int platform_update_requested(void)
{
    /* Check 1: Software update flag */
    if (flash_is_update_requested()) {
        return 1;
    }

    /* Check 2: Hardware button (BLE board — PB12) */
#if UPDATE_BUTTON_PORT != 0
    if (is_update_button_pressed()) {
        /* Debounce: 50 ms */
        platform_delay_ms(50);
        if (is_update_button_pressed()) {
            return 1;
        }
    }
#endif

    /* Check 3: No valid application */
    if (!platform_is_app_valid()) {
        return 1;
    }

    return 0;
}

void platform_clear_update_flag(void)
{
    flash_clear_update_flag();
}

int platform_is_app_valid(void)
{
    uint32_t sp    = *(volatile uint32_t *)APP_START_ADDR;
    uint32_t reset = *(volatile uint32_t *)(APP_START_ADDR + 4);

    /* Stack pointer must be in SRAM (0x20000000–0x20005000) */
    if (sp < 0x20000000U || sp > 0x20005000U) {
        return 0;
    }

    /* Reset handler must be in app flash region */
    if (reset < APP_START_ADDR || reset > (FLASH_BASE_ADDR + FLASH_SIZE)) {
        return 0;
    }

    return 1;
}

void platform_jump_to_app(void)
{
    uint32_t app_sp    = *(volatile uint32_t *)APP_START_ADDR;
    uint32_t app_entry = *(volatile uint32_t *)(APP_START_ADDR + 4);

    /* Relocate vector table to application */
    SCB_VTOR = APP_START_ADDR;

    /* Set MSP and branch to reset handler */
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
    while ((g_tick_ms - start) < ms) { /* Spin */ }
}

void platform_wdt_feed(void)
{
    /* STM32 IWDG feed — safe to call even if IWDG not started */
    IWDG_KR = IWDG_KEY_RELOAD;
}

/* ── ECDSA public key (embedded in bootloader flash) ───────────────────── */

static const uint8_t ecdsa_pubkey[ECDSA_P256_PUBKEY_SIZE] = ECDSA_PUBLIC_KEY;

const uint8_t *platform_get_ecdsa_pubkey(void)
{
    return ecdsa_pubkey;
}

const char *platform_get_target_name(void)
{
#if TARGET_BOARD == BOARD_BLE_STM32
    return "BLE-STM32";
#elif TARGET_BOARD == BOARD_BMS_STM32
    return "BMS-STM32";
#else
    return "UNKNOWN";
#endif
}

uint8_t platform_get_target_id(void)
{
    return MY_TARGET_ID;
}

uint32_t platform_get_max_fw_size(void)
{
    return SFW_MAX_FW_SIZE_STM32;
}

void platform_reset(void)
{
    NVIC_SystemReset();
    /* NVIC_SystemReset is noreturn but GCC may not see through it */
    for (;;) {}
}
