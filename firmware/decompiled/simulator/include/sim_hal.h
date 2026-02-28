/**
 * @file sim_hal.h
 * @brief Simulated hardware abstraction layer for host-based testing.
 *
 * Provides software implementations of all HAL interfaces (IUart, IGpio,
 * IAdc, II2c, ITimer, ISysTick, IWatchdog) that can run on the host PC.
 *
 * Key design choices:
 *   - SimUart stores all TX bytes for verification
 *   - SimGpio tracks pin states in a 16-bit register
 *   - SimAdc returns configurable channel values
 *   - SimI2c simulates the BQ76940 register file
 *   - SimTimer tracks compare/period values without real hardware
 */

#ifndef NINEBOT_SIM_HAL_H
#define NINEBOT_SIM_HAL_H

#include "hal.h"
#include <queue>
#include <vector>
#include <map>
#include <cstring>

namespace ninebot {
namespace sim {

/* =========================================================================
 * Simulated UART
 * =========================================================================
 * Stores all transmitted bytes in a vector and queues received bytes.
 * Optionally connects to another SimUart to simulate a wired connection.
 */
class SimUart : public hal::IUart {
public:
    void sendByte(uint8_t byte) override {
        txBuffer_.push_back(byte);
        /* If wired to a peer, deliver the byte */
        if (peer_) {
            peer_->injectRxByte(byte);
        }
    }

    uint8_t receiveByte() override {
        if (rxQueue_.empty()) return 0;
        uint8_t b = rxQueue_.front();
        rxQueue_.pop();
        return b;
    }

    bool isTxEmpty() const override   { return true; }
    bool isTxComplete() const override { return true; }
    bool isRxReady() const override   { return !rxQueue_.empty(); }
    void enableTxInterrupt() override  {}
    void disableTxInterrupt() override {}
    void enableRxInterrupt() override  {}

    /** Inject a byte into the receive queue (from external source). */
    void injectRxByte(uint8_t byte) { rxQueue_.push(byte); }

    /** Inject raw data into the receive queue. */
    void injectRxData(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) rxQueue_.push(data[i]);
    }

    /** Get all bytes that have been transmitted. */
    const std::vector<uint8_t>& txData() const { return txBuffer_; }

    /** Clear the TX buffer. */
    void clearTx() { txBuffer_.clear(); }

    /** Clear the RX queue. */
    void clearRx() {
        while (!rxQueue_.empty()) rxQueue_.pop();
    }

    /** Wire this UART to a peer (bidirectional when both are wired). */
    void wireTo(SimUart* peer) { peer_ = peer; }

    /** Reset all state. */
    void reset() {
        txBuffer_.clear();
        clearRx();
    }

private:
    std::vector<uint8_t> txBuffer_;
    std::queue<uint8_t>  rxQueue_;
    SimUart*             peer_ = nullptr;
};


/* =========================================================================
 * Simulated GPIO
 * ========================================================================= */

class SimGpio : public hal::IGpio {
public:
    void setPin(uint8_t pin) override {
        if (pin < 16) odr_ |= (1 << pin);
    }

    void resetPin(uint8_t pin) override {
        if (pin < 16) odr_ &= ~(1 << pin);
    }

    bool readPin(uint8_t pin) const override {
        if (pin < 16) return (idr_ >> pin) & 1;
        return false;
    }

    uint16_t readPort() const override { return idr_; }

    void configurePin(uint8_t /*pin*/, hal::GpioMode /*mode*/) override {}

    /** Set input data register (simulate external pin state). */
    void setInputPort(uint16_t value) { idr_ = value; }

    /** Set a single input pin. */
    void setInputPin(uint8_t pin, bool level) {
        if (pin < 16) {
            if (level) idr_ |= (1 << pin);
            else       idr_ &= ~(1 << pin);
        }
    }

    /** Read the output data register. */
    uint16_t outputPort() const { return odr_; }

    /** Check if an output pin is set. */
    bool outputPin(uint8_t pin) const {
        return (pin < 16) ? ((odr_ >> pin) & 1) : false;
    }

private:
    uint16_t idr_ = 0;   ///< Input data register (external inputs)
    uint16_t odr_ = 0;   ///< Output data register (driven outputs)
};


/* =========================================================================
 * Simulated ADC
 * ========================================================================= */

class SimAdc : public hal::IAdc {
public:
    uint16_t readChannel(uint8_t channel) override {
        auto it = channels_.find(channel);
        return (it != channels_.end()) ? it->second : 0;
    }

    void startConversion(uint8_t /*channel*/) override {}
    bool isConversionDone() const override { return true; }

    /** Set the value that will be returned for a specific channel. */
    void setChannel(uint8_t channel, uint16_t value) {
        channels_[channel] = value;
    }

private:
    std::map<uint8_t, uint16_t> channels_;
};


/* =========================================================================
 * Simulated I2C (BQ76940)
 * =========================================================================
 * Simulates the BQ76940 register file. Supports read/write of all
 * registers, and provides cell voltage injection for testing.
 */
class SimI2c : public hal::II2c {
public:
    SimI2c() {
        std::memset(regs_, 0, sizeof(regs_));
        /* Default ADC gain and offset (factory calibration) */
        regs_[0x50] = 0x00;  // ADCGAIN1
        regs_[0x51] = 0x00;  // ADCOFFSET
        regs_[0x59] = 0x00;  // ADCGAIN2
    }

    bool writeRegister(uint8_t /*slaveAddr*/, uint8_t reg, uint8_t value) override {
        regs_[reg] = value;
        return true;
    }

    uint8_t readRegister(uint8_t /*slaveAddr*/, uint8_t reg) override {
        return regs_[reg];
    }

    bool readRegisters(uint8_t /*slaveAddr*/, uint8_t startReg,
                       uint8_t* buffer, uint8_t length) override {
        for (uint8_t i = 0; i < length; i++) {
            buffer[i] = regs_[startReg + i];
        }
        return true;
    }

    /** Write a register value directly (for test setup). */
    void setRegister(uint8_t reg, uint8_t value) { regs_[reg] = value; }

    /** Set a simulated cell voltage (writes to BQ76940 cell ADC registers).
     *  @param cell Cell number (0-9)
     *  @param mv   Voltage in millivolts
     */
    void setCellVoltage(int cell, uint16_t mv) {
        /* Convert mV to ADC counts: adcVal = (mv - offset) * 1000 / gain */
        uint16_t adcVal = static_cast<uint16_t>(mv * 1000UL / 365);
        uint8_t regHi = 0x0C + cell * 2;  // VC1_HI + cell*2
        uint8_t regLo = 0x0D + cell * 2;  // VC1_LO + cell*2
        regs_[regHi] = (adcVal >> 8) & 0x3F;
        regs_[regLo] = adcVal & 0xFF;
    }

    /** Set simulated pack voltage. */
    void setPackVoltage(uint16_t centV) {
        uint16_t adcVal = static_cast<uint16_t>(centV * 10000UL / (4 * 365));
        regs_[0x2A] = (adcVal >> 8) & 0xFF;
        regs_[0x2B] = adcVal & 0xFF;
    }

    /** Set simulated coulomb counter value. */
    void setCoulombCounter(int16_t ccVal) {
        regs_[0x32] = (ccVal >> 8) & 0xFF;
        regs_[0x33] = ccVal & 0xFF;
    }

    /** Set simulated temperature for a sensor.
     *  Inverts: deciC = ((2000 - adcVal) * 350 / 1000) + 250
     *  @param sensor 0=TS1, 1=TS2
     *  @param deciCelsius Temperature in deci-degrees (e.g. 650 = 65.0°C)
     */
    void setTemperature(int sensor, int16_t deciCelsius) {
        int32_t adcVal = 2000 - static_cast<int32_t>(deciCelsius - 250) * 1000 / 350;
        if (adcVal < 0) adcVal = 0;
        if (adcVal > 16383) adcVal = 16383;
        uint8_t regHi, regLo;
        if (sensor == 0) { regHi = 0x2C; regLo = 0x2D; }
        else             { regHi = 0x2E; regLo = 0x2F; }
        regs_[regHi] = (adcVal >> 8) & 0x3F;
        regs_[regLo] = adcVal & 0xFF;
    }

private:
    uint8_t regs_[256];
};


/* =========================================================================
 * Simulated Timer
 * ========================================================================= */

class SimTimer : public hal::ITimer {
public:
    explicit SimTimer(uint16_t period = 2250) : period_(period) {}

    void setCompare(uint8_t channel, uint16_t value) override {
        if (channel >= 1 && channel <= 4) ccr_[channel - 1] = value;
    }

    uint16_t getAutoReload() const override { return period_; }
    uint16_t getCounter() const override { return counter_; }
    void setEnabled(bool enabled) override { enabled_ = enabled; }

    /** Get the compare value for a channel (1-based). */
    uint16_t getCompare(uint8_t channel) const {
        return (channel >= 1 && channel <= 4) ? ccr_[channel - 1] : 0;
    }

    bool isEnabled() const { return enabled_; }

    /** Advance the counter (for simulation). */
    void tick() { counter_ = (counter_ + 1) % (period_ + 1); }

private:
    uint16_t period_  = 2250;
    uint16_t counter_ = 0;
    uint16_t ccr_[4]  = {};
    bool     enabled_ = false;
};


/* =========================================================================
 * Simulated SysTick
 * ========================================================================= */

class SimSysTick : public hal::ISysTick {
public:
    uint32_t getTickMs() const override { return tickMs_; }
    void delayMs(uint32_t ms) override { tickMs_ += ms; }

    /** Advance by 1ms. */
    void tick() { tickMs_++; }

    /** Advance by N ms. */
    void tickN(uint32_t n) { tickMs_ += n; }

    /** Set tick count directly. */
    void setTick(uint32_t t) { tickMs_ = t; }

private:
    uint32_t tickMs_ = 0;
};


/* =========================================================================
 * Simulated Watchdog
 * ========================================================================= */

class SimWatchdog : public hal::IWatchdog {
public:
    void feed() override { feedCount_++; }
    void init(uint32_t timeoutMs) override { timeout_ = timeoutMs; }

    uint32_t feedCount() const { return feedCount_; }
    uint32_t timeout() const { return timeout_; }

private:
    uint32_t feedCount_ = 0;
    uint32_t timeout_   = 0;
};


/* =========================================================================
 * Complete Simulated Board HALs
 * =========================================================================
 * Convenience structs that bundle all simulated peripherals for each board.
 */

/** Complete simulated ESC hardware. */
struct SimEscHardware {
    SimUart      uartBle;
    SimUart      uartExt;
    SimUart      uartBms;
    SimTimer     timerMotor{2250};    ///< TIM1 for motor PWM
    SimTimer     timerGeneral{0xFFFF}; ///< TIM3 general
    SimAdc       adc;
    SimGpio      gpioA;
    SimGpio      gpioB;
    SimGpio      gpioC;
    SimSysTick   systick;
    SimWatchdog  watchdog;

    /** Build an EscHal pointing to simulated peripherals. */
    hal::EscHal toHal() {
        return {
            &uartBle, &uartExt, &uartBms,
            &timerMotor, &timerGeneral,
            &adc, &gpioA, &gpioB, &gpioC,
            &systick, &watchdog
        };
    }
};

/** Complete simulated BLE hardware. */
struct SimBleHardware {
    SimUart      uartNrf;
    SimUart      uartEsc;
    SimAdc       adc;
    SimGpio      gpioA;
    SimGpio      gpioB;
    SimSysTick   systick;
    SimWatchdog  watchdog;

    hal::BleHal toHal() {
        return {
            &uartNrf, &uartEsc,
            &adc, &gpioA, &gpioB,
            &systick, &watchdog
        };
    }
};

/** Complete simulated BMS hardware. */
struct SimBmsHardware {
    SimUart      uartEsc;
    SimI2c       i2cAfe;
    SimAdc       adc;
    SimGpio      gpioA;
    SimGpio      gpioB;
    SimSysTick   systick;
    SimWatchdog  watchdog;

    hal::BmsHal toHal() {
        return {
            &uartEsc, &i2cAfe,
            &adc, &gpioA, &gpioB,
            &systick, &watchdog
        };
    }
};


} // namespace sim
} // namespace ninebot

#endif // NINEBOT_SIM_HAL_H
