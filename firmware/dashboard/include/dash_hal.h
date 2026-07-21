/**
 * @file dash_hal.h
 * @brief Bare-metal driver layer for the STM32F103C8 dashboard firmware.
 *
 * Pins (per docs/DASHBOARD_FIRMWARE.md §2 + boards/ble-dashboard/PINOUT.md):
 *   USART1 PA9/PA10  — nRF51 BLE link (full-duplex, app register protocol)
 *   USART2 PA2       — ESC/VESC cable DATA (single-wire half-duplex, Ninebot)
 *   ADC1  PA0/PA1    — throttle / brake
 *   GPIOB PB0..PB5   — dashboard LEDs;  PB12 — power/mode button (active-low)
 *   IWDG             — 5000 ms hard watchdog (Req 16)
 */
#ifndef DASH_HAL_H
#define DASH_HAL_H

#include <cstdint>
#include <cstddef>

namespace dash {
namespace hal {

/* clock / time */
void     clock_init();      ///< HSE 8 MHz → PLL×9 → 72 MHz, ADC clock, LSI on
void     systick_init();    ///< 1 ms tick
uint32_t now_ms();
void     delay_ms(uint32_t ms);

/* watchdog (Req 16) */
void iwdg_init();           ///< program 5000 ms from iwdg_params() and start
void iwdg_feed();           ///< reload the IWDG counter

/* gpio */
void gpio_init();
void led_write(uint8_t idx, bool on);   ///< idx 0..5 → PB0..PB5
bool button_pressed();                  ///< PB12, active-low

/* usart1 — nRF (full duplex) */
void usart1_init();
void usart1_send(uint8_t b);
bool usart1_recv(uint8_t* out);         ///< non-blocking; false if no byte

/* usart2 — VESC/ESC (half duplex) */
void usart2_init();
void usart2_send(uint8_t b);
bool usart2_recv(uint8_t* out);

/* adc */
bool adc_init();                        ///< returns false if calibration stalls
bool adc_read(uint8_t channel, uint16_t* out); ///< false on conversion timeout

/* optional Daly soft-UART on cable w4 (bit-bang, target TODO) */
void daly_tx(const uint8_t* frame, size_t len);

/* debug (compiled out unless DASH_DEBUG) */
void debug_puts(const char* s);

} // namespace hal
} // namespace dash

#endif // DASH_HAL_H
