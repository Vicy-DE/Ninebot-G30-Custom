/**
 * @file registers.h
 * @brief Ninebot G30 Max register maps for ESC, BLE, and BMS devices.
 *
 * Defines all known registers that can be read/written via the Ninebot
 * protocol.  Register addresses and semantics are extracted from:
 *   - DRV_1.6.13 firmware dispatch table @ 0x08005468
 *   - BMS_1.7.4.5 firmware register handlers
 *   - Community documentation (ScooterHacking Wiki)
 *   - Protocol sniffing with logic analyzers
 *
 * Register values are stored in a flat map for each device, allowing
 * simulation without real hardware.
 *
 * @note  BMS register modifications can affect battery safety!
 *        Always maintain protection circuits.
 */

#ifndef NINEBOT_REGISTERS_H
#define NINEBOT_REGISTERS_H

#include <cstdint>
#include <cstring>
#include <array>
#include <string>
#include <map>

namespace ninebot {

/* ===========================================================================
 * Register Metadata
 * =========================================================================== */

/**
 * Access mode for a register.
 */
enum class RegAccess : uint8_t {
    READ_ONLY,   ///< Can only be read
    READ_WRITE,  ///< Can be read and written
    WRITE_ONLY   ///< Can only be written (rare)
};

/**
 * Descriptor for a single register.
 */
struct RegisterInfo {
    uint8_t    address;     ///< Register address byte
    uint8_t    size;        ///< Size in bytes (1, 2, 4, or variable)
    RegAccess  access;      ///< Access permission
    const char* name;       ///< Human-readable name
    const char* unit;       ///< Unit of measurement (empty if none)
    const char* description;///< Brief description
};


/* ===========================================================================
 * ESC (DRV) Register Map
 * ===========================================================================
 * From protocol.md and firmware dispatch table analysis.
 */

namespace esc_reg {
    /* Read-only registers */
    static constexpr uint8_t SERIAL_NUMBER       = 0x10;  // 14 bytes, ASCII
    static constexpr uint8_t FIRMWARE_VERSION     = 0x1A;  // 2 bytes, BCD
    static constexpr uint8_t ERROR_CODE           = 0x20;  // 2 bytes, bitmask
    static constexpr uint8_t WARNING_CODE         = 0x21;  // 2 bytes, bitmask
    static constexpr uint8_t STATUS_FLAGS         = 0x22;  // 2 bytes, bitmask
    static constexpr uint8_t REMAINING_RANGE      = 0x24;  // 2 bytes, 0.01 km
    static constexpr uint8_t REMAINING_BATTERY    = 0x25;  // 2 bytes, %
    static constexpr uint8_t CURRENT_SPEED        = 0x26;  // 2 bytes, 0.001 km/h
    static constexpr uint8_t TRIP_DISTANCE        = 0x29;  // 4 bytes, meters
    static constexpr uint8_t UPTIME               = 0x2A;  // 2 bytes, seconds
    static constexpr uint8_t FRAME_TEMPERATURE    = 0x2B;  // 2 bytes, 0.1 C
    static constexpr uint8_t TOTAL_DISTANCE       = 0x34;  // 4 bytes, meters
    static constexpr uint8_t BATTERY_VOLTAGE      = 0x3A;  // 2 bytes, 0.01 V
    static constexpr uint8_t BATTERY_CURRENT      = 0x3B;  // 2 bytes, 0.01 A (signed)
    static constexpr uint8_t SPEED_LIMIT_CURRENT  = 0xB0;  // 2 bytes, km/h
    static constexpr uint8_t MOTOR_HALL_SPEED     = 0xB9;  // 2 bytes, RPM

    /* Read-write registers */
    static constexpr uint8_t LOCK_STATE           = 0x31;  // 1 byte, 0/1
    static constexpr uint8_t CRUISE_CONTROL       = 0x72;  // 1 byte, 0/1
    static constexpr uint8_t TAIL_LIGHT           = 0x73;  // 1 byte, 0/1
    static constexpr uint8_t RIDING_MODE          = 0x75;  // 1 byte, 0=Eco 1=D 2=Sport
    static constexpr uint8_t SPEED_LIMIT_SETTING  = 0x7B;  // 2 bytes, km/h
}

/** All known ESC registers with metadata. */
inline const std::array<RegisterInfo, 20>& escRegisters() {
    static const std::array<RegisterInfo, 20> regs = {{
        {0x10, 14, RegAccess::READ_ONLY,  "serial_number",      "",       "ESC serial number (ASCII)"},
        {0x1A,  2, RegAccess::READ_ONLY,  "firmware_version",   "",       "Firmware version (BCD)"},
        {0x20,  2, RegAccess::READ_ONLY,  "error_code",         "",       "Error code bitmask"},
        {0x21,  2, RegAccess::READ_ONLY,  "warning_code",       "",       "Warning code bitmask"},
        {0x22,  2, RegAccess::READ_ONLY,  "status_flags",       "",       "Status flags bitmask"},
        {0x24,  2, RegAccess::READ_ONLY,  "remaining_range",    "0.01km", "Estimated remaining range"},
        {0x25,  2, RegAccess::READ_ONLY,  "remaining_battery",  "%",      "Battery percentage"},
        {0x26,  2, RegAccess::READ_ONLY,  "current_speed",      "0.001km/h","Current speed"},
        {0x29,  4, RegAccess::READ_ONLY,  "trip_distance",      "m",      "Trip odometer"},
        {0x2A,  2, RegAccess::READ_ONLY,  "uptime",             "s",      "System uptime"},
        {0x2B,  2, RegAccess::READ_ONLY,  "frame_temperature",  "0.1C",   "Frame temperature"},
        {0x34,  4, RegAccess::READ_ONLY,  "total_distance",     "m",      "Lifetime odometer"},
        {0x3A,  2, RegAccess::READ_ONLY,  "battery_voltage",    "0.01V",  "Battery voltage"},
        {0x3B,  2, RegAccess::READ_ONLY,  "battery_current",    "0.01A",  "Battery current (signed)"},
        {0xB0,  2, RegAccess::READ_ONLY,  "speed_limit_active", "km/h",   "Active speed limit"},
        {0xB9,  2, RegAccess::READ_ONLY,  "motor_hall_speed",   "RPM",    "Motor hall sensor speed"},
        {0x31,  1, RegAccess::READ_WRITE, "lock_state",         "",       "Lock (0=unlocked, 1=locked)"},
        {0x72,  1, RegAccess::READ_WRITE, "cruise_control",     "",       "Cruise control (0=off, 1=on)"},
        {0x73,  1, RegAccess::READ_WRITE, "tail_light",         "",       "Tail light always on"},
        {0x75,  1, RegAccess::READ_WRITE, "riding_mode",        "",       "Mode (0=Eco, 1=D, 2=Sport)"},
    }};
    return regs;
}


/* ===========================================================================
 * BMS Register Map
 * =========================================================================== */

namespace bms_reg {
    static constexpr uint8_t STATUS              = 0x10;  // 2 bytes, bitmask
    static constexpr uint8_t TEMPERATURE_1       = 0x17;  // 2 bytes, 0.1 C
    static constexpr uint8_t TEMPERATURE_2       = 0x18;  // 2 bytes, 0.1 C
    static constexpr uint8_t REMAINING_CAPACITY  = 0x22;  // 2 bytes, mAh
    static constexpr uint8_t REMAINING_PERCENT   = 0x24;  // 2 bytes, %
    static constexpr uint8_t CURRENT             = 0x25;  // 2 bytes, mA (signed)
    static constexpr uint8_t VOLTAGE             = 0x26;  // 2 bytes, mV
    static constexpr uint8_t CELL_VOLTAGE_BASE   = 0x30;  // 0x30..0x39, 2 bytes each, mV
    static constexpr uint8_t MANUFACTURE_DATE    = 0x40;  // 2 bytes, packed date
    static constexpr uint8_t FULL_CAPACITY       = 0x66;  // 2 bytes, mAh
    static constexpr uint8_t CYCLE_COUNT         = 0x67;  // 2 bytes, count
    static constexpr uint8_t NUM_CELLS           = 10;    // G30 Max has 10S battery
}

/** All known BMS registers with metadata. */
inline const std::array<RegisterInfo, 15>& bmsRegisters() {
    static const std::array<RegisterInfo, 15> regs = {{
        {0x10, 2, RegAccess::READ_ONLY, "status",             "",     "BMS status bitmask"},
        {0x17, 2, RegAccess::READ_ONLY, "temperature_1",      "0.1C", "Temperature sensor 1"},
        {0x18, 2, RegAccess::READ_ONLY, "temperature_2",      "0.1C", "Temperature sensor 2"},
        {0x22, 2, RegAccess::READ_ONLY, "remaining_capacity", "mAh",  "Remaining capacity"},
        {0x24, 2, RegAccess::READ_ONLY, "remaining_percent",  "%",    "Remaining capacity %"},
        {0x25, 2, RegAccess::READ_ONLY, "current",            "mA",   "Battery current (signed)"},
        {0x26, 2, RegAccess::READ_ONLY, "voltage",            "mV",   "Total pack voltage"},
        {0x30, 2, RegAccess::READ_ONLY, "cell_1",             "mV",   "Cell 1 voltage"},
        {0x31, 2, RegAccess::READ_ONLY, "cell_2",             "mV",   "Cell 2 voltage"},
        {0x32, 2, RegAccess::READ_ONLY, "cell_3",             "mV",   "Cell 3 voltage"},
        {0x33, 2, RegAccess::READ_ONLY, "cell_4",             "mV",   "Cell 4 voltage"},
        {0x34, 2, RegAccess::READ_ONLY, "cell_5",             "mV",   "Cell 5 voltage"},
        {0x40, 2, RegAccess::READ_ONLY, "manufacture_date",   "",     "Manufacture date (packed)"},
        {0x66, 2, RegAccess::READ_ONLY, "full_capacity",      "mAh",  "Full charge capacity"},
        {0x67, 2, RegAccess::READ_ONLY, "cycle_count",        "",     "Charge cycle count"},
    }};
    return regs;
}


/* ===========================================================================
 * BLE Register Map
 * =========================================================================== */

namespace ble_reg {
    static constexpr uint8_t SERIAL_NUMBER    = 0x10;  // 14 bytes, ASCII
    static constexpr uint8_t FIRMWARE_VERSION = 0x17;  // 2 bytes
    static constexpr uint8_t MAC_ADDRESS      = 0x68;  // 6 bytes
    static constexpr uint8_t MODEL_STRING     = 0x69;  // 16 bytes
    static constexpr uint8_t PASSWORD         = 0x79;  // 1 byte
}


/* ===========================================================================
 * Register Storage
 * ===========================================================================
 * Flat byte-array storage for simulated device registers.
 */

/**
 * Simple byte-addressable register file for a simulated device.
 *
 * Each register address maps to an independent 16-byte slot.  This
 * mirrors the firmware's approach where each register has its own
 * SRAM location.  The stride ensures that consecutive register
 * addresses (e.g., 0x25 and 0x26 — both 2-byte registers) never
 * overlap in storage, even though their protocol addresses differ
 * by only 1.
 *
 * Slot layout:
 *   register 0x00 → data_[0x000..0x00F]  (16 bytes)
 *   register 0x01 → data_[0x010..0x01F]  (16 bytes)
 *   ...
 *   register 0xFF → data_[0xFF0..0xFFF]  (16 bytes)
 *
 * Each register can store up to 16 bytes (sufficient for the largest
 * register: MODEL_STRING at 16 bytes, SERIAL_NUMBER at 14 bytes).
 */
class RegisterFile {
public:
    /** Stride: bytes per register slot (must be >= largest register size). */
    static constexpr size_t SLOT_SIZE = 16;

    /** Total storage = 256 registers * 16 bytes/slot = 4096 bytes. */
    static constexpr size_t STORAGE_SIZE = 256 * SLOT_SIZE;

    RegisterFile() { std::memset(data_, 0, sizeof(data_)); }

    /** Write a uint8_t value to a register. */
    void writeU8(uint8_t reg, uint8_t value) {
        data_[off(reg)] = value;
    }

    /** Write a uint16_t value (little-endian) to a register. */
    void writeU16(uint8_t reg, uint16_t value) {
        size_t o = off(reg);
        data_[o]     = static_cast<uint8_t>(value & 0xFF);
        data_[o + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }

    /** Write a uint32_t value (little-endian) to a register. */
    void writeU32(uint8_t reg, uint32_t value) {
        size_t o = off(reg);
        data_[o]     = static_cast<uint8_t>(value & 0xFF);
        data_[o + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        data_[o + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        data_[o + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    }

    /** Write arbitrary bytes to a register slot. */
    void writeBytes(uint8_t reg, const uint8_t* data, size_t len) {
        if (len > SLOT_SIZE) len = SLOT_SIZE;
        std::memcpy(&data_[off(reg)], data, len);
    }

    /** Read a uint8_t from a register. */
    uint8_t readU8(uint8_t reg) const { return data_[off(reg)]; }

    /** Read a uint16_t (little-endian) from a register. */
    uint16_t readU16(uint8_t reg) const {
        size_t o = off(reg);
        return static_cast<uint16_t>(data_[o]) |
               (static_cast<uint16_t>(data_[o + 1]) << 8);
    }

    /** Read a uint32_t (little-endian) from a register. */
    uint32_t readU32(uint8_t reg) const {
        size_t o = off(reg);
        return static_cast<uint32_t>(data_[o]) |
               (static_cast<uint32_t>(data_[o + 1]) << 8) |
               (static_cast<uint32_t>(data_[o + 2]) << 16) |
               (static_cast<uint32_t>(data_[o + 3]) << 24);
    }

    /** Read bytes from a register slot into a buffer. */
    void readBytes(uint8_t reg, uint8_t* out, size_t len) const {
        if (len > SLOT_SIZE) len = SLOT_SIZE;
        std::memcpy(out, &data_[off(reg)], len);
    }

    /** Direct access to underlying storage. */
    const uint8_t* raw() const { return data_; }
    uint8_t* raw() { return data_; }

private:
    /** Map register address to byte offset in storage. */
    size_t off(uint8_t reg) const {
        return static_cast<size_t>(reg) * SLOT_SIZE;
    }

    uint8_t data_[STORAGE_SIZE];  ///< Slotted register storage
};


/* ===========================================================================
 * ESC Error Code Bits
 * ===========================================================================
 * From protocol.md error code table.
 */

namespace esc_error {
    static constexpr uint16_t PHASE_A_OVERCURRENT = (1 << 0);
    static constexpr uint16_t PHASE_B_OVERCURRENT = (1 << 1);
    static constexpr uint16_t PHASE_C_OVERCURRENT = (1 << 2);
    static constexpr uint16_t BUS_OVERVOLTAGE      = (1 << 3);
    static constexpr uint16_t BUS_UNDERVOLTAGE     = (1 << 4);
    static constexpr uint16_t HALL_SENSOR_ERROR    = (1 << 5);
    static constexpr uint16_t GATE_DRIVER_ERROR    = (1 << 6);
    static constexpr uint16_t THROTTLE_ERROR       = (1 << 7);
    static constexpr uint16_t CONTROLLER_TEMP_HIGH = (1 << 8);
    static constexpr uint16_t MOTOR_TEMP_HIGH      = (1 << 9);
    static constexpr uint16_t COMMUNICATION_ERROR  = (1 << 10);
}


} // namespace ninebot

#endif // NINEBOT_REGISTERS_H
