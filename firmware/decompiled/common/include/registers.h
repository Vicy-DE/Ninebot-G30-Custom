/**
 * @file registers.h
 * @brief Register file storage — shared register abstraction for all boards.
 *
 * Each board (ESC, BLE, BMS) has a flat register map accessible via the
 * Ninebot protocol. This file provides the storage backend and the
 * register address definitions for all three devices.
 *
 * Register layout uses a slotted approach where each register address
 * maps to an independent 16-byte storage area, preventing overlap
 * between adjacent register addresses.
 *
 * @see DRV_1.6.13 dispatch table @ 0x08005468
 * @see docs/protocol.md for full register map
 */

#ifndef NINEBOT_REGISTERS_COMMON_H
#define NINEBOT_REGISTERS_COMMON_H

#include <cstdint>
#include <cstring>
#include <array>

namespace ninebot {

/* =========================================================================
 * Register File Storage
 * =========================================================================
 * 256 registers × 16 bytes per slot = 4096 bytes total.
 * Mirrors the firmware's SRAM register areas.
 */

class RegisterFile {
public:
    static constexpr size_t SLOT_SIZE    = 16;
    static constexpr size_t STORAGE_SIZE = 256 * SLOT_SIZE;

    RegisterFile() { std::memset(data_, 0, sizeof(data_)); }

    /* ── Write operations ────────────────────────────────────────── */
    void writeU8(uint8_t reg, uint8_t val) { data_[off(reg)] = val; }

    void writeU16(uint8_t reg, uint16_t val) {
        size_t o = off(reg);
        data_[o]     = uint8_t(val & 0xFF);
        data_[o + 1] = uint8_t((val >> 8) & 0xFF);
    }

    void writeU32(uint8_t reg, uint32_t val) {
        size_t o = off(reg);
        data_[o]     = uint8_t(val & 0xFF);
        data_[o + 1] = uint8_t((val >> 8) & 0xFF);
        data_[o + 2] = uint8_t((val >> 16) & 0xFF);
        data_[o + 3] = uint8_t((val >> 24) & 0xFF);
    }

    void writeBytes(uint8_t reg, const uint8_t* src, size_t len) {
        if (len > SLOT_SIZE) len = SLOT_SIZE;
        std::memcpy(&data_[off(reg)], src, len);
    }

    /* ── Read operations ─────────────────────────────────────────── */
    uint8_t readU8(uint8_t reg) const { return data_[off(reg)]; }

    uint16_t readU16(uint8_t reg) const {
        size_t o = off(reg);
        return uint16_t(data_[o]) | (uint16_t(data_[o + 1]) << 8);
    }

    uint32_t readU32(uint8_t reg) const {
        size_t o = off(reg);
        return uint32_t(data_[o]) | (uint32_t(data_[o + 1]) << 8) |
               (uint32_t(data_[o + 2]) << 16) | (uint32_t(data_[o + 3]) << 24);
    }

    void readBytes(uint8_t reg, uint8_t* dst, size_t len) const {
        if (len > SLOT_SIZE) len = SLOT_SIZE;
        std::memcpy(dst, &data_[off(reg)], len);
    }

    /** Direct access to raw storage. */
    const uint8_t* raw() const { return data_; }
    uint8_t*       raw()       { return data_; }

private:
    size_t off(uint8_t reg) const { return size_t(reg) * SLOT_SIZE; }
    uint8_t data_[STORAGE_SIZE];
};


/* =========================================================================
 * ESC Register Addresses
 * =========================================================================
 * From protocol.md and DRV_1.6.13 dispatch table analysis.
 */

namespace esc_reg {
    /* Read-only status registers */
    static constexpr uint8_t SERIAL_NUMBER      = 0x10;   ///< 14 bytes ASCII
    static constexpr uint8_t FIRMWARE_VERSION    = 0x1A;   ///< 2 bytes BCD
    static constexpr uint8_t ERROR_CODE          = 0x20;   ///< 2 bytes bitmask
    static constexpr uint8_t WARNING_CODE        = 0x21;   ///< 2 bytes bitmask
    static constexpr uint8_t STATUS_FLAGS        = 0x22;   ///< 2 bytes bitmask
    static constexpr uint8_t REMAINING_RANGE     = 0x24;   ///< 2 bytes, 0.01 km
    static constexpr uint8_t REMAINING_BATTERY   = 0x25;   ///< 2 bytes, %
    static constexpr uint8_t CURRENT_SPEED       = 0x26;   ///< 2 bytes, 0.001 km/h
    static constexpr uint8_t TRIP_DISTANCE       = 0x29;   ///< 4 bytes, meters
    static constexpr uint8_t UPTIME              = 0x2A;   ///< 2 bytes, seconds
    static constexpr uint8_t FRAME_TEMPERATURE   = 0x2B;   ///< 2 bytes, 0.1 °C
    static constexpr uint8_t TOTAL_DISTANCE      = 0x34;   ///< 4 bytes, meters
    static constexpr uint8_t BATTERY_VOLTAGE     = 0x3A;   ///< 2 bytes, 0.01 V
    static constexpr uint8_t BATTERY_CURRENT     = 0x3B;   ///< 2 bytes, 0.01 A (signed)
    static constexpr uint8_t SPEED_LIMIT_CURRENT = 0xB0;   ///< 2 bytes, km/h
    static constexpr uint8_t MOTOR_HALL_SPEED    = 0xB9;   ///< 2 bytes, RPM

    /* Read-write configuration registers */
    static constexpr uint8_t LOCK_STATE          = 0x31;   ///< 1 byte, 0/1
    static constexpr uint8_t CRUISE_CONTROL      = 0x72;   ///< 1 byte, 0/1
    static constexpr uint8_t TAIL_LIGHT          = 0x73;   ///< 1 byte, 0/1
    static constexpr uint8_t RIDING_MODE         = 0x75;   ///< 1 byte, 0=Eco 1=D 2=Sport
    static constexpr uint8_t SPEED_LIMIT_SETTING = 0x7B;   ///< 2 bytes, km/h

    /* ── Convenience aliases ──────────────────────────────────── */
    static constexpr uint8_t CONTROLLER_TEMP     = FRAME_TEMPERATURE;  ///< Alias for temperature
    static constexpr uint8_t SPEED_LIMIT         = SPEED_LIMIT_CURRENT; ///< Active speed limit
}

/* =========================================================================
 * BMS Register Addresses
 * ========================================================================= */

namespace bms_reg {
    static constexpr uint8_t STATUS              = 0x10;   ///< 2 bytes bitmask
    static constexpr uint8_t SERIAL_NUMBER       = 0x11;   ///< 14 bytes ASCII
    static constexpr uint8_t FIRMWARE_VERSION    = 0x17;   ///< 2 bytes
    static constexpr uint8_t TEMPERATURE_1       = 0x17;   ///< 2 bytes, 0.1 °C
    static constexpr uint8_t TEMPERATURE_2       = 0x18;   ///< 2 bytes, 0.1 °C
    static constexpr uint8_t REMAINING_CAPACITY  = 0x22;   ///< 2 bytes, mAh
    static constexpr uint8_t REMAINING_PERCENT   = 0x24;   ///< 2 bytes, %
    static constexpr uint8_t CURRENT             = 0x25;   ///< 2 bytes, mA (signed)
    static constexpr uint8_t VOLTAGE             = 0x26;   ///< 2 bytes, mV
    static constexpr uint8_t CELL_VOLTAGE_BASE   = 0x30;   ///< 0x30..0x39, 2B each, mV
    static constexpr uint8_t MANUFACTURE_DATE    = 0x40;   ///< 2 bytes, packed
    static constexpr uint8_t FULL_CAPACITY       = 0x66;   ///< 2 bytes, mAh
    static constexpr uint8_t CYCLE_COUNT         = 0x67;   ///< 2 bytes
    static constexpr int     NUM_CELLS           = 10;     ///< 10S configuration

    /* ── Additional / alias registers ─────────────────────────── */
    static constexpr uint8_t ERROR_CODE          = 0x20;   ///< 2 bytes error bitmask
    static constexpr uint8_t CELL_COUNT          = 0x41;   ///< 2 bytes, number of cells
    static constexpr uint8_t DESIGN_CAPACITY     = 0x42;   ///< 2 bytes, design capacity
    static constexpr uint8_t CELL_VOLTAGES       = CELL_VOLTAGE_BASE; ///< Alias
    static constexpr uint8_t FULL_CHARGE_CAPACITY = FULL_CAPACITY;    ///< Alias
    static constexpr uint8_t PACK_VOLTAGE        = VOLTAGE;           ///< Alias
    static constexpr uint8_t PACK_CURRENT        = CURRENT;           ///< Alias
    static constexpr uint8_t BMS_STATUS          = STATUS;            ///< Alias
    static constexpr uint8_t SOC                 = REMAINING_PERCENT; ///< SOC alias
}

/* =========================================================================
 * BLE Register Addresses
 * ========================================================================= */

namespace ble_reg {
    static constexpr uint8_t SERIAL_NUMBER       = 0x10;   ///< 14 bytes ASCII
    static constexpr uint8_t FIRMWARE_VERSION    = 0x17;   ///< 2 bytes
    static constexpr uint8_t THROTTLE_RAW        = 0x30;   ///< 2 bytes, ADC value
    static constexpr uint8_t BRAKE_RAW           = 0x31;   ///< 2 bytes, ADC value
    static constexpr uint8_t MAC_ADDRESS         = 0x68;   ///< 6 bytes
    static constexpr uint8_t MODEL_STRING        = 0x69;   ///< 16 bytes ASCII
    static constexpr uint8_t PASSWORD            = 0x79;   ///< 1 byte

    /* ── Additional registers used by BLE firmware ──────────── */
    static constexpr uint8_t ERROR_CODE          = 0x20;   ///< 2 bytes error bitmask
    static constexpr uint8_t BLE_STATUS          = 0x22;   ///< 1 byte connection status
    static constexpr uint8_t BUTTON_STATE        = 0x23;   ///< 2 bytes button input
    static constexpr uint8_t RIDING_MODE         = 0x75;   ///< 1 byte current mode
}

/* =========================================================================
 * ESC Error Code Bits
 * =========================================================================
 * From protocol.md error code table and DRV_1.6.13 error handling.
 */

namespace esc_error {
    static constexpr uint16_t PHASE_A_OVERCURRENT  = (1 << 0);
    static constexpr uint16_t PHASE_B_OVERCURRENT  = (1 << 1);
    static constexpr uint16_t PHASE_C_OVERCURRENT  = (1 << 2);
    static constexpr uint16_t BUS_OVERVOLTAGE      = (1 << 3);
    static constexpr uint16_t BUS_UNDERVOLTAGE     = (1 << 4);
    static constexpr uint16_t HALL_SENSOR_ERROR    = (1 << 5);
    static constexpr uint16_t GATE_DRIVER_ERROR    = (1 << 6);
    static constexpr uint16_t THROTTLE_ERROR       = (1 << 7);
    static constexpr uint16_t CONTROLLER_TEMP_HIGH = (1 << 8);
    static constexpr uint16_t MOTOR_TEMP_HIGH      = (1 << 9);
    static constexpr uint16_t COMMUNICATION_ERROR  = (1 << 10);

    /* ── Short aliases used in firmware sources ────────────────── */
    static constexpr uint16_t OVERVOLTAGE         = BUS_OVERVOLTAGE;
    static constexpr uint16_t OVER_VOLTAGE        = BUS_OVERVOLTAGE;      ///< Alt alias
    static constexpr uint16_t UNDERVOLTAGE        = BUS_UNDERVOLTAGE;
    static constexpr uint16_t OVERTEMP            = CONTROLLER_TEMP_HIGH;
    static constexpr uint16_t OVER_TEMP           = CONTROLLER_TEMP_HIGH; ///< Alt alias
}

/* =========================================================================
 * BMS Status Bits
 * ========================================================================= */

namespace bms_status {
    static constexpr uint16_t CHARGING         = (1 << 0);
    static constexpr uint16_t DISCHARGING      = (1 << 1);
    static constexpr uint16_t OVERVOLTAGE      = (1 << 2);
    static constexpr uint16_t UNDERVOLTAGE     = (1 << 3);
    static constexpr uint16_t OVERCURRENT      = (1 << 4);
    static constexpr uint16_t SHORT_CIRCUIT    = (1 << 5);
    static constexpr uint16_t OVERTEMP         = (1 << 6);
    static constexpr uint16_t CELL_IMBALANCE   = (1 << 7);

    /* ── Additional status bits and aliases ────────────────────── */
    static constexpr uint16_t UNDERTEMP     = (1 << 8);
    static constexpr uint16_t CELL_OV       = OVERVOLTAGE;   ///< Cell OV alias
    static constexpr uint16_t CELL_UV       = UNDERVOLTAGE;  ///< Cell UV alias
    static constexpr uint16_t CHG_FET_ON    = CHARGING;      ///< Charge FET alias
    static constexpr uint16_t DSG_FET_ON    = DISCHARGING;   ///< Discharge FET alias
}


} // namespace ninebot

#endif // NINEBOT_REGISTERS_COMMON_H
