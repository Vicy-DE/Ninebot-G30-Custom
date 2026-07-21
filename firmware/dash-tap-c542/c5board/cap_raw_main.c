/**
 * @file cap_raw_main.c
 * @brief Logic-analyzer capture of the active wire PA4 (A2). On the first edge
 *        (activity trigger), samples PA4 at 4x oversampling (bit/4 cycles) and packs
 *        4096 raw levels (1 bit/sample = 512 bytes) into SRAM (.result @0x20000010).
 *        The host reads it over SWD and decodes the UART offline (decode_raw.py),
 *        so no on-chip framing assumption can go wrong.
 */
#include <stdint.h>
#include "stm32c5xx.h"

#define OS        4u          /* oversample factor */
#define NSAMP     16384u      /* samples (=> 2048 packed bytes, ~35 ms / ~400 UART bytes) */

typedef struct {
    uint32_t magic;           /* 0x4A700001 last */
    uint32_t sysclk;
    uint32_t interval_cyc;    /* cycles between samples (bit/OS) */
    uint32_t os;              /* oversample factor */
    uint8_t  bits[NSAMP / 8]; /* packed levels, sample i -> bits[i/8] bit (i%8) */
} raw_t;
volatile raw_t g __attribute__((section(".result"), used));

#define DEMCR    (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYC  (*(volatile uint32_t *)0xE0001004u)
static inline int pa4(void) { return (GPIOA->IDR >> 4) & 1u; }

int main(void)
{
    SystemCoreClockUpdate();
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    GPIOA->MODER &= ~(3u << (4 * 2));
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << (4 * 2))) | (1u << (4 * 2));
    DEMCR |= (1u << 24); DWT_CYC = 0; DWT_CTRL |= 1u;

    uint32_t bitc = SystemCoreClock / 115200u;
    uint32_t iv = bitc / OS;
    g.sysclk = SystemCoreClock; g.interval_cyc = iv; g.os = OS;
    for (uint32_t i = 0; i < sizeof(g.bits); i++) g.bits[i] = 0;

    /* Trigger: wait for the first edge (activity), up to ~2 s. */
    int prev = pa4(); uint32_t t0 = DWT_CYC;
    while (pa4() == prev) { if ((DWT_CYC - t0) > SystemCoreClock * 2u) break; }

    /* Capture NSAMP levels at fixed spacing. */
    uint32_t base = DWT_CYC;
    for (uint32_t i = 0; i < NSAMP; i++) {
        uint32_t target = base + i * iv;
        while ((int32_t)(DWT_CYC - target) < 0) { }
        if (pa4()) g.bits[i >> 3] |= (uint8_t)(1u << (i & 7));
    }
    g.magic = 0x4A700001u;
    for (;;) { __asm volatile("nop"); }
}
