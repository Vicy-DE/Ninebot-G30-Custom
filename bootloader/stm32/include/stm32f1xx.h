/**
 * @file stm32f1xx.h
 * @brief Minimal STM32F103 register definitions for bootloader.
 *
 * Only includes registers actually used by the bootloader — not a full CMSIS.
 * This keeps the bootloader dependency-free and self-contained.
 */

#ifndef STM32F1XX_H
#define STM32F1XX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Cortex-M3 System Registers ────────────────────────────────────────── */

#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08U)  /**< Vector Table Offset */
#define SCB_AIRCR         (*(volatile uint32_t *)0xE000ED0CU)  /**< App Interrupt/Reset Control */

#define SYSTICK_CTRL      (*(volatile uint32_t *)0xE000E010U)
#define SYSTICK_LOAD      (*(volatile uint32_t *)0xE000E014U)
#define SYSTICK_VAL       (*(volatile uint32_t *)0xE000E018U)

/* ── RCC (Reset and Clock Control) ─────────────────────────────────────── */

#define RCC_BASE           0x40021000U
#define RCC_CR            (*(volatile uint32_t *)(RCC_BASE + 0x00U))
#define RCC_CFGR          (*(volatile uint32_t *)(RCC_BASE + 0x04U))
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x18U))
#define RCC_APB1ENR       (*(volatile uint32_t *)(RCC_BASE + 0x1CU))

/* RCC_CR bits */
#define RCC_CR_HSEON       (1U << 16)
#define RCC_CR_HSERDY      (1U << 17)
#define RCC_CR_PLLON       (1U << 24)
#define RCC_CR_PLLRDY      (1U << 25)

/* RCC_CFGR bits */
#define RCC_CFGR_SW_PLL    (0x2U << 0)
#define RCC_CFGR_SWS_PLL   (0x2U << 2)
#define RCC_CFGR_PLLSRC    (1U << 16)   /**< PLL source = HSE */
#define RCC_CFGR_PLLMUL9   (0x7U << 18) /**< PLL × 9 = 72 MHz */
#define RCC_CFGR_PPRE1_DIV2 (0x4U << 8) /**< APB1 = HCLK / 2 */
#define RCC_CFGR_PPRE2_DIV1 (0x0U << 11)

/* RCC_APB2ENR bits */
#define RCC_APB2ENR_AFIOEN  (1U << 0)
#define RCC_APB2ENR_IOPAEN  (1U << 2)
#define RCC_APB2ENR_IOPBEN  (1U << 3)
#define RCC_APB2ENR_USART1EN (1U << 14)

/* RCC_APB1ENR bits */
#define RCC_APB1ENR_USART2EN (1U << 17)

/* ── FLASH (Flash Memory Interface) ───────────────────────────────────── */

#define FLASH_R_BASE       0x40022000U
#define FLASH_ACR         (*(volatile uint32_t *)(FLASH_R_BASE + 0x00U))
#define FLASH_KEYR        (*(volatile uint32_t *)(FLASH_R_BASE + 0x04U))
#define FLASH_SR          (*(volatile uint32_t *)(FLASH_R_BASE + 0x0CU))
#define FLASH_CR          (*(volatile uint32_t *)(FLASH_R_BASE + 0x10U))
#define FLASH_AR          (*(volatile uint32_t *)(FLASH_R_BASE + 0x14U))
#define FLASH_OBR         (*(volatile uint32_t *)(FLASH_R_BASE + 0x1CU))
#define FLASH_WRPR        (*(volatile uint32_t *)(FLASH_R_BASE + 0x20U))

/* FLASH_ACR bits */
#define FLASH_ACR_LATENCY_2  (2U << 0)  /**< 2 wait states (48-72 MHz) */

/* FLASH_SR bits */
#define FLASH_SR_BSY       (1U << 0)
#define FLASH_SR_EOP       (1U << 5)

/* FLASH_CR bits */
#define FLASH_CR_PG        (1U << 0)    /**< Programming */
#define FLASH_CR_PER       (1U << 1)    /**< Page Erase */
#define FLASH_CR_STRT      (1U << 6)    /**< Start */
#define FLASH_CR_LOCK      (1U << 7)    /**< Lock */

/* Flash unlock keys */
#define FLASH_KEY1         0x45670123U
#define FLASH_KEY2         0xCDEF89ABU

/* ── GPIO ──────────────────────────────────────────────────────────────── */

#define GPIOA_BASE         0x40010800U
#define GPIOB_BASE         0x40010C00U

typedef struct {
    volatile uint32_t CRL;    /**< 0x00: Port config low (pins 0-7) */
    volatile uint32_t CRH;    /**< 0x04: Port config high (pins 8-15) */
    volatile uint32_t IDR;    /**< 0x08: Input data register */
    volatile uint32_t ODR;    /**< 0x0C: Output data register */
    volatile uint32_t BSRR;   /**< 0x10: Bit set/reset register */
    volatile uint32_t BRR;    /**< 0x14: Bit reset register */
    volatile uint32_t LCKR;   /**< 0x18: Lock register */
} GPIO_TypeDef;

#define GPIOA   ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB   ((GPIO_TypeDef *)GPIOB_BASE)

/* GPIO mode/cnf values (4 bits per pin in CRL/CRH) */
#define GPIO_MODE_INPUT          0x0U
#define GPIO_MODE_OUTPUT_10MHZ   0x1U
#define GPIO_MODE_OUTPUT_2MHZ    0x2U
#define GPIO_MODE_OUTPUT_50MHZ   0x3U

#define GPIO_CNF_IN_ANALOG       (0x0U << 2)
#define GPIO_CNF_IN_FLOATING     (0x1U << 2)
#define GPIO_CNF_IN_PUPD         (0x2U << 2)
#define GPIO_CNF_OUT_PP          (0x0U << 2)
#define GPIO_CNF_OUT_OD          (0x1U << 2)
#define GPIO_CNF_AF_PP           (0x2U << 2)
#define GPIO_CNF_AF_OD           (0x3U << 2)

/* ── AFIO (Alternate Function I/O) ─────────────────────────────────────── */

#define AFIO_BASE          0x40010000U
#define AFIO_MAPR         (*(volatile uint32_t *)(AFIO_BASE + 0x04U))

/* USART2 remap: USART2_REMAP bit */
#define AFIO_MAPR_USART2_REMAP  (1U << 3)

/* ── USART ─────────────────────────────────────────────────────────────── */

#define USART1_BASE        0x40013800U
#define USART2_BASE        0x40004400U

typedef struct {
    volatile uint32_t SR;     /**< Status register */
    volatile uint32_t DR;     /**< Data register */
    volatile uint32_t BRR;    /**< Baud rate register */
    volatile uint32_t CR1;    /**< Control register 1 */
    volatile uint32_t CR2;    /**< Control register 2 */
    volatile uint32_t CR3;    /**< Control register 3 */
    volatile uint32_t GTPR;   /**< Guard time / prescaler */
} USART_TypeDef;

#define USART1  ((USART_TypeDef *)USART1_BASE)
#define USART2  ((USART_TypeDef *)USART2_BASE)

/* USART_SR bits */
#define USART_SR_RXNE    (1U << 5)   /**< Read data register not empty */
#define USART_SR_TC      (1U << 6)   /**< Transmission complete */
#define USART_SR_TXE     (1U << 7)   /**< Transmit data register empty */

/* USART_CR1 bits */
#define USART_CR1_RE     (1U << 2)   /**< Receiver enable */
#define USART_CR1_TE     (1U << 3)   /**< Transmitter enable */
#define USART_CR1_UE     (1U << 13)  /**< USART enable */

/* ── IWDG (Independent Watchdog) ───────────────────────────────────────── */

#define IWDG_BASE          0x40003000U
#define IWDG_KR           (*(volatile uint32_t *)(IWDG_BASE + 0x00U))
#define IWDG_PR           (*(volatile uint32_t *)(IWDG_BASE + 0x04U))
#define IWDG_RLR          (*(volatile uint32_t *)(IWDG_BASE + 0x08U))
#define IWDG_SR           (*(volatile uint32_t *)(IWDG_BASE + 0x0CU))

#define IWDG_KEY_RELOAD    0xAAAAU
#define IWDG_KEY_ENABLE    0xCCCCU
#define IWDG_KEY_ACCESS    0x5555U

/* ── System functions ──────────────────────────────────────────────────── */

/** Trigger system reset via SCB. */
static inline void NVIC_SystemReset(void)
{
    __asm volatile ("dsb 0xF" ::: "memory");
    SCB_AIRCR = (0x5FAUL << 16) | (1UL << 2);
    __asm volatile ("dsb 0xF" ::: "memory");
    for (;;) { __asm volatile ("nop"); }
}

#ifdef __cplusplus
}
#endif

#endif /* STM32F1XX_H */
