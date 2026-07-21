/**
 * @file sniff_main.c
 * @brief Bit-banged software-UART sniffer on the real C542: listens on one tapped
 *        wire (PA0=A0 default, or -DSNIFF_PIN=4 for PA4=A2) at 115200 8N1, captures
 *        bus bytes into SRAM (.result @0x20000000), read back over SWD. Proves the
 *        wired link to the scooter and captures real Ninebot traffic.
 *
 * Timing self-calibrates from the live core clock (SystemCoreClockUpdate → 48 MHz),
 * so cycles-per-bit = SystemCoreClock/115200 (≈416). DWT cycle counter for delays.
 */
#include <stdint.h>
#include "stm32c5xx.h"

#ifndef SNIFF_PIN
#define SNIFF_PIN 0          /* PA0 = Arduino A0 (override: -DSNIFF_PIN=4 for PA4=A2) */
#endif

typedef struct {
    uint32_t magic;          /* 0xCAB70001 written last */
    uint32_t sysclk;         /* live core clock (Hz) */
    uint32_t count;          /* bytes captured */
    uint8_t  data[256];      /* captured bus bytes */
} cap_t;
volatile cap_t g __attribute__((section(".result"), used));

#define DEMCR     (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL  (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYC   (*(volatile uint32_t *)0xE0001004u)
static inline uint32_t cyc(void) { return DWT_CYC; }
static inline void wait_cyc(uint32_t n) { uint32_t s = cyc(); while ((cyc() - s) < n) {} }
static inline int line(void) { return (GPIOA->IDR >> SNIFF_PIN) & 1u; }

/* Receive one byte; return 0 ok / -1 if no start bit within timeout_cyc. */
static int rx_byte(uint8_t *out, uint32_t bitc, uint32_t timeout_cyc)
{
    uint32_t t0 = cyc();
    while (line()) { if ((cyc() - t0) > timeout_cyc) return -1; }   /* await start (low) */
    wait_cyc(bitc + bitc / 2u);                                     /* -> centre of bit0 */
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) { if (line()) b |= (uint8_t)(1u << i); wait_cyc(bitc); }
    *out = b;                                                       /* (stop bit ignored) */
    return 0;
}

int main(void)
{
    SystemCoreClockUpdate();
    uint32_t bitc = SystemCoreClock / 115200u;

    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;                            /* GPIOA clock */
    GPIOA->MODER &= ~(3u << (SNIFF_PIN * 2));                       /* input mode */
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << (SNIFF_PIN * 2)))
                 | (1u << (SNIFF_PIN * 2));                         /* pull-up (idle high) */
    DEMCR |= (1u << 24); DWT_CYC = 0; DWT_CTRL |= 1u;               /* DWT cycle counter */

    g.sysclk = SystemCoreClock;
    uint32_t count = 0;
    uint32_t start = cyc();
    uint32_t window = SystemCoreClock * 3u;                         /* ~3 s capture window */
    while (count < sizeof(g.data) && (cyc() - start) < window) {
        uint8_t b;
        if (rx_byte(&b, bitc, bitc * 40u) == 0) g.data[count++] = b;
    }
    g.count = count;
    g.magic = 0xCAB70001u;
    for (;;) { __asm volatile("nop"); }
}
