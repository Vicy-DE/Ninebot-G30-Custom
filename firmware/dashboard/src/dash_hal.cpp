/**
 * @file dash_hal.cpp
 * @brief STM32F103C8 driver implementation for the dashboard firmware.
 */
#include "dash_hal.h"
#include "stm32f103.h"
#include "watchdog_supervisor.h"   // ninebot::iwdg_params(), IWDG_TIMEOUT_MS

namespace {
volatile uint32_t g_tick_ms = 0;

/** Set the 4-bit CRL/CRH config nibble for one pin. @sideeffects writes CRx. */
void gpio_cfg(GPIO_TypeDef* p, uint8_t pin, uint8_t nibble) {
    volatile uint32_t* cr = (pin < 8) ? &p->CRL : &p->CRH;
    uint32_t shift = (pin & 7u) * 4u;
    uint32_t v = *cr;
    v &= ~(0xFu << shift);
    v |= (uint32_t(nibble) << shift);
    *cr = v;
}
} // namespace

extern "C" void SysTick_Handler(void) { g_tick_ms++; }

namespace dash {
namespace hal {

/* ── clock / time ──────────────────────────────────────────────────────── */
void clock_init() {
    RCC_CR |= RCC_CR_HSEON;
    while (!(RCC_CR & RCC_CR_HSERDY)) {}
    FLASH_ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC_CFGR = RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL9 | RCC_CFGR_PPRE1_DIV2 |
               RCC_CFGR_ADCPRE_DIV6;
    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY)) {}
    RCC_CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC_CFGR & 0x0Cu) != RCC_CFGR_SWS_PLL) {}
    /* Start LSI for the IWDG. */
    RCC_CSR |= RCC_CSR_LSION;
    while (!(RCC_CSR & RCC_CSR_LSIRDY)) {}
    /* Peripheral clocks */
    RCC_APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN |
                   RCC_APB2ENR_ADC1EN | RCC_APB2ENR_USART1EN;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;
}

void systick_init() {
    SYSTICK_LOAD = (72000000u / 1000u) - 1u;
    SYSTICK_VAL = 0;
    SYSTICK_CTRL = 0x07u;            // enable + tickint + core clock
}

uint32_t now_ms() { return g_tick_ms; }

void delay_ms(uint32_t ms) {
    uint32_t start = g_tick_ms;
    while ((g_tick_ms - start) < ms) {}
}

/* ── IWDG (5000 ms hard requirement, Req 16) ───────────────────────────── */
void iwdg_init() {
    ninebot::IwdgParams p = ninebot::iwdg_params(ninebot::IWDG_TIMEOUT_MS);
    IWDG_KR  = IWDG_KEY_ENABLE;      // start the IWDG
    IWDG_KR  = IWDG_KEY_ACCESS;      // enable PR/RLR write
    IWDG_PR  = p.pr;
    IWDG_RLR = p.rlr;
    while (IWDG_SR & 0x3u) {}        // wait for PVU/RVU to clear
    IWDG_KR  = IWDG_KEY_RELOAD;      // load the counter
}

void iwdg_feed() { IWDG_KR = IWDG_KEY_RELOAD; }

/* ── GPIO ──────────────────────────────────────────────────────────────── */
void gpio_init() {
    for (uint8_t i = 0; i < 6; ++i)  // PB0..PB5 LEDs: output PP 2 MHz
        gpio_cfg(GPIOB, i, GPIO_MODE_OUTPUT_2MHZ | GPIO_CNF_OUT_PP);
    gpio_cfg(GPIOB, 12, GPIO_MODE_INPUT | GPIO_CNF_IN_PUPD); // button
    GPIOB->BSRR = (1u << 12);        // pull-up
    gpio_cfg(GPIOA, 0, GPIO_MODE_INPUT | GPIO_CNF_IN_ANALOG); // throttle
    gpio_cfg(GPIOA, 1, GPIO_MODE_INPUT | GPIO_CNF_IN_ANALOG); // brake
    gpio_cfg(GPIOA, 9, GPIO_MODE_OUTPUT_50MHZ | GPIO_CNF_AF_PP);   // USART1 TX
    gpio_cfg(GPIOA, 10, GPIO_MODE_INPUT | GPIO_CNF_IN_FLOATING);   // USART1 RX
    gpio_cfg(GPIOA, 2, GPIO_MODE_OUTPUT_50MHZ | (0x3u << 2));      // USART2 AF-OD (half-duplex)
}

void led_write(uint8_t idx, bool on) {
    if (idx > 5) return;
    GPIOB->BSRR = on ? (1u << idx) : (1u << (idx + 16));
}

bool button_pressed() { return !(GPIOB->IDR & (1u << 12)); } // active-low

/* ── USART ─────────────────────────────────────────────────────────────── */
void usart1_init() {
    USART1->BRR = USART1_BRR_115200;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}
void usart1_send(uint8_t b) {
    while (!(USART1->SR & USART_SR_TXE)) {}
    USART1->DR = b;
}
bool usart1_recv(uint8_t* out) {
    if (!(USART1->SR & USART_SR_RXNE)) return false;
    *out = uint8_t(USART1->DR & 0xFFu);
    return true;
}

void usart2_init() {
    USART2->BRR = USART2_BRR_115200;
    USART2->CR3 = USART_CR3_HDSEL;   // single-wire half-duplex
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}
void usart2_send(uint8_t b) {
    while (!(USART2->SR & USART_SR_TXE)) {}
    USART2->DR = b;
}
bool usart2_recv(uint8_t* out) {
    if (!(USART2->SR & USART_SR_RXNE)) return false;
    *out = uint8_t(USART2->DR & 0xFFu);
    return true;
}

/* ── ADC1 (throttle PA0 / brake PA1) ───────────────────────────────────── */
bool adc_init() {
    ADC1->SMPR2 = (0x7u << 0) | (0x7u << 3);   // ch0/ch1: 239.5-cycle sample
    ADC1->CR2 = ADC_CR2_ADON;                   // power on
    for (volatile int i = 0; i < 10000; ++i) {} // tstab
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    uint32_t guard = 0;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) && ++guard < 1000000u) {}
    if (guard >= 1000000u) return false;
    ADC1->CR2 |= ADC_CR2_CAL;
    guard = 0;
    while ((ADC1->CR2 & ADC_CR2_CAL) && ++guard < 1000000u) {}
    return guard < 1000000u;
}

bool adc_read(uint8_t channel, uint16_t* out) {
    ADC1->SQR1 = 0;                          // 1 conversion
    ADC1->SQR3 = channel & 0x1Fu;
    ADC1->CR2 |= ADC_CR2_EXTSEL_SWSTART | ADC_CR2_EXTTRIG;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    uint32_t guard = 0;
    while (!(ADC1->SR & ADC_SR_EOC)) { if (++guard > 1000000u) return false; }
    *out = uint16_t(ADC1->DR & 0x0FFFu);
    return true;
}

/* ── Daly soft-UART (bit-bang TODO on cable w4) ────────────────────────── */
void daly_tx(const uint8_t* frame, size_t len) {
    // TODO(target): bit-bang 9600 8N1 on the w4 GPIO (104 µs/bit). The frame
    // bytes are produced by ninebot::daly::buildMosControl()/buildRead().
    (void)frame; (void)len;
}

/* ── Debug (compiled out unless DASH_DEBUG) ────────────────────────────── */
void debug_puts(const char* s) {
#ifdef DASH_DEBUG
    while (*s) { usart2_send(uint8_t(*s++)); }  // dev-only; off in production
#else
    (void)s;
#endif
}

} // namespace hal
} // namespace dash
