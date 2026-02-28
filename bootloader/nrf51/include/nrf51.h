/**
 * @file nrf51.h
 * @brief Minimal nRF51822 register definitions for bootloader.
 *
 * Only includes peripherals used by the bootloader:
 *   - UART0 (for XMODEM communication)
 *   - NVMC (flash programming)
 *   - CLOCK (HFCLK startup)
 *   - GPIO (pin configuration)
 *   - POWER (reset, GPREGRET)
 *   - WDT (watchdog)
 */

#ifndef NRF51_H
#define NRF51_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Cortex-M0 System Registers ────────────────────────────────────────── */

#define SCB_VTOR_M0      (*(volatile uint32_t *)0xE000ED08U)
#define SCB_AIRCR_M0     (*(volatile uint32_t *)0xE000ED0CU)

#define SYSTICK_CTRL_M0   (*(volatile uint32_t *)0xE000E010U)
#define SYSTICK_LOAD_M0   (*(volatile uint32_t *)0xE000E014U)
#define SYSTICK_VAL_M0    (*(volatile uint32_t *)0xE000E018U)

/* ── CLOCK ─────────────────────────────────────────────────────────────── */

#define NRF_CLOCK_BASE         0x40000000U

#define NRF_CLOCK_TASKS_HFCLKSTART  (*(volatile uint32_t *)(NRF_CLOCK_BASE + 0x000U))
#define NRF_CLOCK_EVENTS_HFCLKSTARTED (*(volatile uint32_t *)(NRF_CLOCK_BASE + 0x100U))

/* ── POWER ─────────────────────────────────────────────────────────────── */

#define NRF_POWER_BASE         0x40000000U  /* Shares base with CLOCK */

#define NRF_POWER_RESETREAS    (*(volatile uint32_t *)(NRF_POWER_BASE + 0x400U))
#define NRF_POWER_GPREGRET_REG (*(volatile uint32_t *)(NRF_POWER_BASE + 0x51CU))
#define NRF_POWER_SYSTEMOFF    (*(volatile uint32_t *)(NRF_POWER_BASE + 0x500U))

/* ── GPIO ──────────────────────────────────────────────────────────────── */

#define NRF_GPIO_BASE          0x50000000U

#define NRF_GPIO_OUT           (*(volatile uint32_t *)(NRF_GPIO_BASE + 0x504U))
#define NRF_GPIO_OUTSET        (*(volatile uint32_t *)(NRF_GPIO_BASE + 0x508U))
#define NRF_GPIO_OUTCLR        (*(volatile uint32_t *)(NRF_GPIO_BASE + 0x50CU))
#define NRF_GPIO_IN            (*(volatile uint32_t *)(NRF_GPIO_BASE + 0x510U))
#define NRF_GPIO_DIR           (*(volatile uint32_t *)(NRF_GPIO_BASE + 0x514U))
#define NRF_GPIO_DIRSET        (*(volatile uint32_t *)(NRF_GPIO_BASE + 0x518U))

/* Pin configuration registers: NRF_GPIO_PIN_CNF[n] at offset 0x700 + n*4 */
#define NRF_GPIO_PIN_CNF(n)    (*(volatile uint32_t *)(NRF_GPIO_BASE + 0x700U + (n) * 4U))

/* PIN_CNF field values */
#define GPIO_PIN_CNF_DIR_INPUT      (0U << 0)
#define GPIO_PIN_CNF_DIR_OUTPUT     (1U << 0)
#define GPIO_PIN_CNF_INPUT_CONNECT  (0U << 1)
#define GPIO_PIN_CNF_INPUT_DISCONNECT (1U << 1)
#define GPIO_PIN_CNF_PULL_DISABLED  (0U << 2)
#define GPIO_PIN_CNF_PULL_DOWN      (1U << 2)
#define GPIO_PIN_CNF_PULL_UP        (3U << 2)
#define GPIO_PIN_CNF_DRIVE_S0S1     (0U << 8)

/* ── UART0 ─────────────────────────────────────────────────────────────── */

#define NRF_UART0_BASE         0x40002000U

#define NRF_UART0_TASKS_STARTRX (*(volatile uint32_t *)(NRF_UART0_BASE + 0x000U))
#define NRF_UART0_TASKS_STOPRX  (*(volatile uint32_t *)(NRF_UART0_BASE + 0x004U))
#define NRF_UART0_TASKS_STARTTX (*(volatile uint32_t *)(NRF_UART0_BASE + 0x008U))
#define NRF_UART0_TASKS_STOPTX  (*(volatile uint32_t *)(NRF_UART0_BASE + 0x00CU))

#define NRF_UART0_EVENTS_RXDRDY (*(volatile uint32_t *)(NRF_UART0_BASE + 0x108U))
#define NRF_UART0_EVENTS_TXDRDY (*(volatile uint32_t *)(NRF_UART0_BASE + 0x11CU))
#define NRF_UART0_EVENTS_ERROR  (*(volatile uint32_t *)(NRF_UART0_BASE + 0x124U))

#define NRF_UART0_ENABLE       (*(volatile uint32_t *)(NRF_UART0_BASE + 0x500U))
#define NRF_UART0_PSELRXD      (*(volatile uint32_t *)(NRF_UART0_BASE + 0x514U))
#define NRF_UART0_PSELTXD      (*(volatile uint32_t *)(NRF_UART0_BASE + 0x50CU))
#define NRF_UART0_RXD          (*(volatile uint32_t *)(NRF_UART0_BASE + 0x518U))
#define NRF_UART0_TXD          (*(volatile uint32_t *)(NRF_UART0_BASE + 0x51CU))
#define NRF_UART0_BAUDRATE     (*(volatile uint32_t *)(NRF_UART0_BASE + 0x524U))
#define NRF_UART0_CONFIG       (*(volatile uint32_t *)(NRF_UART0_BASE + 0x56CU))

/* UART enable value */
#define UART_ENABLE_ENABLED    4U

/* UART baud rate register values */
#define UART_BAUDRATE_115200   0x01D7E000U

/* ── NVMC (Non-Volatile Memory Controller) ─────────────────────────────── */

#define NRF_NVMC_BASE          0x4001E000U

#define NRF_NVMC_READY         (*(volatile uint32_t *)(NRF_NVMC_BASE + 0x400U))
#define NRF_NVMC_CONFIG        (*(volatile uint32_t *)(NRF_NVMC_BASE + 0x504U))
#define NRF_NVMC_ERASEPAGE     (*(volatile uint32_t *)(NRF_NVMC_BASE + 0x508U))
#define NRF_NVMC_ERASEUICR     (*(volatile uint32_t *)(NRF_NVMC_BASE + 0x514U))

/* NVMC_CONFIG values */
#define NVMC_CONFIG_REN         0U   /**< Read-only */
#define NVMC_CONFIG_WEN         1U   /**< Write enable */
#define NVMC_CONFIG_EEN         2U   /**< Erase enable */

/* ── WDT (Watchdog Timer) ──────────────────────────────────────────────── */

#define NRF_WDT_BASE           0x40010000U

#define NRF_WDT_TASKS_START    (*(volatile uint32_t *)(NRF_WDT_BASE + 0x000U))
#define NRF_WDT_EVENTS_TIMEOUT (*(volatile uint32_t *)(NRF_WDT_BASE + 0x100U))
#define NRF_WDT_RUNSTATUS      (*(volatile uint32_t *)(NRF_WDT_BASE + 0x400U))
#define NRF_WDT_REQSTATUS      (*(volatile uint32_t *)(NRF_WDT_BASE + 0x404U))
#define NRF_WDT_CRV            (*(volatile uint32_t *)(NRF_WDT_BASE + 0x504U))
#define NRF_WDT_RREN           (*(volatile uint32_t *)(NRF_WDT_BASE + 0x508U))
#define NRF_WDT_CONFIG         (*(volatile uint32_t *)(NRF_WDT_BASE + 0x50CU))
#define NRF_WDT_RR0            (*(volatile uint32_t *)(NRF_WDT_BASE + 0x600U))

#define WDT_RR_RELOAD_VALUE    0x6E524635U  /**< Reload value for WDT */

/* ── System reset ──────────────────────────────────────────────────────── */

static inline void nrf_system_reset(void)
{
    SCB_AIRCR_M0 = (0x05FAUL << 16) | (1UL << 2);
    for (;;) { __asm volatile ("nop"); }
}

#ifdef __cplusplus
}
#endif

#endif /* NRF51_H */
