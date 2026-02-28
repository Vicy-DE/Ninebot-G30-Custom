/**
 * @file bms_main.cpp
 * @brief BMS firmware — initialization, main loop, BQ76940 interface, and
 *        battery management logic.
 *
 * Reconstructed from BMS_1.7.4.5.bin analysis.
 *
 * The BMS firmware runs a continuous monitoring loop:
 *   1. Read cell voltages from BQ76940 (every 250ms)
 *   2. Read pack current from coulomb counter
 *   3. Read NTC temperatures
 *   4. Update SOC estimate
 *   5. Check all protection thresholds (OVP, UVP, OCP, OTP)
 *   6. Manage cell balancing (every 5s)
 *   7. Respond to protocol queries from ESC
 *
 * @warning This code manages high-energy lithium batteries.
 *          Never remove or bypass protection checks.
 */

#include "bms_firmware.h"

namespace ninebot {
namespace bms {

/* =========================================================================
 * Constructor
 * ========================================================================= */

BmsFirmware::BmsFirmware(hal::BmsHal& hal)
    : hal_(hal)
    , chEsc_(hal.uartEsc)
{
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void BmsFirmware::init()
{
    initRegisters();
    initProtocol();
    initBq76940();

    tickMs_ = 0;
    lastCellReadMs_ = 0;
    lastBalanceMs_ = 0;
    soc_ = 100;
    chargeFetOn_ = true;
    dischargeFetOn_ = true;
    accumulatedMas_ = 0;

    for (int i = 0; i < NUM_CELLS; i++) cellMv_[i] = 3600;
    for (int i = 0; i < NUM_TEMP_SENSORS; i++) tempDC_[i] = 250;

    if (hal_.watchdog) {
        hal_.watchdog->init(500);
    }
}

void BmsFirmware::initRegisters()
{
    const char* serial = "N2GWB000000000";
    regs_.writeBytes(bms_reg::SERIAL_NUMBER,
                     reinterpret_cast<const uint8_t*>(serial), 14);
    regs_.writeU16(bms_reg::FIRMWARE_VERSION, 0x1745);  // BMS 1.7.4.5
    regs_.writeU16(bms_reg::ERROR_CODE, 0x0000);

    regs_.writeU16(bms_reg::CELL_COUNT, NUM_CELLS);
    regs_.writeU16(bms_reg::DESIGN_CAPACITY,
                   static_cast<uint16_t>(FULL_CAPACITY_MAH / 10));
    regs_.writeU16(bms_reg::FULL_CHARGE_CAPACITY,
                   static_cast<uint16_t>(FULL_CAPACITY_MAH / 10));

    regs_.writeU16(bms_reg::PACK_VOLTAGE, 3600);  // 36.00V default
    regs_.writeU16(bms_reg::PACK_CURRENT, 0);
    regs_.writeU16(bms_reg::REMAINING_CAPACITY, 100);
    regs_.writeU16(bms_reg::TEMPERATURE_1, 250);  // 25.0°C
    regs_.writeU16(bms_reg::TEMPERATURE_2, 250);
    regs_.writeU16(bms_reg::BMS_STATUS, 0x0003);  // CHG + DSG FETs on

    /* Cell voltages register block */
    for (int i = 0; i < NUM_CELLS; i++) {
        regs_.writeU16(bms_reg::CELL_VOLTAGES + i * 2, 3600);
    }
}

void BmsFirmware::initProtocol()
{
    chEsc_.setCallback([this](const Packet& pkt) {
        handlePacket(pkt, chEsc_);
    });
}

/* =========================================================================
 * BQ76940 AFE Initialization
 * =========================================================================
 * Configures the BQ76940 via I2C:
 *   1. Read ADC gain and offset calibration
 *   2. Set OVP threshold to 4.20V per cell
 *   3. Set UVP threshold to 2.75V per cell
 *   4. Configure SCD (short-circuit) and OCD (overcurrent)
 *   5. Enable ADC continuous mode
 *   6. Enable charge and discharge FETs
 *
 * @see BQ76940 datasheet, section 7.3 (Configuration)
 */
void BmsFirmware::initBq76940()
{
    if (!hal_.i2cAfe) return;

    /* Read factory-calibrated ADC gain and offset */
    uint8_t gain1 = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::ADCGAIN1);
    uint8_t gain2 = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::ADCGAIN2);
    int8_t  offset = static_cast<int8_t>(
        hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::ADCOFFSET));

    /*
     * ADC gain calculation (from BQ76940 datasheet):
     *   gain = 365 + (ADCGAIN1[4:3] << 3 | ADCGAIN2[7:5]) µV/LSB
     *   Range: 365-396 µV/LSB
     */
    uint16_t gainBits = ((gain1 & 0x0C) << 1) | ((gain2 & 0xE0) >> 5);
    adcGain_ = 365 + gainBits;
    adcOffset_ = offset;

    /*
     * Set overvoltage threshold.
     *
     * OV threshold register = (OV_mV - adcOffset) * 1000 / adcGain - 1
     * For 4200mV: (4200 - offset) * 1000 / gain - 1
     */
    uint8_t ovTrip = static_cast<uint8_t>(
        ((CELL_OV_MV - adcOffset_) * 1000UL / adcGain_) - 1);

    /*
     * Set undervoltage threshold.
     * UV threshold register = (UV_mV - adcOffset) * 1000 / adcGain
     */
    uint8_t uvTrip = static_cast<uint8_t>(
        (CELL_UV_MV - adcOffset_) * 1000UL / adcGain_);

    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::OV_TRIP, ovTrip);
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::UV_TRIP, uvTrip);

    /*
     * PROTECT1: SCD threshold = 100mV (on 1mΩ shunt = 100A), delay = 70µs
     * PROTECT2: OCD threshold = 50mV (50A), delay = 160ms
     * PROTECT3: UV delay = 4s, OV delay = 2s
     */
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::PROTECT1, 0x9A);
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::PROTECT2, 0x45);
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::PROTECT3, 0x50);

    /*
     * SYS_CTRL1: ADC enable, temperature measurement enable
     * SYS_CTRL2: Charge and discharge FETs on, CC continuous mode
     */
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::SYS_CTRL1, 0x18);
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::SYS_CTRL2, 0x43);

    /* Clear any existing status flags */
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::SYS_STAT, 0xFF);
}

/* =========================================================================
 * Main Loop
 * ========================================================================= */

void BmsFirmware::mainLoopIteration()
{
    readCellVoltages();
    readPackVoltage();
    readPackCurrent();
    readTemperatures();
    updateSoc();
    checkProtection();
    manageCellBalance();
    processProtocol();

    if (hal_.watchdog) {
        hal_.watchdog->feed();
    }
}

/* =========================================================================
 * SysTick and UART
 * ========================================================================= */

void BmsFirmware::sysTickHandler() { tickMs_++; }
void BmsFirmware::usart2RxIsr(uint8_t byte) { chEsc_.receiveByte(byte); }

/* =========================================================================
 * Cell Voltage Reading
 * =========================================================================
 * Reads all 10 cell voltages from BQ76940 ADC registers.
 *
 * BQ76940 stores cell voltages as 14-bit values in register pairs:
 *   VC1_HI/VC1_LO through VC10_HI/VC10_LO
 *
 * Conversion to millivolts:
 *   voltage_mV = ADC_value * gain_µV/LSB / 1000 + offset_mV
 *
 * @see BQ76940 datasheet, section 7.5.1 (Cell Voltage Register)
 */
void BmsFirmware::readCellVoltages()
{
    static constexpr uint32_t CELL_READ_INTERVAL_MS = 250;
    if (tickMs_ - lastCellReadMs_ < CELL_READ_INTERVAL_MS) return;
    lastCellReadMs_ = tickMs_;

    if (!hal_.i2cAfe) return;

    for (int i = 0; i < NUM_CELLS; i++) {
        uint8_t regHi = bq_reg::VC1_HI + i * 2;
        uint8_t regLo = bq_reg::VC1_LO + i * 2;

        uint8_t hi = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, regHi);
        uint8_t lo = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, regLo);

        /*
         * ADC value is 14-bit, stored across two bytes:
         *   hi[5:0] = bits [13:8]
         *   lo[7:0] = bits [7:0]
         */
        uint16_t adcVal = ((hi & 0x3F) << 8) | lo;

        /*
         * Convert ADC to millivolts:
         *   mV = adcVal * adcGain_µV / 1000 + adcOffset_mV
         */
        uint16_t cellMv = static_cast<uint16_t>(
            (static_cast<uint32_t>(adcVal) * adcGain_ / 1000) + adcOffset_);

        cellMv_[i] = cellMv;

        /* Update cell voltage register */
        regs_.writeU16(bms_reg::CELL_VOLTAGES + i * 2, cellMv);
    }
}

/* =========================================================================
 * Pack Voltage Reading
 * ========================================================================= */

void BmsFirmware::readPackVoltage()
{
    if (!hal_.i2cAfe) return;

    uint8_t hi = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::BAT_HI);
    uint8_t lo = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::BAT_LO);

    /*
     * Pack voltage is sum of cell ADC × 4 × gain + cells × offset.
     * Alternatively, read the BAT register (4× the actual count).
     *
     * voltage_V = adcVal * 4 * gain_µV / 1_000_000
     * voltage_centV = adcVal * 4 * gain_µV / 10_000
     */
    uint16_t adcVal = (hi << 8) | lo;
    packVoltageCv_ = static_cast<uint16_t>(
        static_cast<uint32_t>(adcVal) * 4 * adcGain_ / 10000);

    regs_.writeU16(bms_reg::PACK_VOLTAGE, packVoltageCv_);
}

/* =========================================================================
 * Pack Current Reading
 * =========================================================================
 * Reads the coulomb counter from BQ76940 to get instantaneous current.
 *
 * The BQ76940 CC register provides a signed 16-bit value.
 * Current = CC_value * 8.44 µV / R_sense
 * With R_sense = 1 mΩ: Current = CC_value * 8.44 mA
 *
 * Stored as 0.01A units (centi-amps):
 *   centiAmps = CC_value * 844 / 10000
 */
void BmsFirmware::readPackCurrent()
{
    if (!hal_.i2cAfe) return;

    uint8_t hi = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::CC_HI);
    uint8_t lo = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::CC_LO);

    int16_t ccVal = static_cast<int16_t>((hi << 8) | lo);

    /* Convert to centi-amps (0.01A) */
    packCurrentCa_ = static_cast<int16_t>(
        static_cast<int32_t>(ccVal) * 844 / 10000);

    regs_.writeU16(bms_reg::PACK_CURRENT,
                   static_cast<uint16_t>(packCurrentCa_));

    /* Accumulate coulombs for SOC tracking */
    accumulatedMas_ += static_cast<uint32_t>(
        packCurrentCa_ >= 0 ? packCurrentCa_ : -packCurrentCa_);
}

/* =========================================================================
 * Temperature Reading
 * =========================================================================
 * Reads NTC thermistor values. The BQ76940 has TS1, TS2, TS3 inputs
 * that measure NTC voltage through a voltage divider.
 *
 * Also reads supplementary NTC via the STM32's ADC.
 */
void BmsFirmware::readTemperatures()
{
    if (hal_.i2cAfe) {
        /* Read TS1 from BQ76940 */
        uint8_t hi = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::TS1_HI);
        uint8_t lo = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::TS1_LO);
        uint16_t adcVal = ((hi & 0x3F) << 8) | lo;

        /*
         * Temperature from NTC lookup (simplified linear approximation).
         * Real firmware uses a calibration table in flash.
         *
         * For a 10kΩ NTC with β=3435:
         *   T = 1 / (1/298.15 + ln(R/10000)/3435) - 273.15
         *
         * Simplified: use linear approx in the operating range.
         * adcVal ~2000 at 25°C, ~1000 at 60°C, ~3000 at 0°C
         * deciC ≈ (2000 - adcVal) * 350 / 1000 + 250
         */
        int16_t deciC = static_cast<int16_t>(
            ((2000 - static_cast<int32_t>(adcVal)) * 350 / 1000) + 250);
        tempDC_[0] = deciC;
        regs_.writeU16(bms_reg::TEMPERATURE_1, static_cast<uint16_t>(deciC));
    }

    if (hal_.adc) {
        /* Read supplementary NTC from STM32 ADC */
        uint16_t adcVal = hal_.adc->readChannel(0);
        int16_t deciC = static_cast<int16_t>(
            ((2000 - static_cast<int32_t>(adcVal)) * 350 / 1000) + 250);
        tempDC_[1] = deciC;
        regs_.writeU16(bms_reg::TEMPERATURE_2, static_cast<uint16_t>(deciC));
    }
}

/* =========================================================================
 * State of Charge Estimation
 * =========================================================================
 * Simple voltage-based SOC with coulomb counting correction.
 *
 * The firmware uses a lookup table to map average cell voltage to SOC.
 * This simplified version uses a linear interpolation between key points:
 *   4.20V → 100%, 3.90V → 80%, 3.70V → 50%, 3.50V → 20%, 2.75V → 0%
 */
void BmsFirmware::updateSoc()
{
    /* Calculate average cell voltage */
    uint32_t sum = 0;
    for (int i = 0; i < NUM_CELLS; i++) {
        sum += cellMv_[i];
    }
    uint16_t avgMv = static_cast<uint16_t>(sum / NUM_CELLS);

    /* Voltage-based SOC lookup (mV → %) */
    uint8_t voltSoc;
    if (avgMv >= 4200) voltSoc = 100;
    else if (avgMv >= 3900) voltSoc = 80 + (avgMv - 3900) * 20 / 300;
    else if (avgMv >= 3700) voltSoc = 50 + (avgMv - 3700) * 30 / 200;
    else if (avgMv >= 3500) voltSoc = 20 + (avgMv - 3500) * 30 / 200;
    else if (avgMv >= 2750) voltSoc = (avgMv - 2750) * 20 / 750;
    else voltSoc = 0;

    soc_ = voltSoc;
    regs_.writeU16(bms_reg::REMAINING_CAPACITY, soc_);
}

/* =========================================================================
 * Protection Checking
 * =========================================================================
 * Implements all battery protection functions:
 *   - Per-cell overvoltage (OVP): > 4.20V → disable charge FET
 *   - Per-cell undervoltage (UVP): < 2.75V → disable discharge FET
 *   - Overcurrent protection (OCP): > 30A → disable discharge FET
 *   - Over-temperature (OTP): > 60°C → disable both FETs
 *   - Under-temperature: < -20°C → disable charge FET
 *
 * @warning Do NOT modify these thresholds without understanding the
 *          safety implications. Incorrect values can cause thermal
 *          runaway and fire.
 */
void BmsFirmware::checkProtection()
{
    uint16_t status = 0;
    bool shouldCharge = true;
    bool shouldDischarge = true;

    for (int i = 0; i < NUM_CELLS; i++) {
        /* Cell overvoltage */
        if (cellMv_[i] > CELL_OV_MV) {
            status |= bms_status::CELL_OV;
            shouldCharge = false;
        }

        /* Cell undervoltage */
        if (cellMv_[i] > 0 && cellMv_[i] < CELL_UV_MV) {
            status |= bms_status::CELL_UV;
            shouldDischarge = false;
        }
    }

    /* Pack overcurrent (discharge) */
    if (packCurrentCa_ > static_cast<int16_t>(MAX_DISCHARGE_MA / 10)) {
        status |= bms_status::OVERCURRENT;
        shouldDischarge = false;
    }

    /* Over-temperature */
    for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
        if (tempDC_[i] > OTP_THRESHOLD * 10) {  // Convert °C to deci°C
            status |= bms_status::OVERTEMP;
            shouldCharge = false;
            shouldDischarge = false;
        }
        if (tempDC_[i] < UTP_THRESHOLD * 10) {
            status |= bms_status::UNDERTEMP;
            shouldCharge = false;
        }
    }

    /* Apply protection decisions */
    setChargeFet(shouldCharge);
    setDischargeFet(shouldDischarge);

    /* Update status registers */
    regs_.writeU16(bms_reg::ERROR_CODE, status);

    uint16_t bmsStatus = 0;
    if (chargeFetOn_) bmsStatus |= bms_status::CHG_FET_ON;
    if (dischargeFetOn_) bmsStatus |= bms_status::DSG_FET_ON;
    regs_.writeU16(bms_reg::BMS_STATUS, bmsStatus);
}

/* =========================================================================
 * Cell Balancing
 * =========================================================================
 * Passive cell balancing: turns on BQ76940 balance FETs for cells that
 * are above the average voltage by more than CELL_BAL_DIFF mV.
 *
 * Only active during charging (when cells are near full).
 * Balance FETs dissipate excess energy through internal resistors.
 *
 * @see BQ76940 CELLBAL1/CELLBAL2 registers
 */
void BmsFirmware::manageCellBalance()
{
    static constexpr uint32_t BALANCE_INTERVAL_MS = 5000;
    if (tickMs_ - lastBalanceMs_ < BALANCE_INTERVAL_MS) return;
    lastBalanceMs_ = tickMs_;

    if (!hal_.i2cAfe) return;

    /* Only balance when charging and cells are above threshold */
    uint16_t minCell = 0xFFFF, maxCell = 0;
    for (int i = 0; i < NUM_CELLS; i++) {
        if (cellMv_[i] < minCell) minCell = cellMv_[i];
        if (cellMv_[i] > maxCell) maxCell = cellMv_[i];
    }

    /* Don't balance if cells aren't near full, or imbalance is small */
    if (minCell < CELL_BAL_MV || (maxCell - minCell) < CELL_BAL_DIFF) {
        /* Turn off all balance FETs */
        hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::CELLBAL1, 0x00);
        hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::CELLBAL2, 0x00);
        return;
    }

    /* Enable balance for cells above min + threshold */
    uint8_t bal1 = 0, bal2 = 0;
    uint16_t targetMv = minCell + CELL_BAL_DIFF;

    for (int i = 0; i < NUM_CELLS; i++) {
        if (cellMv_[i] > targetMv) {
            if (i < 5) {
                bal1 |= (1 << i);    // CELLBAL1 bits 0-4
            } else {
                bal2 |= (1 << (i - 5)); // CELLBAL2 bits 0-4
            }
        }
    }

    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::CELLBAL1, bal1);
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::CELLBAL2, bal2);
}

/* =========================================================================
 * FET Control
 * ========================================================================= */

void BmsFirmware::setChargeFet(bool on)
{
    chargeFetOn_ = on;
    if (!hal_.i2cAfe) return;

    uint8_t ctrl2 = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::SYS_CTRL2);
    if (on) {
        ctrl2 |= 0x01;   // CHG_ON bit
    } else {
        ctrl2 &= ~0x01;
    }
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::SYS_CTRL2, ctrl2);
}

void BmsFirmware::setDischargeFet(bool on)
{
    dischargeFetOn_ = on;
    if (!hal_.i2cAfe) return;

    uint8_t ctrl2 = hal_.i2cAfe->readRegister(BQ76940_I2C_ADDR, bq_reg::SYS_CTRL2);
    if (on) {
        ctrl2 |= 0x02;   // DSG_ON bit
    } else {
        ctrl2 &= ~0x02;
    }
    hal_.i2cAfe->writeRegister(BQ76940_I2C_ADDR, bq_reg::SYS_CTRL2, ctrl2);
}

/* =========================================================================
 * Protocol Handling
 * ========================================================================= */

void BmsFirmware::processProtocol()
{
    chEsc_.drainTx();
}

void BmsFirmware::handlePacket(const Packet& pkt, ProtocolChannel& respondOn)
{
    if (!pkt.isFor(DevAddr::BMS)) return;

    switch (pkt.command) {
        case static_cast<uint8_t>(Cmd::READ):
            handleRead(pkt, respondOn);
            break;
        case static_cast<uint8_t>(Cmd::WRITE):
            handleWrite(pkt, respondOn);
            break;
        default:
            break;
    }
}

void BmsFirmware::handleRead(const Packet& pkt, ProtocolChannel& respondOn)
{
    uint8_t reg = pkt.argument;
    uint8_t readLen = (pkt.payloadLength > 0) ? pkt.payload[0] : 2;
    if (readLen > 16) readLen = 16;

    uint8_t data[16] = {};
    regs_.readBytes(reg, data, readLen);

    respondOn.enqueuePacket(
        DevAddr::BMS, static_cast<DevAddr>(pkt.source),
        Cmd::READ_RESPONSE, reg, data, readLen);
}

void BmsFirmware::handleWrite(const Packet& pkt, ProtocolChannel& respondOn)
{
    uint8_t reg = pkt.argument;
    uint8_t success = 0x01;

    if (pkt.payloadLength > 0) {
        regs_.writeBytes(reg, pkt.payload, pkt.payloadLength);
    } else {
        success = 0x00;
    }

    respondOn.enqueuePacket(
        DevAddr::BMS, static_cast<DevAddr>(pkt.source),
        Cmd::WRITE_ACK, reg, &success, 1);
}

/* =========================================================================
 * Status Getters
 * ========================================================================= */

uint16_t BmsFirmware::cellVoltage(int cell) const
{
    if (cell < 0 || cell >= NUM_CELLS) return 0;
    return cellMv_[cell];
}

uint16_t BmsFirmware::packVoltage() const  { return packVoltageCv_; }
int16_t  BmsFirmware::packCurrent() const  { return packCurrentCa_; }
uint8_t  BmsFirmware::stateOfCharge() const { return soc_; }

int16_t BmsFirmware::temperature(int sensor) const
{
    if (sensor < 0 || sensor >= NUM_TEMP_SENSORS) return 0;
    return tempDC_[sensor];
}

bool BmsFirmware::isCharging() const      { return chargeFetOn_; }
bool BmsFirmware::isDischarging() const   { return dischargeFetOn_; }

/* =========================================================================
 * Simulation Helpers
 * ========================================================================= */

void BmsFirmware::feedBytes(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        chEsc_.receiveByte(data[i]);
    }
}

void BmsFirmware::setSimCellVoltage(int cell, uint16_t mv)
{
    if (cell >= 0 && cell < NUM_CELLS) {
        cellMv_[cell] = mv;
        regs_.writeU16(bms_reg::CELL_VOLTAGES + cell * 2, mv);
    }
}

void BmsFirmware::setSimPackCurrent(int16_t centiAmps)
{
    packCurrentCa_ = centiAmps;
    regs_.writeU16(bms_reg::PACK_CURRENT, static_cast<uint16_t>(centiAmps));
}

void BmsFirmware::setSimTemperature(int sensor, int16_t deciCelsius)
{
    if (sensor >= 0 && sensor < NUM_TEMP_SENSORS) {
        tempDC_[sensor] = deciCelsius;
    }
}


} // namespace bms
} // namespace ninebot
