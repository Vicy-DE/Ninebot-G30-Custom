/**
 * @file selftest_main.c
 * @brief On-silicon self-test of the C542 software-UART + NBU cores.
 *
 * Runs the verified logic (soft_uart.c + nbu.c) on the actual STM32C542 and writes
 * a result to a fixed SRAM slot at 0x20000000 (section .result), which the host
 * reads back over SWD with STM32CubeProgrammer — no UART, jumper or scooter needed
 * to prove the cores execute correctly on the real Cortex-M33.
 *
 * Build: firmware/dash-tap-c542/c5board/Makefile (reuses the RC-Servo STM32CubeC5 SDK).
 */
#include <stdint.h>
#include "soft_uart.h"
#include "nbu.h"          /* nbu_checksum */

typedef struct {
    uint32_t magic;       /* 0x5A1F7E57 written LAST → present ⇒ rest valid */
    uint32_t checks;      /* tests run */
    uint32_t passed;      /* tests passed */
    uint32_t code;        /* 0xC542600D = all pass, 0xC542DEAD = fail */
} result_t;

volatile result_t g_result __attribute__((section(".result"), used));

/* soft_uart TX collects line levels into a small buffer for in-memory loopback. */
static int g_nbits;
static uint8_t g_bits[16];
static void emit(int lvl, void *ctx) { (void)ctx; if (g_nbits < 16) g_bits[g_nbits++] = (uint8_t)lvl; }

int main(void)
{
    uint32_t checks = 0, passed = 0;

    /* Test 1: software-UART byte loopback (TX -> 10 bits -> RX) over edge values. */
    const uint8_t vals[] = { 0x00, 0xFF, 0x5A, 0xA5, 0x55, 0xAA, 0x21, 0x3E };
    int lb_ok = 1;
    su_rx_t rx;
    for (unsigned i = 0; i < sizeof(vals); i++) {
        g_nbits = 0;
        su_tx_byte(vals[i], emit, 0);
        su_rx_reset(&rx);
        uint8_t out = 0; int got = 0;
        for (int b = 0; b < g_nbits; b++) { uint8_t o; if (su_rx_bit(&rx, g_bits[b], &o)) { out = o; got = 1; } }
        if (!got || out != vals[i] || g_nbits != 10) lb_ok = 0;
    }
    checks++; if (lb_ok) passed++;

    /* Test 2: NBU checksum on real silicon matches the (~sum) reference. */
    uint8_t body[] = { 0x04, 0x3F, 0x21, 0x07, 0x00, 0xC8, 0x00, 0x00, 0x00 };
    uint16_t s = 0; for (unsigned i = 0; i < sizeof(body); i++) s = (uint16_t)(s + body[i]);
    uint16_t expect = (uint16_t)~s;
    checks++; if (nbu_checksum(body, sizeof(body)) == expect) passed++;

    g_result.checks = checks;
    g_result.passed = passed;
    g_result.code   = (passed == checks) ? 0xC542600Du : 0xC542DEADu;
    g_result.magic  = 0x5A1F7E57u;       /* write magic LAST */

    for (;;) { __asm volatile("nop"); }
}
