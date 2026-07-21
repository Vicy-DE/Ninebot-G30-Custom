/**
 * @file stm32f103.h
 * @brief Minimal, self-contained STM32F103C8 register definitions for the
 *        dashboard firmware (RCC, FLASH, GPIO, AFIO, USART, ADC, IWDG, SysTick,
 *        SCB). Not a full CMSIS — only what this firmware touches.
 */
#ifndef DASH_STM32F103_H
#define DASH_STM32F103_H

#include <cstdint>

/* ── Cortex-M3 system ──────────────────────────────────────────────────── */
#define SCB_VTOR      (*(volatile uint32_t *)0xE000ED08U)
#define SCB_AIRCR     (*(volatile uint32_t *)0xE000ED0CU)
#define SYSTICK_CTRL  (*(volatile uint32_t *)0xE000E010U)
#define SYSTICK_LOAD  (*(volatile uint32_t *)0xE000E014U)
#define SYSTICK_VAL   (*(volatile uint32_t *)0xE000E018U)

/* ── RCC ───────────────────────────────────────────────────────────────── */
#define RCC_BASE      0x40021000U
#define RCC_CR        (*(volatile uint32_t *)(RCC_BASE + 0x00U))
#define RCC_CFGR      (*(volatile uint32_t *)(RCC_BASE + 0x04U))
#define RCC_APB2ENR   (*(volatile uint32_t *)(RCC_BASE + 0x18U))
#define RCC_APB1ENR   (*(volatile uint32_t *)(RCC_BASE + 0x1CU))
#define RCC_CSR       (*(volatile uint32_t *)(RCC_BASE + 0x24U))

#define RCC_CR_HSEON   (1U << 16)
#define RCC_CR_HSERDY  (1U << 17)
#define RCC_CR_PLLON   (1U << 24)
#define RCC_CR_PLLRDY  (1U << 25)

#define RCC_CFGR_SW_PLL     (0x2U << 0)
#define RCC_CFGR_SWS_PLL    (0x2U << 2)
#define RCC_CFGR_PLLSRC     (1U << 16)
#define RCC_CFGR_PLLMUL9    (0x7U << 18)
#define RCC_CFGR_PPRE1_DIV2 (0x4U << 8)
#define RCC_CFGR_ADCPRE_DIV6 (0x2U << 14)   /* 72/6 = 12 MHz ADC clock (≤14) */

#define RCC_APB2ENR_AFIOEN   (1U << 0)
#define RCC_APB2ENR_IOPAEN   (1U << 2)
#define RCC_APB2ENR_IOPBEN   (1U << 3)
#define RCC_APB2ENR_ADC1EN   (1U << 9)
#define RCC_APB2ENR_USART1EN (1U << 14)
#define RCC_APB1ENR_USART2EN (1U << 17)

#define RCC_CSR_LSION   (1U << 0)
#define RCC_CSR_LSIRDY  (1U << 1)

/* ── FLASH (latency only) ──────────────────────────────────────────────── */
#define FLASH_ACR     (*(volatile uint32_t *)0x40022000U)
#define FLASH_ACR_LATENCY_2 (2U << 0)
#define FLASH_ACR_PRFTBE    (1U << 4)

/* ── GPIO ──────────────────────────────────────────────────────────────── */
typedef struct {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO_TypeDef;
#define GPIOA ((GPIO_TypeDef *)0x40010800U)
#define GPIOB ((GPIO_TypeDef *)0x40010C00U)

#define GPIO_MODE_INPUT        0x0U
#define GPIO_MODE_OUTPUT_2MHZ  0x2U
#define GPIO_MODE_OUTPUT_50MHZ 0x3U
#define GPIO_CNF_IN_ANALOG     (0x0U << 2)
#define GPIO_CNF_IN_FLOATING   (0x1U << 2)
#define GPIO_CNF_IN_PUPD       (0x2U << 2)
#define GPIO_CNF_OUT_PP        (0x0U << 2)
#define GPIO_CNF_AF_PP         (0x2U << 2)

/* ── AFIO ──────────────────────────────────────────────────────────────── */
#define AFIO_MAPR (*(volatile uint32_t *)(0x40010000U + 0x04U))

/* ── USART ─────────────────────────────────────────────────────────────── */
typedef struct {
    volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} USART_TypeDef;
#define USART1 ((USART_TypeDef *)0x40013800U)
#define USART2 ((USART_TypeDef *)0x40004400U)

#define USART_SR_RXNE  (1U << 5)
#define USART_SR_TC    (1U << 6)
#define USART_SR_TXE   (1U << 7)
#define USART_CR1_RE   (1U << 2)
#define USART_CR1_TE   (1U << 3)
#define USART_CR1_UE   (1U << 13)
#define USART_CR3_HDSEL (1U << 3)   /* half-duplex single-wire */

/* 115200 8N1 BRR words (firmware-observed): USART1 @72 MHz, USART2 @36 MHz */
#define USART1_BRR_115200 0x0271U
#define USART2_BRR_115200 0x0139U

/* ── ADC1 ──────────────────────────────────────────────────────────────── */
typedef struct {
    volatile uint32_t SR, CR1, CR2, SMPR1, SMPR2, JOFR1, JOFR2, JOFR3, JOFR4,
                      HTR, LTR, SQR1, SQR2, SQR3, JSQR, JDR1, JDR2, JDR3, JDR4, DR;
} ADC_TypeDef;
#define ADC1 ((ADC_TypeDef *)0x40012400U)
#define ADC_SR_EOC     (1U << 1)
#define ADC_CR2_ADON   (1U << 0)
#define ADC_CR2_CAL    (1U << 2)
#define ADC_CR2_RSTCAL (1U << 3)
#define ADC_CR2_EXTSEL_SWSTART (0x7U << 17)
#define ADC_CR2_EXTTRIG (1U << 20)
#define ADC_CR2_SWSTART (1U << 22)

/* ── IWDG (independent watchdog) ───────────────────────────────────────── */
#define IWDG_KR  (*(volatile uint32_t *)(0x40003000U + 0x00U))
#define IWDG_PR  (*(volatile uint32_t *)(0x40003000U + 0x04U))
#define IWDG_RLR (*(volatile uint32_t *)(0x40003000U + 0x08U))
#define IWDG_SR  (*(volatile uint32_t *)(0x40003000U + 0x0CU))
#define IWDG_KEY_RELOAD 0xAAAAU
#define IWDG_KEY_ENABLE 0xCCCCU
#define IWDG_KEY_ACCESS 0x5555U

/* ── System reset ──────────────────────────────────────────────────────── */
static inline void NVIC_SystemReset(void) {
    __asm volatile ("dsb 0xF" ::: "memory");
    SCB_AIRCR = (0x5FAUL << 16) | (1UL << 2);
    __asm volatile ("dsb 0xF" ::: "memory");
    for (;;) { __asm volatile ("nop"); }
}

#endif /* DASH_STM32F103_H */
