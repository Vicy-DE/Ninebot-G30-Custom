/**
 * @file dashboard.h
 * @brief Dashboard application logic — bus <-> display <-> button, independent of the chip.
 *
 * Kept free of nRF51 headers so the whole state machine runs in host tests (`sim/`).
 * Behaviour mirrors the stock dashboard where it is known, and diverges only where the VESC
 * replaces the stock ESC (see Documentation/Requirements/dashboard-nrf51.md).
 */
#ifndef DASHBOARD_H
#define DASHBOARD_H

#include "dash_hal.h"
#include "nb_protocol.h"
#include "tm1637.h"

namespace dash {

/** @brief Ride mode, mirroring the stock `0x64` telemetry byte 0. */
enum class RideMode : uint8_t { Eco = 0x00, Drive = 0x01, Sport = 0x02 };

/** @brief What the button did, after debouncing. */
enum class ButtonEvent : uint8_t { None, ShortPress, DoublePress, LongPress };

/** @brief Live values shown on the display. */
struct Telemetry {
    uint16_t speed_kmh_x10 = 0;  /**< speed x10, so 253 == 25.3 km/h */
    uint8_t  battery_pct = 0;
    RideMode mode = RideMode::Drive;
    bool     light_on = false;
    uint8_t  error = 0;          /**< non-zero shows an error code instead of speed */
    bool     linked = false;     /**< true once the ESC/VESC has answered recently */
};

/** @brief Debounce + short/long press classification for the power button. */
class Button {
public:
    static constexpr uint32_t kDebounceMs = 30;
    static constexpr uint32_t kLongPressMs = 1500;
    static constexpr uint32_t kDoubleGapMs = 400;

    /**
     * @brief Feed the current raw level.
     * @param pressed true when the button is down (the line is **active-low** in hardware,
     *                so the caller inverts before calling)
     * @param now_ms  monotonic milliseconds
     */
    ButtonEvent update(bool pressed, uint32_t now_ms);

private:
    bool stable_ = false;
    bool raw_ = false;
    uint32_t changed_at_ = 0;
    uint32_t pressed_at_ = 0;
    uint32_t released_at_ = 0;
    bool long_fired_ = false;
    bool pending_short_ = false;
};

/** @brief The dashboard: owns the display, the bus codec and the button. */
class Dashboard {
public:
    static constexpr uint32_t kLinkTimeoutMs = 1000;  /**< no telemetry for this long => link lost */
    static constexpr uint32_t kPollIntervalMs = 50;   /**< how often we poll the ESC/VESC */

    Dashboard(Hal& hal, Tm1637& display) : hal_(hal), display_(display) {}

    /** @brief Bring up display + bus. @sideeffects */
    void begin();

    /** @brief Run one iteration: pump the bus, handle the button, refresh the display. @sideeffects */
    void poll();

    /** @brief Feed one received bus byte (called from the UART path). */
    void onBusByte(uint8_t b);

    /** @brief Handle a decoded frame. @sideeffects may transmit a reply */
    void onFrame(const Frame& f);

    /** @brief Render the current telemetry into the 6 grids. */
    void render(uint8_t seg[Tm1637::kGrids]) const;

    const Telemetry& telemetry() const { return tele_; }
    bool poweredOn() const { return powered_; }
    uint8_t brightness() const { return brightness_; }

    /** @brief Apply a button event (mode cycling, lights, power-off). @sideeffects */
    void applyButton(ButtonEvent ev);

private:
    void sendThrottle();
    void transmit(const Frame& f);

    Hal& hal_;
    Tm1637& display_;
    Decoder decoder_;
    Button button_;
    Telemetry tele_;
    bool powered_ = true;
    uint8_t brightness_ = 4;
    uint32_t last_rx_ms_ = 0;
    uint32_t last_poll_ms_ = 0;
};

} // namespace dash

#endif // DASHBOARD_H
