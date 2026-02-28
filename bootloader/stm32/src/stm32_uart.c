/**
 * @file stm32_uart.c
 * @brief STM32F103 UART driver for bootloader.
 *
 * Configures the update UART (USART2 for both BLE and BMS boards)
 * at 115200 baud. Provides blocking send/receive with timeout.
 */

#include "stm32_uart.h"
#include "stm32f1xx.h"
#include "bootloader_config.h"

/* ── System tick (set up by bootloader_main.c) ─────────────────────────── */

extern volatile uint32_t g_tick_ms;

/* ── GPIO configuration helper ─────────────────────────────────────────── */

static GPIO_TypeDef *get_port(char port)
{
    if (port == 'A') return GPIOA;
    if (port == 'B') return GPIOB;
    return GPIOA;
}

/**
 * Configure a single GPIO pin (4-bit mode/cnf in CRL or CRH).
 */
static void gpio_config_pin(char port, uint8_t pin, uint8_t mode_cnf)
{
    GPIO_TypeDef *gpio = get_port(port);
    volatile uint32_t *cr;
    uint32_t shift;

    if (pin < 8) {
        cr = &gpio->CRL;
        shift = pin * 4;
    } else {
        cr = &gpio->CRH;
        shift = (pin - 8) * 4;
    }

    uint32_t val = *cr;
    val &= ~(0xFU << shift);
    val |= ((uint32_t)mode_cnf << shift);
    *cr = val;
}

/* ── UART implementation ───────────────────────────────────────────────── */

/** The USART peripheral used for updates. */
static USART_TypeDef *const update_usart =
    (USART_TypeDef *)UPDATE_USART_BASE;

void uart_init(void)
{
    /* Enable GPIO and AFIO clocks */
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN |
                   RCC_APB2ENR_AFIOEN;

    /* Enable USART2 clock (APB1) */
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;

#if UPDATE_UART_REMAP
    /* Remap USART2 to PB6/PB7 (BLE board) */
    AFIO_MAPR |= AFIO_MAPR_USART2_REMAP;
#endif

    /* Configure TX pin: AF push-pull, 50 MHz */
    gpio_config_pin(UPDATE_UART_TX_PORT, UPDATE_UART_TX_PIN,
                    GPIO_MODE_OUTPUT_50MHZ | GPIO_CNF_AF_PP);

    /* Configure RX pin: input floating */
    gpio_config_pin(UPDATE_UART_RX_PORT, UPDATE_UART_RX_PIN,
                    GPIO_MODE_INPUT | GPIO_CNF_IN_FLOATING);

    /* Configure USART */
    update_usart->BRR = UART_BRR_VALUE;
    update_usart->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    update_usart->CR2 = 0;
    update_usart->CR3 = 0;
}

void uart_send_byte(uint8_t byte)
{
    while (!(update_usart->SR & USART_SR_TXE)) {
        /* Wait for TX buffer empty */
    }
    update_usart->DR = byte;
    while (!(update_usart->SR & USART_SR_TC)) {
        /* Wait for transmission complete */
    }
}

void uart_send(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        uart_send_byte(data[i]);
    }
}

int uart_recv_byte(uint8_t *byte, uint32_t timeout)
{
    uint32_t start = g_tick_ms;

    while (!(update_usart->SR & USART_SR_RXNE)) {
        if (timeout > 0) {
            uint32_t elapsed = g_tick_ms - start;
            if (elapsed >= timeout) {
                return -1;  /* Timeout */
            }
        }
    }

    *byte = (uint8_t)(update_usart->DR & 0xFF);
    return 0;
}

void uart_puts(const char *str)
{
    while (*str) {
        uart_send_byte((uint8_t)*str++);
    }
}
