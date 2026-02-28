/**
 * @file stm32_uart.h
 * @brief STM32F103 UART driver for bootloader.
 */

#ifndef STM32_UART_H
#define STM32_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the update UART peripheral.
 * Configures GPIO pins, baud rate (115200), and enables TX/RX.
 */
void uart_init(void);

/**
 * Send a single byte (blocking).
 * @param byte  Byte to transmit
 */
void uart_send_byte(uint8_t byte);

/**
 * Send a buffer of bytes (blocking).
 * @param data  Data to send
 * @param len   Number of bytes
 */
void uart_send(const uint8_t *data, uint32_t len);

/**
 * Receive a single byte with timeout (blocking).
 * @param byte     Pointer to store received byte
 * @param timeout  Timeout in milliseconds (0 = infinite)
 * @return 0 on success, -1 on timeout
 */
int uart_recv_byte(uint8_t *byte, uint32_t timeout);

/**
 * Send a null-terminated string over UART.
 * Useful for bootloader status messages.
 * @param str  String to send
 */
void uart_puts(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* STM32_UART_H */
