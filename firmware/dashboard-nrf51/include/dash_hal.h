/**
 * @file dash_hal.h
 * @brief Hardware abstraction for the G30 dashboard (nRF51822).
 *
 * Everything the dashboard logic needs from the chip sits behind this interface so the same C++
 * builds for the nRF51 target *and* natively on the host for tests (see `sim/`).
 *
 * Pin numbers are the ones recovered from the stock firmware —
 * see firmware/decompiled/nrf51822/RE_NRF51_DASHBOARD.md.
 */
#ifndef DASH_HAL_H
#define DASH_HAL_H

#include <stdint.h>
#include <stddef.h>

namespace dash {

/** @brief nRF51 GPIO pin assignments recovered from the stock image. */
enum Pin : uint8_t {
    PIN_TM1637_DIO = 4,   /**< P0.04 — TM1637 data  (firmware-confirmed) */
    PIN_TM1637_CLK = 5,   /**< P0.05 — TM1637 clock (firmware-confirmed) */
    PIN_BUS_A      = 15,  /**< P0.15 — Ninebot bus line A (TX or RX, swaps) */
    PIN_BUS_B      = 20,  /**< P0.20 — Ninebot bus line B (RX or TX, swaps) */
    PIN_VARIANT    = 25,  /**< P0.25 — board-variant output */
};

/** @brief Ninebot bus direction; the stock firmware swaps PSELTXD/PSELRXD to turn the line around. */
enum class BusDir : uint8_t {
    TxOnB = 0, /**< TX=P0.20, RX=P0.15 (stock "mode 1") */
    TxOnA = 1, /**< TX=P0.15, RX=P0.20 (stock "mode 0") */
};

/**
 * @brief Chip-level operations used by the dashboard logic.
 *
 * Implemented by Nrf51Hal on target and by SimHal in the host tests.
 */
class Hal {
public:
    virtual ~Hal() = default;

    /** @brief Configure a GPIO as output (true) or input (false). @sideeffects */
    virtual void pinDir(uint8_t pin, bool output) = 0;
    /** @brief Drive a GPIO high/low. @sideeffects */
    virtual void pinWrite(uint8_t pin, bool high) = 0;
    /** @brief Sample a GPIO. */
    virtual bool pinRead(uint8_t pin) = 0;
    /** @brief Busy-wait; the TM1637 needs a few microseconds per edge. */
    virtual void delayUs(uint32_t us) = 0;
    /** @brief Milliseconds since boot (monotonic). */
    virtual uint32_t millis() = 0;

    /** @brief Point UART0 at the given direction, 115200 8N1. @sideeffects */
    virtual void uartSetDir(BusDir dir) = 0;
    /** @brief Queue bytes for transmission on the bus. @sideeffects */
    virtual void uartWrite(const uint8_t* data, size_t len) = 0;
    /** @brief Pop one received byte; returns false when nothing is pending. */
    virtual bool uartRead(uint8_t& out) = 0;

    /** @brief Kick the watchdog. @sideeffects */
    virtual void feedWatchdog() = 0;
};

} // namespace dash

#endif // DASH_HAL_H
