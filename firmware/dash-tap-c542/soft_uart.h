/**
 * @file soft_uart.h
 * @brief Bit-banged ("software") UART — 8N1, LSB-first. Pure logic; the timing
 *        (bit period for 115200 at the C542 core clock, via a timer or DWT cycle
 *        counter) is the firmware's job and is documented in main.c.
 *
 * Used by the NUCLEO-C542RC programmer to talk the Ninebot/NBU protocol on a plain
 * GPIO (PA0), so no hardware-USART alternate function is required on the chosen pin.
 */
#ifndef SOFT_UART_H
#define SOFT_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** TX: emit the 10 line levels of one byte — start(0), d0..d7 (LSB first), stop(1). */
void su_tx_byte(uint8_t byte, void (*emit_bit)(int level, void *ctx), void *ctx);

/** RX bit-by-bit state machine (feed one sampled level per bit period). */
typedef struct {
    uint8_t state;   /* 0 = idle (await start), 1 = data, 2 = stop */
    uint8_t bit;     /* data bit index 0..7 */
    uint8_t shift;   /* byte being assembled */
} su_rx_t;

void su_rx_reset(su_rx_t *rx);

/** Feed one bit level. Returns 1 and sets *out when a byte completes (valid stop). */
int su_rx_bit(su_rx_t *rx, int level, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_UART_H */
