/**
 * @file simulation.h
 * @brief Full bus simulation of the Ninebot G30 Max three-board system.
 *
 * Provides a software simulation of all three boards (ESC, BLE, BMS)
 * connected via UART.  Each simulated device:
 *   - Has a register file with realistic default values
 *   - Responds to protocol read/write commands
 *   - Can generate periodic status updates
 *
 * The simulation bus connects devices through virtual wires:
 *   Phone App <--BLE--> [BLE Board] <--UART--> [ESC Board] <--UART--> [BMS Board]
 *
 * This module is designed for:
 *   1. Protocol correctness testing (checksum, framing, address routing)
 *   2. Register read/write verification
 *   3. Multi-board communication flow testing
 *   4. Application-level integration testing
 *
 * @see DRV_1.6.13 dispatchReceivedPacket @ 0x08005468
 */

#ifndef NINEBOT_SIMULATION_H
#define NINEBOT_SIMULATION_H

#include "ninebot/protocol.h"
#include "ninebot/uart.h"
#include "ninebot/registers.h"

#include <vector>
#include <string>
#include <functional>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace ninebot {

/* ===========================================================================
 * Simulated UART Hardware
 * ===========================================================================
 */

/**
 * Software-only UART implementation for simulation/testing.
 *
 * Captures transmitted bytes into a buffer and provides them to the
 * receiving side's parser.  Always reports TX ready.
 */
class SimulatedUart : public UartHardware {
public:
    void sendByte(uint8_t byte) override {
        txBuffer_.push_back(byte);
    }

    bool isTxReady() const override { return true; }
    bool isTxComplete() const override { return true; }

    /** @return all bytes transmitted since last clear. */
    const std::vector<uint8_t>& txBuffer() const { return txBuffer_; }

    /** Clear the TX buffer. */
    void clearTxBuffer() { txBuffer_.clear(); }

    /** @return number of bytes transmitted. */
    size_t txCount() const { return txBuffer_.size(); }

private:
    std::vector<uint8_t> txBuffer_;
};


/* ===========================================================================
 * Simulated Device Base
 * ===========================================================================
 */

/**
 * Base class for a simulated Ninebot bus device.
 *
 * Handles protocol-level command dispatch (register reads/writes)
 * using the device's RegisterFile.  Subclasses add device-specific
 * behavior (e.g., BMS cell balancing, ESC motor control).
 */
class SimulatedDevice {
public:
    /**
     * @param address  Device bus address (e.g., DeviceAddress::ESC).
     * @param name     Human-readable device name for logging.
     */
    SimulatedDevice(DeviceAddress address, const std::string& name)
        : address_(address), name_(name)
    {
        uart_.setPacketCallback([this](const Packet& pkt) {
            handlePacket(pkt);
        });
    }

    virtual ~SimulatedDevice() = default;

    /* ---- Accessors ---- */
    DeviceAddress address() const { return address_; }
    const std::string& name() const { return name_; }
    RegisterFile& registers() { return registers_; }
    const RegisterFile& registers() const { return registers_; }
    UartChannelHandler& uart() { return uart_; }
    SimulatedUart& hardware() { return hw_; }

    /* ---- Register access (typed convenience) ---- */
    void setRegU8(uint8_t reg, uint8_t val)   { registers_.writeU8(reg, val); }
    void setRegU16(uint8_t reg, uint16_t val) { registers_.writeU16(reg, val); }
    void setRegU32(uint8_t reg, uint32_t val) { registers_.writeU32(reg, val); }
    uint8_t  getRegU8(uint8_t reg) const  { return registers_.readU8(reg); }
    uint16_t getRegU16(uint8_t reg) const { return registers_.readU16(reg); }
    uint32_t getRegU32(uint8_t reg) const { return registers_.readU32(reg); }

    /**
     * Feed a raw byte stream into this device's UART parser.
     * Simulates bytes arriving over the wire.
     */
    void feedBytes(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            uart_.receiveByte(data[i]);
        }
    }

    /**
     * Feed a raw byte stream from a vector.
     */
    void feedBytes(const std::vector<uint8_t>& data) {
        feedBytes(data.data(), data.size());
    }

    /**
     * Drain all TX packets and return the raw bytes produced.
     */
    std::vector<uint8_t> drainTxBytes() {
        hw_.clearTxBuffer();
        uart_.drainTx();
        return hw_.txBuffer();
    }

    /** Application-level tick (for periodic behavior). Override in subclass. */
    virtual void tick() {}

    /** Initialize default register values. Override in subclass. */
    virtual void initDefaults() {}

    /** Log of all packets received by this device. */
    const std::vector<Packet>& receivedPackets() const { return rxLog_; }

    /** Clear received packet log. */
    void clearRxLog() { rxLog_.clear(); }

protected:
    /**
     * Handle a received protocol packet.
     *
     * Default implementation processes READ (0x01) and WRITE (0x02) commands
     * against the register file.  Subclasses can override for custom behavior.
     */
    virtual void handlePacket(const Packet& pkt) {
        rxLog_.push_back(pkt);

        /* Only process packets addressed to us */
        if (!pkt.isFor(address_)) return;

        switch (pkt.command) {
            case static_cast<uint8_t>(Command::READ):
                handleRegisterRead(pkt);
                break;
            case static_cast<uint8_t>(Command::WRITE):
                handleRegisterWrite(pkt);
                break;
            default:
                break;
        }
    }

    /**
     * Handle a register read command.
     *
     * The firmware reads the requested register and sends back a response
     * packet with the register contents as payload.
     *
     * Request format:  payload[0] = number of bytes to read
     * Response format: payload = register data bytes
     */
    void handleRegisterRead(const Packet& pkt) {
        uint8_t reg = pkt.argument;
        uint8_t readLen = (pkt.payloadLength > 0) ? pkt.payload[0] : 2;

        /* Build response payload from register file */
        uint8_t responsePayload[MAX_PAYLOAD_LENGTH];
        registers_.readBytes(reg, responsePayload, readLen);

        /* Send response: swap source/destination, use READ_RESPONSE command */
        uart_.enqueuePacket(
            address_,
            static_cast<DeviceAddress>(pkt.source),
            Command::READ_RESPONSE,
            reg,
            responsePayload,
            readLen
        );
    }

    /**
     * Handle a register write command.
     *
     * The firmware writes the payload data to the specified register
     * and sends back a WRITE_RESPONSE acknowledgment.
     */
    void handleRegisterWrite(const Packet& pkt) {
        uint8_t reg = pkt.argument;

        if (pkt.payloadLength > 0) {
            registers_.writeBytes(reg, pkt.payload, pkt.payloadLength);
        }

        /* Send ACK (empty payload, WRITE_RESPONSE command) */
        uart_.enqueuePacket(
            address_,
            static_cast<DeviceAddress>(pkt.source),
            Command::WRITE_RESPONSE,
            reg,
            nullptr, 0
        );
    }

    DeviceAddress       address_;
    std::string         name_;
    RegisterFile        registers_;
    SimulatedUart       hw_;
    UartChannelHandler  uart_{UartChannel::CHANNEL_EXT, &hw_};
    std::vector<Packet> rxLog_;
};


/* ===========================================================================
 * Simulated ESC (Motor Controller)
 * ===========================================================================
 */

/**
 * Simulated ESC (DRV) device.
 *
 * Initializes with realistic default values for a Ninebot G30 Max:
 *   - Serial number: "N2GWX1234567890"
 *   - Firmware version: 1.6.13 (0x0613 BCD)
 *   - Battery: ~85%, 38.5V, 0A
 *   - Speed: 0 km/h, Mode: D (normal)
 *   - Total distance: 1234 km
 */
class SimulatedESC : public SimulatedDevice {
public:
    SimulatedESC() : SimulatedDevice(DeviceAddress::ESC, "ESC") {
        initDefaults();
    }

    void initDefaults() override {
        /* Serial number (14 bytes ASCII) */
        const char* serial = "N2GWX123456789";
        registers_.writeBytes(esc_reg::SERIAL_NUMBER,
                              reinterpret_cast<const uint8_t*>(serial), 14);

        /* Firmware version: 1.6.13 = 0x0613 in BCD-like encoding */
        registers_.writeU16(esc_reg::FIRMWARE_VERSION, 0x0613);

        /* No errors or warnings */
        registers_.writeU16(esc_reg::ERROR_CODE, 0x0000);
        registers_.writeU16(esc_reg::WARNING_CODE, 0x0000);
        registers_.writeU16(esc_reg::STATUS_FLAGS, 0x0000);

        /* Battery state */
        registers_.writeU16(esc_reg::REMAINING_BATTERY, 85);    // 85%
        registers_.writeU16(esc_reg::REMAINING_RANGE, 3200);    // 32.00 km
        registers_.writeU16(esc_reg::BATTERY_VOLTAGE, 3850);    // 38.50 V
        registers_.writeU16(esc_reg::BATTERY_CURRENT, 0);       // 0.00 A (idle)

        /* Speed and distance */
        registers_.writeU16(esc_reg::CURRENT_SPEED, 0);         // 0 km/h (stationary)
        registers_.writeU32(esc_reg::TRIP_DISTANCE, 0);         // 0 m this trip
        registers_.writeU32(esc_reg::TOTAL_DISTANCE, 1234000);  // 1234 km lifetime
        registers_.writeU16(esc_reg::UPTIME, 0);                // 0 s

        /* Temperature */
        registers_.writeU16(esc_reg::FRAME_TEMPERATURE, 250);   // 25.0 C

        /* Riding configuration */
        registers_.writeU8(esc_reg::LOCK_STATE, 0);             // Unlocked
        registers_.writeU8(esc_reg::CRUISE_CONTROL, 0);         // Off
        registers_.writeU8(esc_reg::TAIL_LIGHT, 0);             // Off
        registers_.writeU8(esc_reg::RIDING_MODE, 1);            // D (normal) mode

        /* Speed limits */
        registers_.writeU16(esc_reg::SPEED_LIMIT_CURRENT, 25);  // 25 km/h
        registers_.writeU16(esc_reg::SPEED_LIMIT_SETTING, 25);
        registers_.writeU16(esc_reg::MOTOR_HALL_SPEED, 0);       // 0 RPM
    }

    /**
     * Simulate one time tick.
     * Updates uptime and simulates basic physics if "riding".
     */
    void tick() override {
        /* Increment uptime */
        uint16_t uptime = registers_.readU16(esc_reg::UPTIME);
        registers_.writeU16(esc_reg::UPTIME, uptime + 1);
    }

    /** Set the simulated speed (in 0.001 km/h units). */
    void setSpeed(uint16_t speed_milli_kmh) {
        registers_.writeU16(esc_reg::CURRENT_SPEED, speed_milli_kmh);
    }

    /** Set riding mode (0=Eco, 1=D, 2=Sport). */
    void setRidingMode(uint8_t mode) {
        registers_.writeU8(esc_reg::RIDING_MODE, mode);
    }

    /** Simulate an error condition. */
    void setError(uint16_t errorBits) {
        registers_.writeU16(esc_reg::ERROR_CODE, errorBits);
    }
};


/* ===========================================================================
 * Simulated BMS (Battery Management)
 * ===========================================================================
 */

/**
 * Simulated BMS device.
 *
 * Models a Ninebot G30 Max 10S2P Li-ion battery pack:
 *   - 10 cells in series, ~3.85V each = 38.5V total
 *   - 5100 mAh capacity (2×2550 mAh cells in parallel)
 *   - Two temperature sensors
 *   - BQ76940 analog front-end (real hardware)
 */
class SimulatedBMS : public SimulatedDevice {
public:
    SimulatedBMS() : SimulatedDevice(DeviceAddress::BMS, "BMS") {
        initDefaults();
    }

    void initDefaults() override {
        /* Status: normal operation */
        registers_.writeU16(bms_reg::STATUS, 0x0000);

        /* Temperature sensors: 28.0 C and 27.5 C */
        registers_.writeU16(bms_reg::TEMPERATURE_1, 280);
        registers_.writeU16(bms_reg::TEMPERATURE_2, 275);

        /* Battery capacity */
        registers_.writeU16(bms_reg::REMAINING_CAPACITY, 4335);  // 4335 mAh (85%)
        registers_.writeU16(bms_reg::REMAINING_PERCENT, 85);
        registers_.writeU16(bms_reg::FULL_CAPACITY, 5100);       // 5100 mAh total
        registers_.writeU16(bms_reg::CYCLE_COUNT, 142);          // 142 charge cycles

        /* Current and voltage */
        registers_.writeU16(bms_reg::CURRENT, 0);     // 0 mA (idle)
        registers_.writeU16(bms_reg::VOLTAGE, 38500);  // 38500 mV = 38.5V

        /* Individual cell voltages (10 cells, slight variation is normal) */
        uint16_t cellVoltages[bms_reg::NUM_CELLS] = {
            3852, 3850, 3848, 3853, 3849,
            3851, 3847, 3854, 3850, 3846
        };
        for (int i = 0; i < bms_reg::NUM_CELLS; ++i) {
            registers_.writeU16(bms_reg::CELL_VOLTAGE_BASE + i, cellVoltages[i]);
        }

        /* Manufacture date: 2021-06-15 (packed: year<<9 | month<<5 | day) */
        uint16_t mfgDate = (2021 - 1980) << 9 | 6 << 5 | 15;
        registers_.writeU16(bms_reg::MANUFACTURE_DATE, mfgDate);
    }

    /** Set all 10 cell voltages from an array (mV). */
    void setCellVoltages(const uint16_t voltages[10]) {
        uint32_t total = 0;
        for (int i = 0; i < bms_reg::NUM_CELLS; ++i) {
            registers_.writeU16(bms_reg::CELL_VOLTAGE_BASE + i, voltages[i]);
            total += voltages[i];
        }
        registers_.writeU16(bms_reg::VOLTAGE, static_cast<uint16_t>(total));
    }

    /** Set battery current (mA, signed — negative = discharging). */
    void setCurrent(int16_t current_mA) {
        registers_.writeU16(bms_reg::CURRENT, static_cast<uint16_t>(current_mA));
    }

    /** Set remaining capacity (mAh and percentage). */
    void setCapacity(uint16_t mAh, uint16_t percent) {
        registers_.writeU16(bms_reg::REMAINING_CAPACITY, mAh);
        registers_.writeU16(bms_reg::REMAINING_PERCENT, percent);
    }

    /** Get minimum cell voltage (mV). */
    uint16_t minCellVoltage() const {
        uint16_t minV = 0xFFFF;
        for (int i = 0; i < bms_reg::NUM_CELLS; ++i) {
            uint16_t v = registers_.readU16(bms_reg::CELL_VOLTAGE_BASE + i);
            if (v < minV) minV = v;
        }
        return minV;
    }

    /** Get maximum cell voltage (mV). */
    uint16_t maxCellVoltage() const {
        uint16_t maxV = 0;
        for (int i = 0; i < bms_reg::NUM_CELLS; ++i) {
            uint16_t v = registers_.readU16(bms_reg::CELL_VOLTAGE_BASE + i);
            if (v > maxV) maxV = v;
        }
        return maxV;
    }

    /** Cell voltage imbalance (max - min) in mV. */
    uint16_t cellImbalance() const {
        return maxCellVoltage() - minCellVoltage();
    }
};


/* ===========================================================================
 * Simulated BLE (Dashboard)
 * ===========================================================================
 */

/**
 * Simulated BLE dashboard device.
 *
 * Models the front display/bluetooth board with:
 *   - Serial number, firmware version, MAC address
 *   - Throttle input, brake input
 *   - Display state
 */
class SimulatedBLE : public SimulatedDevice {
public:
    SimulatedBLE() : SimulatedDevice(DeviceAddress::BLE, "BLE") {
        initDefaults();
    }

    void initDefaults() override {
        const char* serial = "N2GBL123456789";
        registers_.writeBytes(ble_reg::SERIAL_NUMBER,
                              reinterpret_cast<const uint8_t*>(serial), 14);

        registers_.writeU16(ble_reg::FIRMWARE_VERSION, 0x0117);

        /* BLE MAC address: AA:BB:CC:DD:EE:FF */
        uint8_t mac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        registers_.writeBytes(ble_reg::MAC_ADDRESS, mac, 6);

        /* Model string */
        const char* model = "Ninebot Max G30";
        registers_.writeBytes(ble_reg::MODEL_STRING,
                              reinterpret_cast<const uint8_t*>(model), 15);
    }
};


/* ===========================================================================
 * Simulation Bus
 * ===========================================================================
 */

/**
 * Connects simulated devices together and manages message routing.
 *
 * Topology:
 * @code
 *   [BLE] <---- UART ----> [ESC] <---- UART ----> [BMS]
 *    │
 *    └── (BLE receives phone app commands via setAppChannel)
 * @endcode
 *
 * The bus transfers TX output from one device's UART into the
 * receiving device's parser, simulating the physical wire connection.
 */
class SimulationBus {
public:
    SimulationBus()
        : esc_(), ble_(), bms_()
    {}

    /* ---- Device accessors ---- */
    SimulatedESC& esc() { return esc_; }
    SimulatedBLE& ble() { return ble_; }
    SimulatedBMS& bms() { return bms_; }

    /**
     * Transfer all pending TX bytes between connected devices.
     *
     * Routing:
     *   ESC TX -> BLE RX (if packet addressed to BLE/APP)
     *   ESC TX -> BMS RX (if packet addressed to BMS)
     *   BLE TX -> ESC RX
     *   BMS TX -> ESC RX
     *
     * In practice, the firmware sends all TX out one wire and the
     * destination device filters by address. We simulate this by
     * feeding all TX bytes to the connected parser.
     *
     * @return Total bytes transferred.
     */
    size_t transferAll() {
        size_t total = 0;

        /* ESC -> BLE (USART2 channel on ESC connects to BLE) */
        auto escTx = esc_.drainTxBytes();
        if (!escTx.empty()) {
            ble_.feedBytes(escTx);
            total += escTx.size();
        }

        /* BLE -> ESC */
        auto bleTx = ble_.drainTxBytes();
        if (!bleTx.empty()) {
            esc_.feedBytes(bleTx);
            total += bleTx.size();
        }

        /* ESC -> BMS (USART3 channel on ESC connects to BMS) */
        /* BMS -> ESC */
        auto bmsTx = bms_.drainTxBytes();
        if (!bmsTx.empty()) {
            esc_.feedBytes(bmsTx);
            total += bmsTx.size();
        }

        return total;
    }

    /**
     * Inject a raw packet from an "external" source (e.g., phone app)
     * into the specified device's parser.
     *
     * @param target Device to receive the packet.
     * @param rawPacket Complete packet bytes including 0x5A 0xA5 header.
     * @param len Number of bytes.
     */
    void inject(SimulatedDevice& target, const uint8_t* rawPacket, size_t len) {
        target.feedBytes(rawPacket, len);
    }

    /**
     * Build and inject a protocol packet into a device.
     *
     * Convenience method that constructs the wire-format packet
     * and feeds it byte-by-byte into the target's parser.
     *
     * @return Total packet size injected.
     */
    size_t sendTo(SimulatedDevice& target,
                  DeviceAddress source, DeviceAddress destination,
                  Command command, uint8_t argument,
                  const uint8_t* payload = nullptr, uint8_t payloadLen = 0)
    {
        uint8_t buffer[MAX_PACKET_SIZE];
        size_t len = buildPacket(
            static_cast<uint8_t>(source),
            static_cast<uint8_t>(destination),
            static_cast<uint8_t>(command),
            argument, payload, payloadLen, buffer
        );
        target.feedBytes(buffer, len);
        return len;
    }

    /**
     * Run one full simulation cycle:
     *   1. Tick all devices
     *   2. Transfer all pending messages
     *
     * @return Total bytes transferred.
     */
    size_t cycle() {
        esc_.tick();
        ble_.tick();
        bms_.tick();
        return transferAll();
    }

    /** Run multiple cycles. */
    size_t runCycles(int count) {
        size_t total = 0;
        for (int i = 0; i < count; ++i) {
            total += cycle();
        }
        return total;
    }

private:
    SimulatedESC esc_;
    SimulatedBLE ble_;
    SimulatedBMS bms_;
};


/* ===========================================================================
 * Utility: Packet Hex Dump
 * ===========================================================================
 */

/**
 * Format a byte buffer as a hex string for display/logging.
 */
inline std::string hexDump(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        if (i > 0) oss << ' ';
        oss << std::uppercase << std::hex << std::setfill('0')
            << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

/**
 * Format a Packet struct as a human-readable string.
 */
inline std::string formatPacket(const Packet& pkt) {
    std::ostringstream oss;
    oss << "Packet { src=0x" << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<int>(pkt.source)
        << " dst=0x" << std::setw(2) << static_cast<int>(pkt.destination)
        << " cmd=0x" << std::setw(2) << static_cast<int>(pkt.command)
        << " arg=0x" << std::setw(2) << static_cast<int>(pkt.argument)
        << " len=" << std::dec << static_cast<int>(pkt.payloadLength);
    if (pkt.payloadLength > 0) {
        oss << " payload=[" << hexDump(pkt.payload, pkt.payloadLength) << "]";
    }
    oss << " chk=0x" << std::hex << std::setw(4) << pkt.checksum << " }";
    return oss.str();
}


} // namespace ninebot

#endif // NINEBOT_SIMULATION_H
