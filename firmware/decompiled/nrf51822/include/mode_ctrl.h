/**
 * @file mode_ctrl.h
 * @brief nRF51 NORMAL ⇄ HAYSTACK mode switch driven by STM32 UART commands
 *        (Req 15.3). Header-only, host-testable.
 *
 * The STM32 dashboard sends a single byte before sleeping / on wake:
 *   - `0xAA` → enter HAYSTACK (FindMy advertising only; NUS + bridge torn down)
 *   - `0xAB` → return to NORMAL (App NUS + VESC NUS + UART bridge active)
 * Any other byte leaves the mode unchanged (it is normal bridge traffic).
 */
#ifndef NINEBOT_MODE_CTRL_H
#define NINEBOT_MODE_CTRL_H

#include <cstdint>

namespace ninebot {
namespace nrf51 {

enum class Mode : uint8_t { Normal, Haystack };

static constexpr uint8_t CMD_ENTER_HAYSTACK = 0xAA;
static constexpr uint8_t CMD_ENTER_NORMAL   = 0xAB;

/** Tiny mode FSM with an edge-change signal for the caller to (re)configure BLE. */
class ModeCtrl {
public:
    Mode mode() const { return mode_; }

    /**
     * Feed a byte received from the STM32 on UART.
     * @return true if the mode just changed (caller should reconfigure radio).
     */
    bool onUartByte(uint8_t b) {
        Mode next = mode_;
        if (b == CMD_ENTER_HAYSTACK) next = Mode::Haystack;
        else if (b == CMD_ENTER_NORMAL) next = Mode::Normal;
        bool changed = (next != mode_);
        mode_ = next;
        return changed;
    }

    bool isHaystack() const { return mode_ == Mode::Haystack; }
    bool isNormal()   const { return mode_ == Mode::Normal; }

private:
    Mode mode_ = Mode::Normal;
};

} // namespace nrf51
} // namespace ninebot

#endif // NINEBOT_MODE_CTRL_H
