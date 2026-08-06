/**
 * @file nrf51_uart.c
 * @brief nRF51822 UART0 driver for bootloader.
 *
 * Uses polling mode (no interrupts) for simplicity in the bootloader.
 * UART0 drives the Ninebot bus itself (there is no STM32 on this board). The bus is one-wire
 * half-duplex, so the driver can swap TX/RX pin selection the same way the stock firmware does.
 */

#include "nrf51_uart.h"
#include "nrf51.h"
#include "bootloader_config.h"

/* ── System tick (from bootloader_main.c) ──────────────────────────────── */

extern volatile uint32_t g_tick_ms;

/* ── Public API ────────────────────────────────────────────────────────── */

/** Currently selected transmit pin, so nrf_uart_set_dir() can skip redundant reconfiguration. */
static uint8_t s_tx_pin = 0xFF;

void nrf_uart_set_dir(uint8_t tx_pin, uint8_t rx_pin)
{
    if (s_tx_pin == tx_pin) {
        return;                     /* already pointing this way */
    }

    /* Mirrors the stock uart_init() @0x0001FDB4: reconfigure the pins, then re-point the UART. */
    NRF_UART0_ENABLE = 0;

    NRF_GPIO_PIN_CNF(tx_pin) =
        GPIO_PIN_CNF_DIR_OUTPUT |
        GPIO_PIN_CNF_INPUT_DISCONNECT |
        GPIO_PIN_CNF_PULL_DISABLED |
        GPIO_PIN_CNF_DRIVE_S0S1;

    NRF_GPIO_PIN_CNF(rx_pin) =
        GPIO_PIN_CNF_DIR_INPUT |
        GPIO_PIN_CNF_INPUT_CONNECT |
        GPIO_PIN_CNF_PULL_DISABLED;

    NRF_UART0_PSELTXD = tx_pin;
    NRF_UART0_PSELRXD = rx_pin;
    NRF_UART0_BAUDRATE = UART_BAUDRATE_115200;
    NRF_UART0_CONFIG = 0;                       /* 8N1, no flow control */
    NRF_UART0_ENABLE = UART_ENABLE_ENABLED;

    NRF_UART0_EVENTS_RXDRDY = 0;
    NRF_UART0_EVENTS_TXDRDY = 0;
    NRF_UART0_TASKS_STARTRX = 1;

    s_tx_pin = tx_pin;
}

void nrf_uart_init(void)
{
    s_tx_pin = 0xFF;                            /* force a full configure */
    nrf_uart_set_dir(UART_PIN_BUS_A, UART_PIN_BUS_B);
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
