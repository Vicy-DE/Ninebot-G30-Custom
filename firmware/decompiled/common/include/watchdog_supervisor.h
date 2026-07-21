/**
 * @file watchdog_supervisor.h
 * @brief Independent-watchdog (IWDG) supervisor for the dashboard firmware.
 *        Header-only, HAL-free, host-testable.
 *
 * **Hard requirement (Req 16):** the dashboard runs an STM32F103 IWDG with a
 * **5000 ms timeout**. The IWDG resets the MCU if it is not "kicked" in time.
 *
 * This supervisor decides *whether* to kick it: the firmware kicks the IWDG only
 * when **every required subsystem is fresh**. If a required subsystem is missing
 * (never initialised, or its last heartbeat is older than `staleMs`), the
 * supervisor withholds the kick → the IWDG fires → the MCU resets and re-inits.
 * That is how "reset if something is missing" is enforced.
 *
 * Timing: `staleMs` (default 2000 ms) must be **< 5000 ms** so a missing
 * subsystem is detected and the reset happens within one IWDG period.
 *
 * @note The LSI clock that drives the IWDG is ~40 kHz but spec'd 30–60 kHz, so
 * the real timeout spans ~3.7–6.7 s around the 5 s nominal. That tolerance is
 * fine for a recovery watchdog. See `iwdg_params()` and `tools/analysis/iwdg_config.py`.
 */
#ifndef NINEBOT_WATCHDOG_SUPERVISOR_H
#define NINEBOT_WATCHDOG_SUPERVISOR_H

#include <cstdint>

namespace ninebot {

/** Hard requirement: dashboard IWDG timeout (Req 16). */
static constexpr uint32_t IWDG_TIMEOUT_MS = 5000;

/** STM32F103 IWDG register parameters for a target timeout. */
struct IwdgParams {
    uint8_t  pr;         ///< IWDG_PR prescaler field (0..6 → /4../256)
    uint16_t rlr;        ///< IWDG_RLR reload value (0..4095)
    uint32_t actual_ms;  ///< realised timeout at the given LSI (nominal)
};

/**
 * Compute the smallest-prescaler IWDG params that realise @p timeout_ms.
 * Timeout = (4 << pr) * (rlr + 1) / lsi_hz. Single source of truth shared by the
 * firmware and `tools/analysis/iwdg_config.py`.
 *
 * @param timeout_ms desired timeout (e.g. IWDG_TIMEOUT_MS)
 * @param lsi_hz      LSI frequency (nominal 40000)
 */
inline IwdgParams iwdg_params(uint32_t timeout_ms, uint32_t lsi_hz = 40000) {
    for (uint8_t pr = 0; pr <= 6; ++pr) {
        uint32_t presc = 4u << pr;                       // /4,/8,...,/256
        uint64_t ticks = (uint64_t)timeout_ms * lsi_hz / 1000u / presc;
        if (ticks >= 1 && ticks <= 4096) {
            uint16_t rlr = (uint16_t)(ticks - 1);
            uint32_t actual = (uint32_t)((uint64_t)presc * (rlr + 1) * 1000u / lsi_hz);
            return { pr, rlr, actual };
        }
    }
    // Fall back to the maximum period (/256, reload 4095).
    return { 6, 4095, (uint32_t)((uint64_t)256 * 4096 * 1000u / lsi_hz) };
}

/**
 * Decides whether the IWDG should be kicked, based on subsystem freshness.
 *
 * Usage in the firmware main loop:
 * @code
 *   sup.kick(WatchdogSupervisor::LOOP, now);     // every iteration
 *   if (adc_ok)   sup.kick(WatchdogSupervisor::ADC, now);
 *   if (vesc_seen) sup.kick(WatchdogSupervisor::UART_VESC, now);
 *   if (sup.shouldFeed(now)) iwdg_feed();        // else: IWDG resets in ≤5 s
 * @endcode
 */
class WatchdogSupervisor {
public:
    /** Monitored subsystems (bit indices). */
    enum Subsys : uint8_t { LOOP, CLOCK, UART_VESC, UART_NRF, ADC, DALY, COUNT };

    explicit WatchdogSupervisor(uint32_t staleMs = 2000) : staleMs_(staleMs) {}

    /** Set the required-subsystem mask directly. */
    void setRequired(uint32_t mask) { required_ = mask; }

    /** Add/remove one subsystem from the required set. */
    void require(Subsys s, bool on = true) {
        if (on) required_ |= (1u << s); else required_ &= ~(1u << s);
    }

    /** Record that subsystem @p s was just seen healthy at @p nowMs. */
    void kick(Subsys s, uint32_t nowMs) {
        if (s < COUNT) { lastSeen_[s] = nowMs; seen_ |= (1u << s); }
    }

    /** True iff every required subsystem has been seen within `staleMs`. */
    bool shouldFeed(uint32_t nowMs) const {
        for (uint8_t s = 0; s < COUNT; ++s) {
            uint32_t bit = 1u << s;
            if (!(required_ & bit)) continue;            // not required → ignore
            if (!(seen_ & bit)) return false;            // required but never seen
            if ((uint32_t)(nowMs - lastSeen_[s]) > staleMs_) return false; // stale
        }
        return true;
    }

    uint32_t required() const { return required_; }
    uint32_t staleMs()  const { return staleMs_; }

private:
    uint32_t staleMs_;
    uint32_t required_ = 0;
    uint32_t seen_     = 0;
    uint32_t lastSeen_[COUNT] = {};
};

} // namespace ninebot

#endif // NINEBOT_WATCHDOG_SUPERVISOR_H
