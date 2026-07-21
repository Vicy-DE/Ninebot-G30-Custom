/**
 * @file main_programmer.c
 * @brief NUCLEO-C542RC IAP programmer over a SOFTWARE (bit-banged) UART.
 *
 * The C542 drives the dashboard bus on **A0 = PA0** with a software UART (no
 * hardware-USART AF needed on the pin) and acts as the IAP programmer: it streams
 * a signed .sfw from the PC (over the ST-LINK VCP) and flashes it to the target
 * bootloader using the verified NBU protocol (nbu_prog.c), or receives a bootloader
 * dump and forwards it to the PC.
 *
 * Verified cores (host-tested, do not edit for the board):
 *   - soft_uart.c : bit framing (8N1, LSB-first)         [sim/test_wire_finder + test_c5_prog]
 *   - nbu_prog.c  : NBU BEGIN/DATA/END + ACK/retransmit   [sim/test_c5_prog, sim/test_iap_chain]
 * This file is the board glue (bit timing + GPIO + VCP) — STM32CubeIDE / STM32CubeC5.
 * Spots that depend on the STM32C542 are tagged  <<< CONFIRM IN CUBEMX/DATASHEET >>>.
 *
 * Before flashing real hardware, run:  python tools/verify_c5_flash.py   (must print GO).
 */
#include "soft_uart.h"
#include "nbu_prog.h"
#include <string.h>

#include "stm32c5xx_hal.h"          /* <<< CONFIRM IN CUBEMX >>> (generated) */

/* ── Bit timing ────────────────────────────────────────────────────────────
 * One bit period at 115200 baud. With the C542 core clock CORE_HZ, the per-bit
 * delay is CORE_HZ/115200 cycles. Use the DWT cycle counter for a tight loop.   */
#define BAUD        115200u
#define CORE_HZ     144000000u       /* <<< CONFIRM: SystemCoreClock after clock init >>> */
#define BIT_CYCLES  (CORE_HZ / BAUD)

/* DWT cycle counter (Cortex-M33). */
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004u)
#define DEMCR       (*(volatile uint32_t *)0xE000EDFCu)

static void dwt_init(void) { DEMCR |= (1u << 24); DWT_CYCCNT = 0; DWT_CTRL |= 1u; }
static void delay_cycles(uint32_t n) { uint32_t s = DWT_CYCCNT; while ((DWT_CYCCNT - s) < n) {} }
static void delay_bit(void)  { delay_cycles(BIT_CYCLES); }
static void delay_half(void) { delay_cycles(BIT_CYCLES / 2u); }

/* ── PA0 line (A0). Idle high; open-drain so it can share a half-duplex wire. ── */
#define LINE_PORT   GPIOA            /* <<< CONFIRM: A0 = PA0 (verified for NUCLEO-C542RC) >>> */
#define LINE_PIN    GPIO_PIN_0
static void line_high(void) { HAL_GPIO_WritePin(LINE_PORT, LINE_PIN, GPIO_PIN_SET); }
static void line_low(void)  { HAL_GPIO_WritePin(LINE_PORT, LINE_PIN, GPIO_PIN_RESET); }
static int  line_read(void) { return HAL_GPIO_ReadPin(LINE_PORT, LINE_PIN) == GPIO_PIN_SET; }

/* Software-UART TX: drive one bit level for one bit period (start,d0..d7,stop). */
static void su_emit(int level, void *ctx) {
    (void)ctx;
    if (level) line_high(); else line_low();
    delay_bit();
}
static void soft_tx_byte(uint8_t b) { su_tx_byte(b, su_emit, 0); }

/* Software-UART RX: wait for a start edge, sample 8 data bits at bit centre + stop. */
static int soft_rx_byte(uint8_t *out, uint32_t timeout_ms) {
    uint32_t spins = timeout_ms * (CORE_HZ / 1000u);
    uint32_t t0 = DWT_CYCCNT;
    while (line_read()) {                              /* idle high: await start (low) */
        if ((DWT_CYCCNT - t0) > spins) return -1;
    }
    delay_half();                                     /* move to centre of the start bit */
    su_rx_t rx; su_rx_reset(&rx);
    int got = 0; uint8_t b;
    /* feed start(0), 8 data, stop(1) — one sample per bit period at centre */
    got = su_rx_bit(&rx, 0, &b);
    for (int i = 0; i < 9; i++) { delay_bit(); if (su_rx_bit(&rx, line_read(), &b)) got = 1; }
    if (got) { *out = b; return 0; }
    return -1;
}

/* ── nbu_prog I/O over the software UART ───────────────────────────────────── */
static void io_send(uint8_t b, void *c)  { (void)c; soft_tx_byte(b); }
static int  io_recv(uint8_t *b, uint32_t t, void *c) { (void)c; return soft_rx_byte(b, t); }
static uint32_t io_tick(void *c) { (void)c; return HAL_GetTick(); }

/* ── VCP (PC link) ─────────────────────────────────────────────────────────── */
extern UART_HandleTypeDef hxUartVcp;     /* ST-LINK VCP  <<< CONFIRM IN CUBEMX >>> */
static void vcp_puts(const char *s) {
    HAL_UART_Transmit(&hxUartVcp, (uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}
static int vcp_get(uint8_t *b, uint32_t to) {
    return HAL_UART_Receive(&hxUartVcp, b, 1, to) == HAL_OK ? 0 : -1;
}

/* Image staging buffer (RAM). The C542 has ample SRAM for a 50 KB app image. */
static uint8_t g_image[52u * 1024u];

/* Read a length-prefixed image from the PC: 4-byte LE size, then that many bytes. */
static int vcp_load_image(uint32_t *len_out) {
    uint8_t n[4];
    for (int i = 0; i < 4; i++) if (vcp_get(&n[i], 5000) != 0) return -1;
    uint32_t len = (uint32_t)n[0] | (n[1] << 8) | (n[2] << 16) | (n[3] << 24);
    if (len > sizeof(g_image)) return -1;
    for (uint32_t i = 0; i < len; i++) if (vcp_get(&g_image[i], 5000) != 0) return -1;
    *len_out = len;
    return 0;
}

extern void SystemClock_Config(void);
extern void MX_GPIO_Init(void);          /* PA0 open-drain output + input capable */
extern void MX_VCP_UART_Init(void);

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_VCP_UART_Init();
    dwt_init();
    line_high();

    vcp_puts("\r\n[C542-PROG] software-UART IAP programmer ready (A0=PA0 @115200).\r\n");
    vcp_puts("[C542-PROG] send: u32 LE length + .sfw bytes; I flash it to the target BL.\r\n");

    const nbu_prog_io_t io = { io_send, io_recv, io_tick, 0 };
    const uint8_t TARGET_ADDR = 0x21;     /* BLE/dashboard bus address */

    for (;;) {
        uint32_t len = 0;
        if (vcp_load_image(&len) != 0) { vcp_puts("[C542-PROG] image load timeout/oversize.\r\n"); continue; }

        vcp_puts("[C542-PROG] flashing via NBU over software UART...\r\n");
        nbu_prog_result_t r = nbu_prog_send(&io, TARGET_ADDR, g_image, len);
        vcp_puts(r == NBU_PROG_OK ? "[C542-PROG] OK: target accepted the image.\r\n"
                                  : "[C542-PROG] FAIL: no/!ACK (check wiring, GND, target in update mode).\r\n");
    }
}
