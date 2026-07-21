/**
 * @file dash_keeper.h
 * @brief Dashboard power-keeper state machine (power-latch "Solution D",
 *        `docs/POWER_LATCH_SCHEMATIC.md` / `DASHBOARD_FIRMWARE.md` §3).
 *
 * Pure logic (no HAL): given events (button edges, VESC link seen, tick), it
 * produces state transitions and a list of @ref KeeperEffect side-effect requests
 * the firmware layer carries out (drive Daly 0xD9 on/off via the soft-UART,
 * notify the nRF51 0xAA/0xAB, enter STOP, disable VESC output). Host-testable.
 *
 * @code
 *   DEEP_SLEEP ──button press──► WAKE ──VESC link up──► RUN
 *       ▲  ▲                      │ timeout                │ long-press(off)
 *       │  └──────────────────────┘                        │
 *       └───────────────────────────────────────────────  ┘
 * @endcode
 */
#ifndef NINEBOT_DASH_KEEPER_H
#define NINEBOT_DASH_KEEPER_H

#include <cstdint>

namespace ninebot {
namespace ble {

enum class KeeperState : uint8_t { DeepSleep, Wake, Run };

/** Side-effects the firmware must perform after a step() (bit flags). */
enum KeeperEffect : uint32_t {
    EFF_NONE          = 0,
    EFF_DALY_ON       = 1u << 0,  ///< soft-UART: Daly 0xD9 ON (powers VESC) + S1 wake
    EFF_DALY_OFF      = 1u << 1,  ///< soft-UART: Daly 0xD9 OFF (cuts VESC power)
    EFF_NRF_HAYSTACK  = 1u << 2,  ///< USART1: send 0xAA → nRF51 FindMy mode
    EFF_NRF_NORMAL    = 1u << 3,  ///< USART1: send 0xAB → nRF51 normal mode
    EFF_VESC_DISABLE  = 1u << 4,  ///< ask VESC to disable output before cut
    EFF_ENTER_STOP    = 1u << 5,  ///< put STM32 into STOP (EXTI armed on PB12)
    EFF_REINIT        = 1u << 6,  ///< re-init clocks/peripherals on wake
};

/** Tunables (ms). */
struct KeeperConfig {
    uint32_t wakeTimeoutMs  = 4000;   ///< give up if VESC link not seen
    uint32_t longPressMs    = 1500;   ///< long-press → power off
};

/**
 * Power-keeper FSM. Feed it events; read back the effect bitmask to act on.
 *
 * Time is supplied by the caller (ms tick) so it is deterministic in tests.
 */
class DashKeeper {
public:
    explicit DashKeeper(KeeperConfig cfg = {}) : cfg_(cfg) {}

    KeeperState state() const { return state_; }

    /** Cold boot: powered from the always-on rail, nothing else on. */
    uint32_t begin() { state_ = KeeperState::DeepSleep; return EFF_ENTER_STOP; }

    /** Button pressed (EXTI wake or in-run press). @return effect bitmask. */
    uint32_t onButtonPress(uint32_t nowMs) {
        if (state_ == KeeperState::DeepSleep) {
            state_ = KeeperState::Wake;
            wakeStartMs_ = nowMs;
            return EFF_REINIT | EFF_DALY_ON | EFF_NRF_NORMAL;
        }
        pressStartMs_ = nowMs;   // RUN: remember for long-press detection
        return EFF_NONE;
    }

    /** Button released in RUN; long hold → power off. */
    uint32_t onButtonRelease(uint32_t nowMs) {
        if (state_ == KeeperState::Run && pressStartMs_ &&
            (nowMs - pressStartMs_) >= cfg_.longPressMs) {
            return powerOff();
        }
        pressStartMs_ = 0;
        return EFF_NONE;
    }

    /** VESC Ninebot frames started arriving → fully running. */
    uint32_t onVescLinkUp() {
        if (state_ == KeeperState::Wake) { state_ = KeeperState::Run; }
        return EFF_NONE;
    }

    /** Periodic tick; enforces the wake timeout. */
    uint32_t onTick(uint32_t nowMs) {
        if (state_ == KeeperState::Wake &&
            (nowMs - wakeStartMs_) >= cfg_.wakeTimeoutMs) {
            return powerOff();   // VESC never came up — go back to sleep
        }
        return EFF_NONE;
    }

private:
    uint32_t powerOff() {
        state_ = KeeperState::DeepSleep;
        pressStartMs_ = 0;
        return EFF_VESC_DISABLE | EFF_DALY_OFF | EFF_NRF_HAYSTACK | EFF_ENTER_STOP;
    }

    KeeperConfig cfg_;
    KeeperState  state_ = KeeperState::DeepSleep;
    uint32_t     wakeStartMs_  = 0;
    uint32_t     pressStartMs_ = 0;
};

} // namespace ble
} // namespace ninebot

#endif // NINEBOT_DASH_KEEPER_H
