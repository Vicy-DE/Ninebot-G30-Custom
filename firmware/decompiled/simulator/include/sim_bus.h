/**
 * @file sim_bus.h
 * @brief Simulation bus connecting all three Ninebot G30 firmware instances.
 *
 * The SimulationBus creates and wires the complete scooter system:
 *
 *   Phone App ←BLE→ [nRF/BLE Dashboard] ←UART→ [ESC Controller] ←UART→ [BMS Battery]
 *
 * All inter-board communication flows through SimUart cross-links, allowing
 * the test suite to verify end-to-end protocol behavior in software.
 *
 * Usage:
 *   SimulationBus bus;
 *   bus.init();
 *   bus.tickMs(1000);    // Run 1 second of simulated time
 *   bus.injectAppPacket({...});  // Send a command as the phone app
 *   auto response = bus.captureAppResponse();  // Read the response
 */

#ifndef NINEBOT_SIM_BUS_H
#define NINEBOT_SIM_BUS_H

#include "sim_hal.h"
#include "esc_firmware.h"
#include "ble_firmware.h"
#include "bms_firmware.h"
#include "protocol.h"
#include <vector>
#include <memory>
#include <functional>

namespace ninebot {
namespace sim {

/**
 * @class SimulationBus
 * @brief Connects ESC, BLE, and BMS firmware instances with simulated wiring.
 *
 * Wiring diagram:
 *   BLE.uartEsc ←→ ESC.uartBle   (bidirectional cross-link)
 *   ESC.uartBms ←→ BMS.uartEsc   (bidirectional cross-link)
 *   BLE.uartNrf ← external injection (simulates nRF51822/phone app)
 *
 * Firmware instances are heap-allocated because they require HAL
 * references at construction time. The HAL structs are stored as
 * members so they outlive the firmware objects.
 */
class SimulationBus {
public:
    SimulationBus() = default;

    /**
     * @brief Initialize all three firmware instances and wire the bus.
     *
     * This performs the following:
     *   1. Cross-links BLE↔ESC and ESC↔BMS UARTs
     *   2. Sets default BQ76940 cell voltages (3.7V × 10 cells)
     *   3. Constructs firmware instances with HAL bindings
     *   4. Calls init() on each firmware instance
     */
    void init() {
        /* Wire the UARTs: BLE.uartEsc ↔ ESC.uartBle */
        bleHw_.uartEsc.wireTo(&escHw_.uartBle);
        escHw_.uartBle.wireTo(&bleHw_.uartEsc);

        /* Wire the UARTs: ESC.uartBms ↔ BMS.uartEsc */
        escHw_.uartBms.wireTo(&bmsHw_.uartEsc);
        bmsHw_.uartEsc.wireTo(&escHw_.uartBms);

        /* Set default cell voltages (37.0V total = 3700mV × 10) */
        for (int i = 0; i < 10; i++) {
            bmsHw_.i2cAfe.setCellVoltage(i, 3700);
        }

        /* Set default temperatures to 25°C (adcVal=2000 in new formula) */
        bmsHw_.i2cAfe.setTemperature(0, 250);
        bmsHw_.adc.setChannel(0, 2000);

        /* Build HAL structs (stored as members so refs remain valid) */
        escHal_ = escHw_.toHal();
        bleHal_ = bleHw_.toHal();
        bmsHal_ = bmsHw_.toHal();

        /* Construct firmware instances with HAL bindings */
        esc_ = std::make_unique<esc::EscFirmware>(escHal_);
        ble_ = std::make_unique<ble::BleFirmware>(bleHal_);
        bms_ = std::make_unique<bms::BmsFirmware>(bmsHal_);

        /* Initialize each firmware */
        esc_->init();
        ble_->init();
        bms_->init();

        initialized_ = true;
    }

    /**
     * @brief Advance the simulation by one millisecond.
     *
     * Ticks all three SysTick timers, runs each firmware's main loop
     * iteration, and processes all inter-board UART traffic.
     */
    void tick1ms() {
        escHw_.systick.tick();
        bleHw_.systick.tick();
        bmsHw_.systick.tick();

        /* Trigger SysTick handlers (increment firmware tick counters) */
        esc_->sysTickHandler();
        ble_->sysTickHandler();
        bms_->sysTickHandler();

        /* Feed received bytes to protocol parsers (simulates UART ISRs) */
        drainUartRx();

        /* Run one main-loop iteration for each board */
        esc_->mainLoopIteration();
        ble_->mainLoopIteration();
        bms_->mainLoopIteration();

        /* Drain TX queues (sends bytes to SimUart, triggers cross-links) */
        drainUartTx();

        totalTickMs_++;
    }

    /**
     * @brief Advance the simulation by N milliseconds.
     * @param ms Number of milliseconds to simulate
     */
    void tickMs(uint32_t ms) {
        for (uint32_t i = 0; i < ms; i++) {
            tick1ms();
        }
    }

    /**
     * @brief Inject a raw Ninebot protocol packet as if sent from the phone app.
     *
     * The packet is delivered to BLE.uartNrf (simulating the nRF51822 relay).
     * @param packet Complete raw packet bytes including 5A A5 header
     */
    void injectAppPacket(const std::vector<uint8_t>& packet) {
        for (uint8_t b : packet) {
            bleHw_.uartNrf.injectRxByte(b);
        }
    }

    /**
     * @brief Build and inject a Ninebot protocol packet from the app.
     * @param dst   Destination address (e.g., 0x20 for ESC)
     * @param cmd   Command byte (READ=0x01, WRITE=0x02)
     * @param arg   Register argument (offset | length<<4)
     * @param payload Payload data
     */
    void injectAppCommand(uint8_t dst, uint8_t cmd, uint8_t arg,
                          const std::vector<uint8_t>& payload = {}) {
        uint8_t buf[256];
        int len = buildPacket(0x3E, dst, cmd, arg,
                              payload.empty() ? nullptr : payload.data(),
                              static_cast<uint8_t>(payload.size()), buf);
        for (int i = 0; i < len; i++) {
            bleHw_.uartNrf.injectRxByte(buf[i]);
        }
    }

    /**
     * @brief Capture any data that BLE has sent back to the nRF (app response).
     * @return Raw bytes from BLE.uartNrf TX buffer
     */
    std::vector<uint8_t> captureAppResponse() {
        auto data = bleHw_.uartNrf.txData();
        bleHw_.uartNrf.clearTx();
        return data;
    }

    /**
     * @brief Set throttle ADC value on the BLE board.
     * @param adcValue Raw 12-bit ADC value (400-3400 typical range)
     */
    void setThrottle(uint16_t adcValue) {
        bleHw_.adc.setChannel(0, adcValue);
    }

    /**
     * @brief Set brake ADC value on the BLE board.
     * @param adcValue Raw 12-bit ADC value (500-3200 typical range)
     */
    void setBrake(uint16_t adcValue) {
        bleHw_.adc.setChannel(1, adcValue);
    }

    /**
     * @brief Press or release the BLE dashboard button.
     * @param pressed true = button pressed (PB12 low), false = released
     */
    void setButton(bool pressed) {
        /* Button on PB12, active low */
        bleHw_.gpioB.setInputPin(12, !pressed);
    }

    /**
     * @brief Set a BMS cell voltage for testing.
     * @param cell Cell index (0-9)
     * @param mv   Voltage in millivolts
     */
    void setCellVoltage(int cell, uint16_t mv) {
        bmsHw_.i2cAfe.setCellVoltage(cell, mv);
    }

    /**
     * @brief Simulate hall sensor inputs on the ESC.
     * @param hallState 3-bit hall pattern (bits: PB7|PB6|PB5)
     */
    void setHallState(uint8_t hallState) {
        escHw_.gpioB.setInputPin(5, (hallState >> 0) & 1);
        escHw_.gpioB.setInputPin(6, (hallState >> 1) & 1);
        escHw_.gpioB.setInputPin(7, (hallState >> 2) & 1);
    }

    /* ---- Direct firmware access for testing ---- */
    esc::EscFirmware& esc() { return *esc_; }
    ble::BleFirmware& ble() { return *ble_; }
    bms::BmsFirmware& bms() { return *bms_; }

    SimEscHardware& escHw() { return escHw_; }
    SimBleHardware& bleHw() { return bleHw_; }
    SimBmsHardware& bmsHw() { return bmsHw_; }

    uint32_t totalTicks() const { return totalTickMs_; }

private:
    /**
     * @brief Drain all RX queues into firmware protocol parsers.
     *
     * Simulates the UART receive ISR triggering byte-by-byte parsing.
     * Each byte is delivered to the correct USART ISR handler.
     */
    void drainUartRx() {
        /* ESC: USART2 ← BLE side, USART3 ← BMS side */
        while (escHw_.uartBle.isRxReady()) {
            uint8_t b = escHw_.uartBle.receiveByte();
            esc_->usart2RxIsr(b);
        }
        while (escHw_.uartBms.isRxReady()) {
            uint8_t b = escHw_.uartBms.receiveByte();
            esc_->usart3RxIsr(b);
        }
        while (escHw_.uartExt.isRxReady()) {
            uint8_t b = escHw_.uartExt.receiveByte();
            esc_->usart1RxIsr(b);
        }

        /* BLE: USART1 ← nRF, USART2 ← ESC */
        while (bleHw_.uartNrf.isRxReady()) {
            uint8_t b = bleHw_.uartNrf.receiveByte();
            ble_->usart1RxIsr(b);
        }
        while (bleHw_.uartEsc.isRxReady()) {
            uint8_t b = bleHw_.uartEsc.receiveByte();
            ble_->usart2RxIsr(b);
        }

        /* BMS: USART2 ← ESC */
        while (bmsHw_.uartEsc.isRxReady()) {
            uint8_t b = bmsHw_.uartEsc.receiveByte();
            bms_->usart2RxIsr(b);
        }
    }

    /**
     * @brief Drain all TX queues from protocol channels.
     *
     * Calls drainTx() on each protocol channel. Since the channels
     * use SimUart (which is cross-linked), transmitted bytes
     * automatically appear in the peer's RX queue.
     */
    void drainUartTx() {
        esc_->channelExt().drainTx();
        esc_->channelBle().drainTx();
        esc_->channelBms().drainTx();
        ble_->channelNrf().drainTx();
        ble_->channelEsc().drainTx();
        bms_->channelEsc().drainTx();
    }

    /* Simulated hardware */
    SimEscHardware escHw_;
    SimBleHardware bleHw_;
    SimBmsHardware bmsHw_;

    /* HAL structs (stored as members so firmware refs remain valid) */
    hal::EscHal escHal_{};
    hal::BleHal bleHal_{};
    hal::BmsHal bmsHal_{};

    /* Firmware instances (heap-allocated: require HAL ref at construction) */
    std::unique_ptr<esc::EscFirmware> esc_;
    std::unique_ptr<ble::BleFirmware> ble_;
    std::unique_ptr<bms::BmsFirmware> bms_;

    bool     initialized_ = false;
    uint32_t totalTickMs_  = 0;
};

} // namespace sim
} // namespace ninebot

#endif // NINEBOT_SIM_BUS_H
