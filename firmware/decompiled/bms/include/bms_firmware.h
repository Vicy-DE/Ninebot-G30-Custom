/**
 * @file bms_firmware.h
 * @brief Decompiled BMS battery management firmware — main interface header.
 *
 * Reconstructed from BMS_1.7.4.5.bin.
 * Target: STM32F103C8T6 (64KB Flash, 20KB SRAM, 72MHz Cortex-M3)
 *         + TI BQ76940 analog front-end (14-cell monitor, I2C).
 *
 * The BMS board manages a 10S lithium-ion battery pack (36V nominal):
 *   1. Cell voltage monitoring (10 cells via BQ76940 ADC)
 *   2. Pack voltage and current measurement
 *   3. Temperature monitoring (2-3 NTC thermistors)
 *   4. Charge and discharge MOSFET control
 *   5. Cell balancing (passive, through BQ76940)
 *   6. State of charge (SOC) estimation
 *   7. Protection: OVP, UVP, OCP, SCP, OTP
 *   8. Communication with ESC via USART (Ninebot protocol)
 *
 * Architecture:
 * @code
 *   ┌───────────────────────────────────────────────┐
 *   │              BMS Battery Board                 │
 *   │                                               │
 *   │  STM32F103C8T6:                               │
 *   │    ├── I2C1 ↔ BQ76940 (cell ADC, balancing)   │
 *   │    ├── USART2 ↔ ESC (Ninebot protocol)       │
 *   │    ├── ADC1: NTC temperature sensors          │
 *   │    ├── GPIO: CHG/DSG FET control              │
 *   │    └── SysTick: 1ms timing                   │
 *   │                                               │
 *   │  BQ76940 AFE (I2C address 0x08):              │
 *   │    ├── Cell voltage ADC (cells 1-10)          │
 *   │    ├── Pack current measurement (shunt)       │
 *   │    ├── Hardware OVP/UVP/OCP/SCP               │
 *   │    ├── Cell balancing FETs                    │
 *   │    └── ALERT output → STM32 interrupt         │
 *   │                                               │
 *   │  Battery Pack: 10S 3P  (36V nom, 42V max)     │
 *   │    ├── Nominal voltage: 36.0V                 │
 *   │    ├── Maximum voltage: 42.0V (4.20V/cell)    │
 *   │    ├── Minimum voltage: 27.5V (2.75V/cell)    │
 *   │    ├── Capacity: 551 Wh (15.3 Ah)             │
 *   │    └── Max discharge: 30A continuous           │
 *   └───────────────────────────────────────────────┘
 * @endcode
 *
 * @warning BMS modifications can cause battery fires. Never bypass
 *          undervoltage or overcurrent protection. The battery pack
 *          stores 551 Wh of energy — handle with care.
 */

#ifndef NINEBOT_BMS_FIRMWARE_H
#define NINEBOT_BMS_FIRMWARE_H

#include "hal.h"
#include "protocol.h"
#include "registers.h"
#include <cstdint>
#include <functional>

namespace ninebot {
namespace bms {

/* =========================================================================
 * BQ76940 Constants
 * =========================================================================
 * I2C register addresses for the BQ76940 analog front-end.
 * @see BQ76940 datasheet (SLUSBJ2) section 7.6.
 */

/** BQ76940 I2C slave address (7-bit, without R/W bit). */
static constexpr uint8_t BQ76940_I2C_ADDR = 0x08;

/** BQ76940 register addresses. */
namespace bq_reg {
    static constexpr uint8_t SYS_STAT     = 0x00;  ///< System status
    static constexpr uint8_t CELLBAL1     = 0x01;  ///< Cell balance 1-5
    static constexpr uint8_t CELLBAL2     = 0x02;  ///< Cell balance 6-10
    static constexpr uint8_t SYS_CTRL1    = 0x04;  ///< System control 1
    static constexpr uint8_t SYS_CTRL2    = 0x05;  ///< System control 2
    static constexpr uint8_t PROTECT1     = 0x06;  ///< Protection settings 1
    static constexpr uint8_t PROTECT2     = 0x07;  ///< Protection settings 2
    static constexpr uint8_t PROTECT3     = 0x08;  ///< Protection settings 3
    static constexpr uint8_t OV_TRIP      = 0x09;  ///< Overvoltage trip
    static constexpr uint8_t UV_TRIP      = 0x0A;  ///< Undervoltage trip
    static constexpr uint8_t CC_CFG       = 0x0B;  ///< CC configuration
    static constexpr uint8_t VC1_HI       = 0x0C;  ///< Cell 1 voltage high
    static constexpr uint8_t VC1_LO       = 0x0D;  ///< Cell 1 voltage low
    // Cells 2-10 at VC1_HI + (cell-1)*2
    static constexpr uint8_t BAT_HI       = 0x2A;  ///< Pack voltage high
    static constexpr uint8_t BAT_LO       = 0x2B;  ///< Pack voltage low
    static constexpr uint8_t TS1_HI       = 0x2C;  ///< Temperature 1 high
    static constexpr uint8_t TS1_LO       = 0x2D;  ///< Temperature 1 low
    static constexpr uint8_t CC_HI        = 0x32;  ///< Coulomb counter high
    static constexpr uint8_t CC_LO        = 0x33;  ///< Coulomb counter low
    static constexpr uint8_t ADCGAIN1     = 0x50;  ///< ADC gain 1
    static constexpr uint8_t ADCOFFSET    = 0x51;  ///< ADC offset
    static constexpr uint8_t ADCGAIN2     = 0x59;  ///< ADC gain 2
}

/* =========================================================================
 * Battery Parameters
 * ========================================================================= */

static constexpr int    NUM_CELLS          = 10;     ///< 10S configuration
static constexpr int    NUM_TEMP_SENSORS   = 3;      ///< 3 NTC thermistors

static constexpr uint16_t CELL_OV_MV      = 4200;   ///< Overvoltage: 4.200V
static constexpr uint16_t CELL_UV_MV      = 2750;   ///< Undervoltage: 2.750V
static constexpr uint16_t CELL_BAL_MV     = 4100;   ///< Balance threshold: 4.100V
static constexpr uint16_t CELL_BAL_DIFF   = 30;     ///< Max imbalance: 30mV
static constexpr uint16_t PACK_MAX_MV     = 42000;  ///< Max pack voltage: 42.0V
static constexpr uint16_t PACK_MIN_MV     = 27500;  ///< Min pack voltage: 27.5V
static constexpr uint16_t MAX_CHARGE_MA   = 3000;   ///< Max charge: 3.0A
static constexpr uint16_t MAX_DISCHARGE_MA = 30000;  ///< Max discharge: 30.0A
static constexpr int16_t  OTP_THRESHOLD   = 60;     ///< Over-temp shutdown: 60°C
static constexpr int16_t  UTP_THRESHOLD   = -20;    ///< Under-temp shutdown: -20°C

/** Full capacity in milliamp-hours. */
static constexpr uint32_t FULL_CAPACITY_MAH = 15300; ///< 15.3 Ah


/* =========================================================================
 * BMS Firmware Class
 * ========================================================================= */

/**
 * Complete decompiled BMS firmware.
 *
 * Manages a 10S 3P lithium-ion battery pack through the BQ76940 AFE.
 */
class BmsFirmware {
public:
    explicit BmsFirmware(hal::BmsHal& hal);

    /** Initialize BQ76940 AFE and set protection thresholds. */
    void init();

    /** Execute one main loop iteration. */
    void mainLoopIteration();

    /** SysTick handler (1ms). */
    void sysTickHandler();

    /** USART2 RX ISR (ESC data). */
    void usart2RxIsr(uint8_t byte);

    /* ── Register & Status Access ─────────────────────────── */
    RegisterFile& registers() { return regs_; }
    const RegisterFile& registers() const { return regs_; }

    uint16_t cellVoltage(int cell) const;     ///< Cell voltage in mV (0-indexed)
    uint16_t packVoltage() const;             ///< Pack voltage in 0.01V
    int16_t  packCurrent() const;             ///< Current in 0.01A (+ = discharge)
    uint8_t  stateOfCharge() const;           ///< SOC 0-100%
    int16_t  temperature(int sensor) const;   ///< Temperature in 0.1°C
    bool     isCharging() const;              ///< Charge FET on
    bool     isDischarging() const;           ///< Discharge FET on
    uint32_t tickCount() const { return tickMs_; }

    /** Protocol channel for ESC communication. */
    ProtocolChannel& channelEsc() { return chEsc_; }

    /** Feed raw bytes (simulation). */
    void feedBytes(const uint8_t* data, size_t len);

    /* ── Simulation Helpers ───────────────────────────────── */
    void setSimCellVoltage(int cell, uint16_t mv);
    void setSimPackCurrent(int16_t centiAmps);
    void setSimTemperature(int sensor, int16_t deciCelsius);

private:
    void initRegisters();
    void initProtocol();
    void initBq76940();

    void readCellVoltages();         ///< Read all cell voltages from BQ76940
    void readPackVoltage();          ///< Read pack voltage
    void readPackCurrent();          ///< Read coulomb counter for current
    void readTemperatures();         ///< Read NTC thermistor values
    void updateSoc();                ///< Update state of charge estimate
    void checkProtection();          ///< Check all protection thresholds
    void manageCellBalance();        ///< Passive cell balancing logic
    void processProtocol();          ///< TX pump
    void handlePacket(const Packet& pkt, ProtocolChannel& respondOn);
    void handleRead(const Packet& pkt, ProtocolChannel& respondOn);
    void handleWrite(const Packet& pkt, ProtocolChannel& respondOn);
    void setChargeFet(bool on);      ///< Control charge MOSFET
    void setDischargeFet(bool on);   ///< Control discharge MOSFET

    hal::BmsHal&    hal_;
    RegisterFile    regs_;
    ProtocolChannel chEsc_;          ///< USART2: ESC mainboard

    uint32_t tickMs_               = 0;
    uint32_t lastCellReadMs_       = 0;
    uint32_t lastBalanceMs_        = 0;

    /* Battery state */
    uint16_t cellMv_[NUM_CELLS]    = {};    ///< Cell voltages in mV
    uint16_t packVoltageCv_        = 0;     ///< Pack voltage in 0.01V
    int16_t  packCurrentCa_        = 0;     ///< Pack current in 0.01A
    int16_t  tempDC_[NUM_TEMP_SENSORS] = {};///< Temperatures in 0.1°C
    uint8_t  soc_                  = 0;     ///< SOC 0-100%
    bool     chargeFetOn_          = true;  ///< Charge MOSFET state
    bool     dischargeFetOn_       = true;  ///< Discharge MOSFET state
    uint32_t accumulatedMas_       = 0;     ///< Coulomb counter (mA·ms)

    /* BQ76940 calibration */
    uint16_t adcGain_              = 365;   ///< ADC gain in µV/LSB (nominal 365)
    int8_t   adcOffset_            = 0;     ///< ADC offset in mV
};


} // namespace bms
} // namespace ninebot

#endif // NINEBOT_BMS_FIRMWARE_H
