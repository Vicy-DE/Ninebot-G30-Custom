/**
 * @file nrf51_hal.h
 * @brief Concrete dash::Hal for the nRF51822 (target build only).
 */
#ifndef NRF51_HAL_H
#define NRF51_HAL_H

#include "dash_hal.h"

namespace dash {

/** @brief Register-level HAL for the dashboard's nRF51822. */
class Nrf51Hal : public Hal {
public:
    /** @brief Start LFCLK + RTC1 (the millisecond timebase). @sideeffects */
    void begin();

    void pinDir(uint8_t pin, bool output) override;
    void pinWrite(uint8_t pin, bool high) override;
    bool pinRead(uint8_t pin) override;
    void delayUs(uint32_t us) override;
    uint32_t millis() override;
    void uartSetDir(BusDir dir) override;
    void uartWrite(const uint8_t* data, size_t len) override;
    bool uartRead(uint8_t& out) override;
    void feedWatchdog() override;

private:
    BusDir dir_ = BusDir::TxOnA;
    bool configured_ = false;
};

} // namespace dash

#endif // NRF51_HAL_H
