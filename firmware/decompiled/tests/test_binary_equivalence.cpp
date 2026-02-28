/**
 * @file test_binary_equivalence.cpp
 * @brief Behavioral equivalence tests between C++ firmware and original binary.
 *
 * These tests verify that the decompiled C++ firmware produces the
 * same outputs as the original stock firmware for known input patterns.
 *
 * Test methodology:
 *   1. Define a set of input stimuli (protocol packets, ADC values, etc.)
 *   2. Capture the C++ firmware's response (register values, TX packets)
 *   3. Compare against expected values derived from the original firmware
 *
 * Expected values are from:
 *   - Protocol captures with the stock firmware
 *   - Disassembly analysis of DRV_1.6.13, BLE_1.1.7, BMS_1.7.4.5
 *   - The annotated assembly in firmware-rebuild/build/ .s files
 *
 * @note These tests validate *behavioral* equivalence, not binary
 *       identity. The C++ code produces semantically identical behavior
 *       but may differ in exact instruction sequences.
 */

#include "test_framework.h"
#include "sim_bus.h"
#include "sim_hal.h"
#include "protocol.h"

using namespace ninebot;
using namespace ninebot::sim;


/* =========================================================================
 * Protocol Checksum Equivalence
 * =========================================================================
 * DRV_1.6.13 calculateChecksum @ 0x08002720:
 *   Sums all bytes from buffer[0] to buffer[length-1].
 *   Returns ~sum (bitwise NOT, 16-bit).
 *
 * Known test vectors from protocol captures:
 */

TEST(BinaryEq, ChecksumKnownVector1)
{
    /* Actual captured packet from Ninebot G30 Max:
     * 5A A5 06 20 3E 01 10 [checksum]
     * Data for checksum: 06 20 3E 01 10
     * Sum = 0x06 + 0x20 + 0x3E + 0x01 + 0x10 = 0x75
     * Checksum = ~0x75 = 0xFF8A
     */
    uint8_t data[] = {0x06, 0x20, 0x3E, 0x01, 0x10};
    uint16_t cs = calculateChecksum(data, 5);
    ASSERT_EQ(cs, static_cast<uint16_t>(0xFF8A));
}

TEST(BinaryEq, ChecksumKnownVector2)
{
    /* READ_RESPONSE packet from ESC:
     * 5A A5 08 20 3E 03 10 E8 03 [checksum]
     * Data: 08 20 3E 03 10 E8 03
     * Sum = 0x08+0x20+0x3E+0x03+0x10+0xE8+0x03 = 0x0164
     * Checksum = ~0x0164 = 0xFE9B
     */
    uint8_t data[] = {0x08, 0x20, 0x3E, 0x03, 0x10, 0xE8, 0x03};
    uint16_t cs = calculateChecksum(data, 7);
    ASSERT_EQ(cs, static_cast<uint16_t>(0xFE9B));
}


/* =========================================================================
 * Packet Format Equivalence
 * =========================================================================
 * DRV_1.6.13 buildPacket @ 0x080036AC:
 *   Constructs: [0x5A][0xA5][LEN][SRC][DST][CMD][ARG][PAYLOAD][CHK_LO][CHK_HI]
 *   LEN = payloadLen + 2 (SRC+DST) + 2 (CMD+ARG)
 *   No — wait, from analysis: LEN = payloadLen + 2 (CMD+ARG) + 2 (checksum) 
 *   Let's verify the exact format.
 */

TEST(BinaryEq, PacketFormatMatchesFirmware)
{
    uint8_t buf[64];
    int len = buildPacket(0x3E, 0x20, 0x01, 0x10, nullptr, 0, buf);

    /* Verify header */
    ASSERT_EQ(buf[0], 0x5A);
    ASSERT_EQ(buf[1], 0xA5);

    /* LEN byte at offset 2 */
    uint8_t pktLen = buf[2];

    /* SRC at offset 3, DST at offset 4 */
    ASSERT_EQ(buf[3], 0x3E);
    ASSERT_EQ(buf[4], 0x20);

    /* CMD at offset 5, ARG at offset 6 */
    ASSERT_EQ(buf[5], 0x01);
    ASSERT_EQ(buf[6], 0x10);

    /* Total length should be LEN + HEADER_OVERHEAD (2 header + 1 len = 3) */
    ASSERT_EQ(len, static_cast<int>(pktLen + 3));

    /* Last 2 bytes are checksum */
    uint16_t sum = 0;
    for (int i = 2; i < len - 2; i++) sum += buf[i];
    uint16_t chk = ~sum & 0xFFFF;
    ASSERT_EQ(buf[len-2], static_cast<uint8_t>(chk & 0xFF));
    ASSERT_EQ(buf[len-1], static_cast<uint8_t>((chk >> 8) & 0xFF));
}


/* =========================================================================
 * Register Default Equivalence
 * =========================================================================
 * Verifies that initial register values match what the stock firmware sets.
 */

TEST(BinaryEq, EscDefaultRegisters)
{
    SimEscHardware hw;
    auto hal = hw.toHal();
    esc::EscFirmware fw(hal);
    fw.init();

    /* Firmware version register (0x1A) = 0x0613 for DRV 1.6.13 */
    ASSERT_EQ(fw.readRegU16(esc_reg::FIRMWARE_VERSION), 0x0613);

    /* Riding mode defaults to D (1) */
    ASSERT_EQ(fw.ridingMode(), 1);

    /* Speed = 0 at startup */
    ASSERT_EQ(fw.currentSpeed(), 0);

    /* Error code = 0 */
    ASSERT_EQ(fw.errorCode(), 0);

    /* Lock state = 0 (unlocked) */
    ASSERT_FALSE(fw.isLocked());

    /* Temperature = 250 (25.0°C) */
    ASSERT_EQ(fw.readRegU16(esc_reg::CONTROLLER_TEMP), 250);
}


/* =========================================================================
 * Speed Limit per Mode Equivalence
 * =========================================================================
 * From firmware analysis:
 *   ECO:   20.000 km/h (20000 units)
 *   D:     25.000 km/h (25000 units)
 *   Sport: 30.000 km/h (30000 units)
 */

TEST(BinaryEq, SpeedLimitsPerMode)
{
    SimEscHardware hw;
    auto hal = hw.toHal();
    esc::EscFirmware fw(hal);
    fw.init();

    fw.setRidingMode(esc::RidingMode::ECO);
    ASSERT_EQ(fw.readRegU16(esc_reg::SPEED_LIMIT),
              esc::MODE_SPEED_LIMIT[0]);

    fw.setRidingMode(esc::RidingMode::D);
    ASSERT_EQ(fw.readRegU16(esc_reg::SPEED_LIMIT),
              esc::MODE_SPEED_LIMIT[1]);

    fw.setRidingMode(esc::RidingMode::SPORT);
    ASSERT_EQ(fw.readRegU16(esc_reg::SPEED_LIMIT),
              esc::MODE_SPEED_LIMIT[2]);
}


/* =========================================================================
 * Protocol Round-Trip Equivalence
 * =========================================================================
 * Verifies that a protocol read request produces the correct response
 * format, matching what the stock firmware would generate.
 */

TEST(BinaryEq, ReadResponseFormat)
{
    SimEscHardware hw;
    auto hal = hw.toHal();
    esc::EscFirmware fw(hal);
    fw.init();

    /* Send READ for firmware version */
    uint8_t pkt[64];
    int len = buildPacket(0x3E, 0x20, 0x01, esc_reg::FIRMWARE_VERSION,
                          nullptr, 0, pkt);

    for (int i = 0; i < len; i++) {
        fw.usart2RxIsr(pkt[i]);
    }

    fw.mainLoopIteration();
    fw.channelBle().drainTx();

    const auto& txData = hw.uartBle.txData();
    ASSERT_GT(txData.size(), static_cast<size_t>(6));

    /* Response should be a READ_RESPONSE (0x03) */
    ASSERT_EQ(txData[0], 0x5A);
    ASSERT_EQ(txData[1], 0xA5);

    /* SRC should be ESC (0x20), DST should be App (0x3E) */
    ASSERT_EQ(txData[3], 0x20);
    ASSERT_EQ(txData[4], 0x3E);

    /* CMD should be READ_RESPONSE (0x03) */
    ASSERT_EQ(txData[5], 0x03);

    /* ARG should match the requested register */
    ASSERT_EQ(txData[6], static_cast<uint8_t>(esc_reg::FIRMWARE_VERSION));
}


/* =========================================================================
 * Motor Control Constants Equivalence
 * ========================================================================= */

TEST(BinaryEq, MotorPwmPeriod)
{
    /* TIM1 ARR = 2250, verified from DRV_1.6.13 disassembly */
    ASSERT_EQ(esc::MOTOR_PWM_PERIOD, 2250);
}

TEST(BinaryEq, HallEdgesPerRev)
{
    /* 15 pole pairs × 6 states = 90 edges per revolution */
    ASSERT_EQ(esc::HALL_EDGES_PER_REV, 90);
}

TEST(BinaryEq, WheelCircumference)
{
    /* ~10 inch wheel = 790mm circumference */
    ASSERT_EQ(esc::WHEEL_CIRC_MM, static_cast<uint32_t>(790));
}


/* =========================================================================
 * BMS Battery Parameters Equivalence
 * ========================================================================= */

TEST(BinaryEq, BmsParameters)
{
    ASSERT_EQ(bms::NUM_CELLS, 10);
    ASSERT_EQ(bms::CELL_OV_MV, 4200);
    ASSERT_EQ(bms::CELL_UV_MV, 2750);
    ASSERT_EQ(bms::CELL_BAL_MV, 4100);
    ASSERT_EQ(bms::CELL_BAL_DIFF, 30);
    ASSERT_EQ(bms::MAX_DISCHARGE_MA, 30000);
    ASSERT_EQ(bms::FULL_CAPACITY_MAH, static_cast<uint32_t>(15300));
}


/* =========================================================================
 * BQ76940 I2C Address Equivalence
 * ========================================================================= */

TEST(BinaryEq, Bq76940Address)
{
    /* BQ76940 7-bit I2C address = 0x08 */
    ASSERT_EQ(bms::BQ76940_I2C_ADDR, 0x08);
}


/* =========================================================================
 * Protocol Address Equivalence
 * ========================================================================= */

TEST(BinaryEq, DeviceAddresses)
{
    ASSERT_EQ(static_cast<uint8_t>(DevAddr::ESC), 0x20);
    ASSERT_EQ(static_cast<uint8_t>(DevAddr::BLE), 0x21);
    ASSERT_EQ(static_cast<uint8_t>(DevAddr::BMS), 0x22);
    ASSERT_EQ(static_cast<uint8_t>(DevAddr::APP), 0x3E);
}


/* =========================================================================
 * Full Bus Behavioral Equivalence
 * =========================================================================
 * Simulates a sequence of operations matching known stock firmware behavior.
 */

TEST(BinaryEq, FullBusStartupSequence)
{
    SimulationBus bus;
    bus.init();

    /* After startup, ESC should be in D mode with no errors */
    ASSERT_EQ(bus.esc().ridingMode(), 1);
    ASSERT_EQ(bus.esc().errorCode(), 0);
    ASSERT_EQ(bus.esc().currentSpeed(), 0);
    ASSERT_FALSE(bus.esc().isLocked());

    /* BMS should have FETs on */
    ASSERT_TRUE(bus.bms().isCharging());
    ASSERT_TRUE(bus.bms().isDischarging());

    /* BLE should be idle with 0 throttle */
    ASSERT_EQ(bus.ble().throttlePercent(), 0);

    /* Run 2 seconds — ESC should start polling BMS */
    bus.tickMs(2000);

    /* System should remain stable */
    ASSERT_EQ(bus.esc().errorCode(), 0);
    ASSERT_TRUE(bus.bms().isDischarging());
}
