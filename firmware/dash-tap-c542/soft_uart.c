/**
 * @file soft_uart.c
 * @brief Bit-banged UART logic (see soft_uart.h).
 */
#include "soft_uart.h"

void su_tx_byte(uint8_t b, void (*emit)(int, void *), void *ctx)
{
    emit(0, ctx);                                   /* start bit */
    for (int i = 0; i < 8; i++) emit((b >> i) & 1, ctx);   /* d0..d7, LSB first */
    emit(1, ctx);                                   /* stop bit */
}

void su_rx_reset(su_rx_t *rx)
{
    rx->state = 0; rx->bit = 0; rx->shift = 0;
}

int su_rx_bit(su_rx_t *rx, int level, uint8_t *out)
{
    switch (rx->state) {
    case 0:                                          /* idle: await start (0) */
        if (level == 0) { rx->state = 1; rx->bit = 0; rx->shift = 0; }
        return 0;
    case 1:                                          /* data bits, LSB first */
        if (level & 1) rx->shift |= (uint8_t)(1u << rx->bit);
        if (++rx->bit == 8) rx->state = 2;
        return 0;
    default:                                         /* stop bit */
        rx->state = 0;
        if (level == 1) { *out = rx->shift; return 1; }   /* framing ok */
        return 0;                                    /* framing error: drop */
    }
}
