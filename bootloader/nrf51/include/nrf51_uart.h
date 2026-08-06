/**
 * @file nrf51_uart.h
 * @brief nRF51822 UART driver for bootloader.
 */

#ifndef NRF51_UART_H
#define NRF51_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize UART0 at 115200 8N1 on the Ninebot bus (transmit on P0.15 by default). */
void nrf_uart_init(void);

/**
 * Point UART0 at a TX/RX pin pair — used to turn the one-wire half-duplex bus around.
 * Re-configuring is skipped when the direction is already correct.
 * @param tx_pin GPIO to drive (UART_PIN_BUS_A or UART_PIN_BUS_B)
 * @param rx_pin GPIO to listen on (the other one)
 */
void nrf_uart_set_dir(uint8_t tx_pin, uint8_t rx_pin);

/** Send a single byte (blocking). */
void nrf_uart_send_byte(uint8_t byte);

/** Send a buffer (blocking). */
void nrf_uart_send(const uint8_t *data, uint32_t len);

/**
 * Receive a byte with timeout.
 * @return 0 on success, -1 on timeout
 */
int nrf_uart_recv_byte(uint8_t *byte, uint32_t timeout_ms);

/** Send null-terminated string. */
void nrf_uart_puts(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* NRF51_UART_H */
