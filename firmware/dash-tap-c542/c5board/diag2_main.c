/**
 * @file diag2_main.c
 * @brief Measure the real bit timing on the active wire PA4 (A2): records edge-to-edge
 *        intervals (core cycles) and the minimum (= 1 bit period). baud = SystemCoreClock
 *        / min_interval. Tells us the dashboard's actual baud instead of assuming 115200.
 *        Read back over SWD at 0x20000000.
 */
#include <stdint.h>
#include "stm32c5xx.h"

typedef struct {
    uint32_t magic;        /* 0xB10D0001 last */
    uint32_t sysclk;
    uint32_t edges;
    uint32_t min_cyc;      /* smallest edge interval -> 1 bit */
    uint16_t iv[60];       /* first 60 edge intervals (cycles, saturated) */
} t_t;
volatile t_t g __attribute__((section(".result"), used));

#define DEMCR    (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYC  (*(volatile uint32_t *)0xE0001004u)

int main(void)
{
    SystemCoreClockUpdate();
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    GPIOA->MODER &= ~(3u << (4 * 2));                    /* PA4 input */
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << (4 * 2))) | (1u << (4 * 2));
    DEMCR |= (1u << 24); DWT_CYC = 0; DWT_CTRL |= 1u;

    g.sysclk = SystemCoreClock;
    uint32_t window = SystemCoreClock * 2u;
    uint32_t start = DWT_CYC, last = start, edges = 0, mn = 0xFFFFFFFFu;
    int prev = (GPIOA->IDR >> 4) & 1u;

    while ((DWT_CYC - start) < window) {
        int c = (GPIOA->IDR >> 4) & 1u;
        if (c != prev) {
            uint32_t now = DWT_CYC, d = now - last; last = now; prev = c;
            if (d < mn) mn = d;
            if (edges < 60) g.iv[edges] = (d > 0xFFFFu) ? 0xFFFFu : (uint16_t)d;
            edges++;
        }
    }
    g.edges = edges;
    g.min_cyc = (mn == 0xFFFFFFFFu) ? 0 : mn;
    g.magic = 0xB10D0001u;
    for (;;) { __asm volatile("nop"); }
}
