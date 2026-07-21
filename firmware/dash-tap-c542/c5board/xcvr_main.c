/**
 * @file xcvr_main.c
 * @brief SWD-driven half-duplex transceiver on PA4 (the live scooter wire).
 *
 * The host drives it entirely over SWD (STM32CubeProgrammer write/read of the SRAM
 * mailbox at 0x20000000) — no UART/VCP needed. On trigger it: waits for a bus idle
 * gap, transmits tx[] on PA4 (open-drain, bit-banged 115200), reading the line back
 * on every bit (echo[]) to PROVE it drove the wire, then captures the response (rx[])
 * for a window. Lets us verify TX non-destructively and run the stock IAP sequence.
 *
 * PA4 = open-drain output + pull-up: write 0 drives low, write 1 releases (bus pull-up
 * or another device sets the level); IDR always reads the real line — proper for the
 * one-wire half-duplex Ninebot bus.
 */
#include <stdint.h>
#include "stm32c5xx.h"
#include "soft_uart.h"

#define TXCAP 192
#define RXCAP 320

typedef struct {
    uint32_t magic_in;     /* host writes 0x60001234 to trigger              */
    uint32_t tx_len;       /* bytes to transmit                              */
    uint32_t rx_win_ms;    /* response capture window                        */
    uint32_t magic_out;    /* firmware writes 0x600D0DEF when done           */
    uint32_t rx_len;       /* bytes captured                                 */
    uint32_t echo_bits;    /* number of valid echo bits (tx_len*10)          */
    uint32_t sysclk;
    uint32_t rsvd;
    uint8_t  tx[TXCAP];
    uint8_t  echo[TXCAP * 10 / 8 + 1];   /* read-back level per transmitted bit */
    uint8_t  rx[RXCAP];
} mb_t;
volatile mb_t mb __attribute__((section(".result"), used));

#define DEMCR    (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYC  (*(volatile uint32_t *)0xE0001004u)
static uint32_t g_bitc;
static inline uint32_t cyc(void) { return DWT_CYC; }
static inline void wait(uint32_t n) { uint32_t s = cyc(); while ((cyc() - s) < n) {} }
static inline void drive_low(void) { GPIOA->BSRR = (1u << (4 + 16)); }   /* reset PA4 */
static inline void release(void)   { GPIOA->BSRR = (1u << 4); }          /* set PA4 (open-drain -> hi-Z) */
static inline int  rdline(void)    { return (GPIOA->IDR >> 4) & 1u; }

static uint32_t g_ei;              /* echo bit index during TX */
static void emit(int level, void *ctx)
{
    (void)ctx;
    if (level) release(); else drive_low();
    wait(g_bitc / 2u);
    if (rdline() && g_ei < sizeof(mb.echo) * 8)               /* sample mid-bit */
        mb.echo[g_ei >> 3] |= (uint8_t)(1u << (g_ei & 7));
    g_ei++;
    wait(g_bitc - g_bitc / 2u);
}

static int rx_byte(uint8_t *out, uint32_t timeout_cyc)
{
    uint32_t t0 = cyc();
    while (rdline()) { if ((cyc() - t0) > timeout_cyc) return -1; }
    wait(g_bitc + g_bitc / 2u);
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) { if (rdline()) b |= (uint8_t)(1u << i); wait(g_bitc); }
    *out = b;
    return 0;
}

int main(void)
{
    SystemCoreClockUpdate();
    g_bitc = SystemCoreClock / 115200u;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    /* PA4 PUSH-PULL output (fast edges; the internal pull-up is too weak for the bus
     * capacitance, so open-drain release rises too slowly for 115200). We only drive
     * during a confirmed idle gap, so we don't fight the dashboard's open-drain TX. */
    release();
    GPIOA->OTYPER  &= ~(1u << 4);                                         /* push-pull (when driving) */
    GPIOA->OSPEEDR |= (3u << (4 * 2));                                    /* high speed */
    GPIOA->PUPDR    = (GPIOA->PUPDR & ~(3u << (4 * 2))) | (1u << (4 * 2)); /* pull-up */
    GPIOA->MODER   &= ~(3u << (4 * 2));                                   /* INPUT (RX default) */
    DEMCR |= (1u << 24); DWT_CYC = 0; DWT_CTRL |= 1u;
    mb.sysclk = SystemCoreClock;
    mb.magic_out = 0;

    for (;;) {
        if (mb.magic_in != 0x60001234u) continue;
        uint32_t tx_len = mb.tx_len; if (tx_len > TXCAP) tx_len = TXCAP;

        for (uint32_t i = 0; i < sizeof(mb.echo); i++) mb.echo[i] = 0;
        g_ei = 0;

        /* wait for an idle gap: line high continuously for ~2 ms */
        uint32_t need = SystemCoreClock / 500u, hs = cyc(), guard = cyc();
        while ((cyc() - hs) < need) {
            if (!rdline()) hs = cyc();
            if ((cyc() - guard) > SystemCoreClock * 2u) break;   /* 2 s safety */
        }

        GPIOA->MODER = (GPIOA->MODER & ~(3u << (4 * 2))) | (1u << (4 * 2));  /* OUTPUT for TX */
        for (uint32_t i = 0; i < tx_len; i++) su_tx_byte(mb.tx[i], emit, 0);
        release();
        GPIOA->MODER &= ~(3u << (4 * 2));                                    /* back to INPUT for RX */
        mb.echo_bits = g_ei;

        /* capture response */
        uint32_t n = 0, start = cyc(), win = (SystemCoreClock / 1000u) * mb.rx_win_ms;
        while (n < RXCAP && (cyc() - start) < win) {
            uint8_t b;
            if (rx_byte(&b, g_bitc * 200u) == 0) mb.rx[n++] = b;
        }
        mb.rx_len = n;
        mb.magic_out = 0x600D0DEFu;
        mb.magic_in = 0;
    }
}
