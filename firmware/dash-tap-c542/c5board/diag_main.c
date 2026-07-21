/**
 * @file diag_main.c
 * @brief Electrical-activity diagnostic on the two tapped wires (PA0=A0, PA4=A2).
 *        Samples both pins in a tight loop for ~2 s and records, per pin: number of
 *        level transitions (edges) and whether the line ever went low. Distinguishes
 *        "bus idle / wire not the data line" (no edges) from "traffic present but my
 *        UART framing is off" (many edges). Read back over SWD at 0x20000000.
 */
#include <stdint.h>
#include "stm32c5xx.h"

typedef struct {
    uint32_t magic;       /* 0xD1A60001 last */
    uint32_t sysclk;
    uint32_t samples;     /* total samples taken */
    uint32_t edges_a0;    /* PA0 transitions */
    uint32_t edges_a4;    /* PA4 transitions */
    uint32_t low_a0;      /* PA0 ever low? (1/0) */
    uint32_t low_a4;      /* PA4 ever low? */
    uint32_t rsvd;
} diag_t;
volatile diag_t g __attribute__((section(".result"), used));

#define DEMCR     (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL  (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYC   (*(volatile uint32_t *)0xE0001004u)

int main(void)
{
    SystemCoreClockUpdate();
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    /* PA0 + PA4 inputs, pull-up (idle high if floating; data pulls it). */
    GPIOA->MODER &= ~((3u << (0 * 2)) | (3u << (4 * 2)));
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3u << (0 * 2)) | (3u << (4 * 2))))
                 | (1u << (0 * 2)) | (1u << (4 * 2));
    DEMCR |= (1u << 24); DWT_CYC = 0; DWT_CTRL |= 1u;

    g.sysclk = SystemCoreClock;
    uint32_t window = SystemCoreClock * 2u;      /* ~2 s */
    uint32_t start = DWT_CYC, samples = 0, e0 = 0, e4 = 0, lo0 = 0, lo4 = 0;
    int p0 = (GPIOA->IDR >> 0) & 1u, p4 = (GPIOA->IDR >> 4) & 1u;

    while ((DWT_CYC - start) < window) {
        int c0 = (GPIOA->IDR >> 0) & 1u;
        int c4 = (GPIOA->IDR >> 4) & 1u;
        if (c0 != p0) { e0++; p0 = c0; }
        if (c4 != p4) { e4++; p4 = c4; }
        if (!c0) lo0 = 1;
        if (!c4) lo4 = 1;
        samples++;
    }
    g.samples = samples; g.edges_a0 = e0; g.edges_a4 = e4;
    g.low_a0 = lo0; g.low_a4 = lo4;
    g.magic = 0xD1A60001u;
    for (;;) { __asm volatile("nop"); }
}
