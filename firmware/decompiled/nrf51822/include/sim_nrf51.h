/**
 * @file sim_nrf51.h
 * @brief Simulated nRF51822 hardware for host-based testing.
 *
 * Provides software implementations of all nRF51822 HAL interfaces
 * that run on the host PC. Includes:
 *   - SimSoftDevice: Simulated Nordic BLE stack with event injection
 *   - SimNrfUart: UART with TX buffer capture and RX injection
 *   - SimNrfGpio: 32-pin GPIO with input/output tracking
 *   - SimNrfTimer: Timer with counter and CC registers
 *   - SimNrfWdt: Watchdog with feed counting
 *   - SimPsm: Persistent storage manager with in-memory storage
 *   - SimNrf51Hardware: Complete bundled simulated hardware
 *
 * @see nrf51_hal.h for interface definitions
 */

#ifndef NINEBOT_SIM_NRF51_H
#define NINEBOT_SIM_NRF51_H

#include "nrf51_hal.h"
#include <queue>
#include <vector>
#include <map>
#include <cstring>
#include <algorithm>

namespace ninebot {
namespace sim {

/* =========================================================================
 * Simulated SoftDevice (BLE Stack)
 * =========================================================================
 * Simulates the Nordic S110 SoftDevice BLE stack.
 * Test code can inject BLE events and inspect state.
 */

class SimSoftDevice : public nrf51::ISoftDevice {
public:
    /* ─── BLE Enable & Events ─────────────────────────────────── */

    uint32_t bleEnable() override {
        enabled_ = true;
        return 0;
    }

    bool getEvent(nrf51::BleEventType& type, uint16_t& connHandle) override {
        if (eventQueue_.empty()) return false;
        auto& evt = eventQueue_.front();
        type = evt.type;
        connHandle = evt.connHandle;
        eventQueue_.pop();
        return true;
    }

    /* ─── GAP ─────────────────────────────────────────────────── */

    uint32_t setAdvData(const uint8_t* data, uint8_t len,
                        const uint8_t* /*srData*/, uint8_t /*srLen*/) override {
        advData_.assign(data, data + len);
        return 0;
    }

    uint32_t startAdvertising() override {
        advertising_ = true;
        return 0;
    }

    uint32_t stopAdvertising() override {
        advertising_ = false;
        return 0;
    }

    uint32_t setTxPower(int8_t txPower) override {
        txPower_ = txPower;
        return 0;
    }

    uint32_t disconnect(uint16_t connHandle, uint8_t /*reason*/) override {
        if (connHandle == connHandle_) {
            connected_ = false;
            connHandle_ = nrf51::BLE_CONN_HANDLE_INVALID;
        }
        return 0;
    }

    uint32_t setPPCP(uint16_t minInterval, uint16_t maxInterval,
                     uint16_t slaveLatency, uint16_t supTimeout) override {
        ppcp_ = {minInterval, maxInterval, slaveLatency, supTimeout};
        return 0;
    }

    /* ─── GATTS ───────────────────────────────────────────────── */

    uint32_t addService(uint8_t /*type*/, const uint8_t* /*uuid128*/,
                        uint16_t& handle) override {
        handle = nextHandle_++;
        return 0;
    }

    uint32_t addCharacteristic(uint16_t /*serviceHandle*/,
                               const uint8_t* /*uuid128*/,
                               uint8_t /*properties*/,
                               uint16_t& valueHandle,
                               uint16_t& cccdHandle) override {
        valueHandle = nextHandle_++;
        cccdHandle = nextHandle_++;
        return 0;
    }

    uint32_t hvx(uint16_t /*connHandle*/, uint16_t /*valueHandle*/,
                 const uint8_t* data, uint16_t len) override {
        notificationsSent_++;
        lastNotification_.assign(data, data + len);
        return 0;
    }

    /* ─── L2CAP ───────────────────────────────────────────────── */

    uint32_t l2capRegister(uint16_t cid) override {
        registeredCids_.push_back(cid);
        return 0;
    }

    uint32_t l2capTx(uint16_t /*connHandle*/, uint16_t /*cid*/,
                     const uint8_t* data, uint16_t len) override {
        l2capTxData_.assign(data, data + len);
        return 0;
    }

    /* ─── Power ───────────────────────────────────────────────── */

    uint32_t gpregretGet(uint32_t& value) override {
        value = gpregret_;
        return 0;
    }

    uint32_t dcdcModeSet(uint8_t mode) override {
        dcdcMode_ = mode;
        return 0;
    }

    /* ─── Flash ───────────────────────────────────────────────── */

    uint32_t flashWrite(uint32_t* /*dst*/, const uint32_t* /*src*/,
                        uint32_t /*wordCount*/) override {
        flashWriteCount_++;
        return 0;
    }

    uint32_t flashPageErase(uint32_t /*pageNumber*/) override {
        flashEraseCount_++;
        return 0;
    }

    /* ─── Connection status ───────────────────────────────────── */

    bool isConnected() const override { return connected_; }
    uint16_t connHandle() const override { return connHandle_; }

    /* ─── Test Injection ──────────────────────────────────────── */

    /** Simulate a BLE connection event. */
    void injectConnect(uint16_t connHandle = 0x0001) {
        connected_ = true;
        connHandle_ = connHandle;
        advertising_ = false;  /* SoftDevice auto-stops advertising on connect */
        eventQueue_.push({nrf51::BleEventType::CONNECTED, connHandle});
    }

    /** Simulate a BLE disconnection event. */
    void injectDisconnect() {
        connected_ = false;
        uint16_t handle = connHandle_;
        connHandle_ = nrf51::BLE_CONN_HANDLE_INVALID;
        eventQueue_.push({nrf51::BleEventType::DISCONNECTED, handle});
    }

    /** Simulate a flash operation complete event. */
    void injectFlashComplete() {
        eventQueue_.push({nrf51::BleEventType::SYS_EVT_FLASH_OP, 0});
    }

    /** Set GPREGRET value (for boot-time DFU check). */
    void setGpregret(uint32_t val) { gpregret_ = val; }

    /* ─── State Inspection ────────────────────────────────────── */

    bool isEnabled() const { return enabled_; }
    bool isAdvertising() const { return advertising_; }
    int8_t txPower() const { return txPower_; }
    uint8_t dcdcMode() const { return dcdcMode_; }
    uint32_t flashWriteCount() const { return flashWriteCount_; }
    uint32_t flashEraseCount() const { return flashEraseCount_; }
    uint32_t notificationsSent() const { return notificationsSent_; }
    const std::vector<uint8_t>& lastNotification() const { return lastNotification_; }
    const std::vector<uint8_t>& advData() const { return advData_; }
    const std::vector<uint16_t>& registeredCids() const { return registeredCids_; }

    struct PPCP { uint16_t min, max, latency, timeout; };
    const PPCP& ppcp() const { return ppcp_; }

private:
    struct BleEvent {
        nrf51::BleEventType type;
        uint16_t connHandle;
    };

    bool        enabled_       = false;
    bool        advertising_   = false;
    bool        connected_     = false;
    uint16_t    connHandle_    = nrf51::BLE_CONN_HANDLE_INVALID;
    uint16_t    nextHandle_    = 0x0010;
    int8_t      txPower_       = 0;
    uint8_t     dcdcMode_      = 0;
    uint32_t    gpregret_      = 0;
    uint32_t    flashWriteCount_ = 0;
    uint32_t    flashEraseCount_ = 0;
    uint32_t    notificationsSent_ = 0;
    PPCP        ppcp_{};

    std::queue<BleEvent>    eventQueue_;
    std::vector<uint8_t>    advData_;
    std::vector<uint8_t>    lastNotification_;
    std::vector<uint16_t>   registeredCids_;
    std::vector<uint8_t>    l2capTxData_;
};


/* =========================================================================
 * Simulated nRF51822 UART
 * ========================================================================= */

class SimNrfUart : public nrf51::INrfUart {
public:
    void sendByte(uint8_t byte) override {
        txBuffer_.push_back(byte);
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

    bool isTxReady() const override { return true; }
    bool isRxReady() const override { return !rxQueue_.empty(); }

    void enable() override { enabled_ = true; }
    void disable() override { enabled_ = false; }

    /* ─── Test Helpers ────────────────────────────────────────── */

    void injectRxByte(uint8_t byte) { rxQueue_.push(byte); }

    void injectRxData(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) rxQueue_.push(data[i]);
    }

    const std::vector<uint8_t>& txData() const { return txBuffer_; }
    void clearTx() { txBuffer_.clear(); }
    void clearRx() { while (!rxQueue_.empty()) rxQueue_.pop(); }
    bool isEnabled() const { return enabled_; }

    void wireTo(SimNrfUart* peer) { peer_ = peer; }

    void reset() {
        txBuffer_.clear();
        clearRx();
        enabled_ = false;
    }

private:
    std::vector<uint8_t> txBuffer_;
    std::queue<uint8_t>  rxQueue_;
    SimNrfUart*          peer_ = nullptr;
    bool                 enabled_ = false;
};


/* =========================================================================
 * Simulated nRF51822 GPIO (32 pins P0.00 – P0.31)
 * ========================================================================= */

class SimNrfGpio : public nrf51::INrfGpio {
public:
    void setPin(uint8_t pin) override {
        if (pin < 32) out_ |= (1U << pin);
    }

    void clearPin(uint8_t pin) override {
        if (pin < 32) out_ &= ~(1U << pin);
    }

    bool readPin(uint8_t pin) const override {
        if (pin < 32) return (in_ >> pin) & 1;
        return false;
    }

    void configureOutput(uint8_t pin) override {
        if (pin < 32) dirMask_ |= (1U << pin);
    }

    void configureInput(uint8_t pin, uint8_t /*pull*/) override {
        if (pin < 32) dirMask_ &= ~(1U << pin);
    }

    /* ─── Test Helpers ────────────────────────────────────────── */

    void setInputPin(uint8_t pin, bool level) {
        if (pin < 32) {
            if (level) in_ |= (1U << pin);
            else       in_ &= ~(1U << pin);
        }
    }

    bool outputPin(uint8_t pin) const {
        return (pin < 32) ? ((out_ >> pin) & 1) : false;
    }

    uint32_t outputReg() const { return out_; }
    uint32_t inputReg() const { return in_; }
    uint32_t directionReg() const { return dirMask_; }

private:
    uint32_t in_      = 0;
    uint32_t out_     = 0;
    uint32_t dirMask_ = 0;
};


/* =========================================================================
 * Simulated nRF51822 Timer
 * ========================================================================= */

class SimNrfTimer : public nrf51::INrfTimer {
public:
    void start() override { running_ = true; }
    void stop() override { running_ = false; }
    uint32_t getCounter() const override { return counter_; }
    void setCompare(uint8_t cc, uint32_t value) override {
        if (cc < 4) cc_[cc] = value;
    }
    void clear() override { counter_ = 0; }

    /* ─── Test Helpers ────────────────────────────────────────── */

    void tick() { if (running_) counter_++; }
    bool isRunning() const { return running_; }
    uint32_t getCC(uint8_t cc) const { return (cc < 4) ? cc_[cc] : 0; }

private:
    uint32_t counter_ = 0;
    uint32_t cc_[4] = {};
    bool     running_ = false;
};


/* =========================================================================
 * Simulated nRF51822 Watchdog
 * ========================================================================= */

class SimNrfWdt : public nrf51::INrfWdt {
public:
    void start(uint32_t timeoutMs) override {
        timeout_ = timeoutMs;
        running_ = true;
    }

    void feed() override { feedCount_++; }

    /* ─── Test Helpers ────────────────────────────────────────── */

    uint32_t feedCount() const { return feedCount_; }
    uint32_t timeout() const { return timeout_; }
    bool isRunning() const { return running_; }

private:
    uint32_t feedCount_ = 0;
    uint32_t timeout_ = 0;
    bool     running_ = false;
};


/* =========================================================================
 * Simulated Persistent Storage Manager (PSM)
 * =========================================================================
 * In-memory key-value store simulating flash-based persistent storage.
 */

class SimPsm : public nrf51::IPsm {
public:
    bool store(uint16_t blockId, const uint8_t* data, uint16_t len) override {
        auto& block = blocks_[blockId];
        block.assign(data, data + len);

        if (callback_) {
            callback_(nrf51::PsmOpcode::STORE, nrf51::PsmResult::SUCCESS, len);
        }
        return true;
    }

    bool load(uint16_t blockId, uint8_t* data, uint16_t maxLen,
              uint16_t& actualLen) override {
        auto it = blocks_.find(blockId);
        if (it == blocks_.end()) {
            actualLen = 0;
            if (callback_) {
                callback_(nrf51::PsmOpcode::LOAD, nrf51::PsmResult::ERROR, 0);
            }
            return false;
        }

        actualLen = static_cast<uint16_t>(
            std::min(static_cast<size_t>(maxLen), it->second.size()));
        std::memcpy(data, it->second.data(), actualLen);

        if (callback_) {
            callback_(nrf51::PsmOpcode::LOAD, nrf51::PsmResult::SUCCESS, actualLen);
        }
        return true;
    }

    bool update(uint16_t blockId, const uint8_t* data, uint16_t len) override {
        auto it = blocks_.find(blockId);
        if (it == blocks_.end()) return false;

        it->second.assign(data, data + len);
        if (callback_) {
            callback_(nrf51::PsmOpcode::UPDATE, nrf51::PsmResult::SUCCESS, len);
        }
        return true;
    }

    bool clear(uint16_t blockId) override {
        auto it = blocks_.find(blockId);
        if (it == blocks_.end()) return false;

        uint16_t len = static_cast<uint16_t>(it->second.size());
        blocks_.erase(it);
        if (callback_) {
            callback_(nrf51::PsmOpcode::CLEAR, nrf51::PsmResult::SUCCESS, len);
        }
        return true;
    }

    void registerCallback(nrf51::PsmCallback cb) override {
        callback_ = std::move(cb);
    }

    void registerFlashWriteFunc() override {
        flashWriteRegistered_ = true;
    }

    bool isFlashWriteRegistered() const override {
        return flashWriteRegistered_;
    }

    /* ─── Test Helpers ────────────────────────────────────────── */

    /** Pre-load data into a block (for test setup). */
    void preload(uint16_t blockId, const std::vector<uint8_t>& data) {
        blocks_[blockId] = data;
    }

    /** Check if a block exists. */
    bool hasBlock(uint16_t blockId) const {
        return blocks_.count(blockId) > 0;
    }

    /** Get block data directly. */
    const std::vector<uint8_t>& getBlock(uint16_t blockId) const {
        return blocks_.at(blockId);
    }

    /** Get number of stored blocks. */
    size_t blockCount() const { return blocks_.size(); }

private:
    std::map<uint16_t, std::vector<uint8_t>> blocks_;
    nrf51::PsmCallback callback_;
    bool flashWriteRegistered_ = false;
};


/* =========================================================================
 * Complete Simulated nRF51822 Hardware
 * =========================================================================
 * Bundles all simulated peripherals for the nRF51822. Provides a
 * convenience toHal() method to build the Nrf51Hal struct.
 */

struct SimNrf51Hardware {
    SimSoftDevice  softdevice;
    SimNrfUart     uart;       ///< UART0: STM32 communication
    SimNrfGpio     gpio;       ///< P0: GPIO port
    SimNrfTimer    timer1;     ///< TIMER1
    SimNrfWdt      wdt;        ///< Watchdog
    SimPsm         psm;        ///< Persistent storage

    /** Build a Nrf51Hal pointing to simulated peripherals. */
    nrf51::Nrf51Hal toHal() {
        return {
            &softdevice,
            &uart,
            &gpio,
            &timer1,
            &wdt,
            &psm
        };
    }

    /** Reset all peripherals to initial state. */
    void reset() {
        softdevice = SimSoftDevice{};
        uart.reset();
        gpio = SimNrfGpio{};
        timer1 = SimNrfTimer{};
        wdt = SimNrfWdt{};
        psm = SimPsm{};
    }
};


} // namespace sim
} // namespace ninebot

#endif // NINEBOT_SIM_NRF51_H
