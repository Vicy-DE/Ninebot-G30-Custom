/**
 * @file probe_clock.c
 * @brief Read the real STM32C542 core clock off the chip (SWD readback at 0x20000000),
 *        so the software-UART bit timing can be calibrated to it.
 */
#include <stdint.h>

extern uint32_t SystemCoreClock;
extern void SystemCoreClockUpdate(void);

typedef struct { uint32_t magic; uint32_t sysclk_hz; uint32_t cyc_per_bit; uint32_t rsvd; } probe_t;
volatile probe_t g __attribute__((section(".result"), used));

int main(void)
{
    SystemCoreClockUpdate();                 /* compute HCLK from live RCC registers */
    g.sysclk_hz    = SystemCoreClock;
    g.cyc_per_bit  = SystemCoreClock / 115200u;
    g.magic        = 0xC10C0001u;            /* written last */
    for (;;) { __asm volatile("nop"); }
}
