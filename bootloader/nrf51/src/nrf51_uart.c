/**
 * @file nrf51_uart.c
 * @brief nRF51822 UART0 driver for bootloader.
 *
 * Uses polling mode (no interrupts) for simplicity in the bootloader.
 * UART0 connects to the BLE STM32 via internal PCB traces.
 */

#include "nrf51_uart.h"
#include "nrf51.h"
#include "bootloader_config.h"

/* ── System tick (from bootloader_main.c) ──────────────────────────────── */

extern volatile uint32_t g_tick_ms;

/* ── Public API ────────────────────────────────────────────────────────── */

void nrf_uart_init(void)
{
    /* Configure TX pin as output */
    NRF_GPIO_PIN_CNF(UART_TX_PIN) =
        GPIO_PIN_CNF_DIR_OUTPUT |
        GPIO_PIN_CNF_INPUT_DISCONNECT |
        GPIO_PIN_CNF_PULL_DISABLED |
        GPIO_PIN_CNF_DRIVE_S0S1;

    /* Configure RX pin as input */
    NRF_GPIO_PIN_CNF(UART_RX_PIN) =
        GPIO_PIN_CNF_DIR_INPUT |
        GPIO_PIN_CNF_INPUT_CONNECT |
        GPIO_PIN_CNF_PULL_DISABLED;

    /* Select UART pins */
    NRF_UART0_PSELTXD = UART_TX_PIN;
    NRF_UART0_PSELRXD = UART_RX_PIN;

    /* Set baud rate */
    NRF_UART0_BAUDRATE = UART_BAUDRATE_115200;

    /* No parity, no flow control */
    NRF_UART0_CONFIG = 0;

    /* Enable UART */
    NRF_UART0_ENABLE = UART_ENABLE_ENABLED;

    /* Clear events */
    NRF_UART0_EVENTS_RXDRDY = 0;
    NRF_UART0_EVENTS_TXDRDY = 0;

    /* Start RX */
    NRF_UART0_TASKS_STARTRX = 1;
}

void nrf_uart_send_byte(uint8_t byte)
{
    NRF_UART0_EVENTS_TXDRDY = 0;
    NRF_UART0_TASKS_STARTTX = 1;
    NRF_UART0_TXD = byte;

    /* Wait for transmit complete */
    while (NRF_UART0_EVENTS_TXDRDY == 0) {
        /* Spin */
    }
    NRF_UART0_EVENTS_TXDRDY = 0;
}

void nrf_uart_send(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        nrf_uart_send_byte(data[i]);
    }
}

int nrf_uart_recv_byte(uint8_t *byte, uint32_t timeout_ms)
{
    uint32_t start = g_tick_ms;

    while (NRF_UART0_EVENTS_RXDRDY == 0) {
        if (timeout_ms > 0) {
            uint32_t elapsed = g_tick_ms - start;
            if (elapsed >= timeout_ms) {
                return -1;
            }
        }
    }

    NRF_UART0_EVENTS_RXDRDY = 0;
    *byte = (uint8_t)(NRF_UART0_RXD & 0xFF);
    return 0;
}

void nrf_uart_puts(const char *str)
{
    while (*str) {
        nrf_uart_send_byte((uint8_t)*str++);
    }
}
