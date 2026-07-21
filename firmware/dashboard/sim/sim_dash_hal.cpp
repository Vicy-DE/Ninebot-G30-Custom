/**
 * @file sim_dash_hal.cpp
 * @brief Simulated implementation of the `dash::hal` interface, backed by the
 *        SimChip model. Linked into the chip simulator INSTEAD of dash_hal.cpp,
 *        so the unmodified `DashApp` firmware logic runs on the host.
 *
 * The IWDG path mirrors the real driver exactly: it uses the same
 * `ninebot::iwdg_params()` and reload semantics, so the simulated 5000 ms
 * timeout matches the hardware.
 */
#include "dash_hal.h"
#include "sim_chip.h"
#include "watchdog_supervisor.h"   // ninebot::iwdg_params / IWDG_TIMEOUT_MS

namespace dash {
namespace sim {

SimChip& chip() {
    static SimChip g_chip;
    return g_chip;
}

} // namespace sim

namespace hal {

void clock_init()   { /* SimChip clock is always ready */ }
void systick_init() { /* time comes from SimChip::time_ms */ }
uint32_t now_ms()   { return sim::chip().time_ms; }
void delay_ms(uint32_t ms) { sim::chip().advance(ms); }

void iwdg_init() {
    ninebot::IwdgParams p = ninebot::iwdg_params(ninebot::IWDG_TIMEOUT_MS);
    sim::chip().iwdg_program(p.pr, p.rlr);
}
void iwdg_feed() { sim::chip().iwdg_reload(); }

void gpio_init() {}
void led_write(uint8_t idx, bool on) { if (idx < 6) sim::chip().led[idx] = on; }
bool button_pressed() { return sim::chip().button; }

void usart1_init() {}
void usart1_send(uint8_t b) { sim::chip().u1_tx.push_back(b); }
bool usart1_recv(uint8_t* out) {
    auto& q = sim::chip().u1_rx;
    if (q.empty()) return false;
    *out = q.front(); q.pop_front();
    return true;
}

void usart2_init() {}
void usart2_send(uint8_t b) { sim::chip().u2_tx.push_back(b); }
bool usart2_recv(uint8_t* out) {
    auto& q = sim::chip().u2_rx;
    if (q.empty()) return false;
    *out = q.front(); q.pop_front();
    return true;
}

bool adc_init() { return sim::chip().adc_ok; }
bool adc_read(uint8_t channel, uint16_t* out) {
    if (!sim::chip().adc_ok) return false;          // missing ADC → no kick → reset
    *out = (channel < 16) ? sim::chip().adc_ch[channel] : 0;
    return true;
}

void daly_tx(const uint8_t* /*frame*/, size_t /*len*/) { /* captured elsewhere if needed */ }
void debug_puts(const char* /*s*/) { /* boot banner is irrelevant in sim */ }

} // namespace hal
} // namespace dash
