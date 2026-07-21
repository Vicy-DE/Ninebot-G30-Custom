/**
 * @file dash_app.h
 * @brief Dashboard application logic (init + one-step loop body), shared by the
 *        real target (`src/main.cpp`) and the chip simulator (`sim/`).
 *
 * It is HAL-agnostic: it calls the `dash::hal::*` free functions, which are
 * provided by `dash_hal.cpp` on the STM32 target and by `sim/sim_dash_hal.cpp`
 * in the simulator. Keeping the logic in one place means the simulator verifies
 * *exactly* the firmware that ships — including the 5000 ms watchdog discipline.
 */
#ifndef DASH_APP_H
#define DASH_APP_H

#include "dash_hal.h"
#include "watchdog_supervisor.h"
#include "ninebot_protocol_verified.hpp"
#include "dash_bridge.h"
#include "dash_keeper.h"
#include "mode_ctrl.h"
#include "daly_soft_uart.h"

namespace dash {

namespace nv = ninebot_verified;

/** The whole dashboard application as init()/step(), so any driver loop or the
 *  simulator can run it one iteration at a time. */
class DashApp {
public:
    /** Bring up peripherals + state. The IWDG is started LAST (Req 16). */
    void init() {
        hal::clock_init();
        hal::systick_init();
        hal::gpio_init();
        hal::usart1_init();
        hal::usart2_init();
        adcOk_ = hal::adc_init();
        hal::iwdg_init();                 // 5000 ms — last, so init can't be cut short

        sup_ = ninebot::WatchdogSupervisor(/*staleMs*/2000);
        sup_.require(ninebot::WatchdogSupervisor::LOOP);
        sup_.require(ninebot::WatchdogSupervisor::CLOCK);
        sup_.require(ninebot::WatchdogSupervisor::ADC);

        bridge_ = ninebot::ble::DashBridge();
        keeper_ = ninebot::ble::DashKeeper();
        runEffects(keeper_.begin());

        vescParser_.reset(); nrfParser_.reset();
        lastThrottleMs_ = 0; lastBtn_ = false; lastEdgeMs_ = 0; fed_ = false;

        sup_.kick(ninebot::WatchdogSupervisor::CLOCK, hal::now_ms());
        if (adcOk_) sup_.kick(ninebot::WatchdogSupervisor::ADC, hal::now_ms());
        hal::debug_puts("[BOOT] G30 dash wdt=5000ms\r\n");
    }

    /** One main-loop iteration. */
    void step() {
        fed_ = false;
        uint32_t now = hal::now_ms();
        sup_.kick(ninebot::WatchdogSupervisor::LOOP, now);
        sup_.kick(ninebot::WatchdogSupervisor::CLOCK, now);  // core clock runs while we loop

        uint16_t tRaw = 0, bRaw = 0;
        bool a0 = hal::adc_read(0, &tRaw);
        bool a1 = hal::adc_read(1, &bRaw);
        if (a0 && a1) sup_.kick(ninebot::WatchdogSupervisor::ADC, now);
        uint8_t throttle = scale(tRaw, THROTTLE_MIN, THROTTLE_MAX);
        uint8_t brake    = scale(bRaw, BRAKE_MIN, BRAKE_MAX);

        uint8_t b;
        while (hal::usart2_recv(&b)) {
            if (nv::receiveByte(vescParser_, b, vescPkt_) == nv::RxResult::PacketReady) {
                sup_.kick(ninebot::WatchdogSupervisor::UART_VESC, now);
                runEffects(keeper_.onVescLinkUp());
                if (vescPkt_.cmd == nv::CMD_HEAD_IO) bridge_.parseDisplayFrame(vescPkt_);
            }
        }
        if (now - lastThrottleMs_ >= 50) {
            lastThrottleMs_ = now;
            uint8_t frame[16];
            size_t n = bridge_.buildThrottleFrame(throttle, brake, frame);
            for (size_t i = 0; i < n; ++i) hal::usart2_send(frame[i]);
        }

        while (hal::usart1_recv(&b)) {
            sup_.kick(ninebot::WatchdogSupervisor::UART_NRF, now);
            if (nv::receiveByte(nrfParser_, b, nrfPkt_) == nv::RxResult::PacketReady) {
                uint8_t resp[64];
                size_t rn = bridge_.onAppPacket(nrfPkt_, resp);
                for (size_t i = 0; i < rn; ++i) hal::usart1_send(resp[i]);
            }
        }

        handleButton(now);

        sup_.require(ninebot::WatchdogSupervisor::UART_VESC,
                     keeper_.state() == ninebot::ble::KeeperState::Run);
        runEffects(keeper_.onTick(now));
        updateLeds();

        if (sup_.shouldFeed(now)) { hal::iwdg_feed(); fed_ = true; }
    }

    /* ── inspection hooks (used by the simulator/tests) ─────────────────── */
    bool fedLastStep() const { return fed_; }
    ninebot::ble::KeeperState keeperState() const { return keeper_.state(); }
    ninebot::ble::DashBridge& bridge() { return bridge_; }

private:
    static constexpr uint16_t THROTTLE_MIN = 400, THROTTLE_MAX = 3400;
    static constexpr uint16_t BRAKE_MIN = 500,   BRAKE_MAX = 3200;

    static uint8_t scale(uint16_t adc, uint16_t lo, uint16_t hi) {
        if (adc <= lo) return 0;
        if (adc >= hi) return 255;
        return uint8_t(uint32_t(adc - lo) * 255u / (hi - lo));
    }

    void updateLeds() {
        hal::led_write(0, true);
        uint8_t mode = uint8_t(bridge_.getReg(0x75) & 0x0F);
        hal::led_write(2, mode == 1);
        hal::led_write(3, mode == 0);
        hal::led_write(4, mode == 2);
        hal::led_write(5, bridge_.getReg(0x1B) != 0);
    }

    void runEffects(uint32_t eff) {
        using namespace ninebot::ble;
        if (eff & EFF_NRF_NORMAL)   hal::usart1_send(ninebot::nrf51::CMD_ENTER_NORMAL);
        if (eff & EFF_NRF_HAYSTACK) hal::usart1_send(ninebot::nrf51::CMD_ENTER_HAYSTACK);
        if (eff & (EFF_DALY_ON | EFF_DALY_OFF)) {
            uint8_t f[ninebot::daly::FRAME_LEN];
            ninebot::daly::buildMosControl(ninebot::daly::CMD_DISCHARGE_MOS,
                                           (eff & EFF_DALY_ON) != 0, f);
            hal::daly_tx(f, sizeof f);
        }
    }

    void handleButton(uint32_t now) {
        bool pressed = hal::button_pressed();
        if (pressed != lastBtn_ && (now - lastEdgeMs_) > 30) {
            lastEdgeMs_ = now;
            lastBtn_ = pressed;
            runEffects(pressed ? keeper_.onButtonPress(now)
                               : keeper_.onButtonRelease(now));
        }
    }

    ninebot::WatchdogSupervisor sup_{2000};
    ninebot::ble::DashBridge    bridge_;
    ninebot::ble::DashKeeper    keeper_;
    nv::Parser vescParser_{}, nrfParser_{};
    nv::Packet vescPkt_{}, nrfPkt_{};
    uint32_t lastThrottleMs_ = 0;
    uint32_t lastEdgeMs_ = 0;
    bool     lastBtn_ = false;
    bool     adcOk_ = false;
    bool     fed_ = false;
};

} // namespace dash

#endif // DASH_APP_H
