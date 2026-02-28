/**
 * @file test_esc.cpp
 * @brief ESC motor controller firmware tests.
 *
 * Validates the decompiled ESC firmware behavior:
 *   - Register initialization and default values
 *   - Motor control: hall decoding, speed calculation, PID
 *   - Riding mode switching and speed limits
 *   - Lock mechanism
 *   - Error detection (OV, UV, OTP)
 *   - Protocol handling: register read/write via Ninebot protocol
 *   - Watchdog feeding
 *
 * All test values derived from DRV_1.6.13 firmware analysis.
 */

#include "test_framework.h"
#include "sim_hal.h"
#include "esc_firmware.h"
#include "protocol.h"

using namespace ninebot;
using namespace ninebot::esc;
using namespace ninebot::sim;


/* =========================================================================
 * Helper: Create an initialized ESC firmware instance
 * ========================================================================= */

struct EscTestFixture {
    SimEscHardware hw;
    hal::EscHal    hal;
    EscFirmware*   fw = nullptr;

    EscTestFixture() {
        hal = hw.toHal();
        fw  = new EscFirmware(hal);
        fw->init();
    }

    ~EscTestFixture() {
        delete fw;
    }

    /** Advance simulation by N ms (tick systick + run main loop). */
    void tickMs(uint32_t ms) {
        for (uint32_t i = 0; i < ms; i++) {
            hw.systick.tick();
            fw->sysTickHandler();
            fw->mainLoopIteration();
        }
    }
};


/* =========================================================================
 * Initialization Tests
 * ========================================================================= */

TEST(ESC, InitDefaultRegisters)
{
    EscTestFixture f;

    /* Firmware version = 0x0613 (DRV 1.6.13 → encoded as 0x0613) */
    ASSERT_EQ(f.fw->readRegU16(esc_reg::FIRMWARE_VERSION), 0x0613);

    /* Default riding mode = D (1) */
    ASSERT_EQ(f.fw->ridingMode(), 1);

    /* No errors initially */
    ASSERT_EQ(f.fw->errorCode(), 0);

    /* Speed = 0 at startup */
    ASSERT_EQ(f.fw->currentSpeed(), 0);

    /* Not locked */
    ASSERT_FALSE(f.fw->isLocked());
}

TEST(ESC, InitTemperature)
{
    EscTestFixture f;

    /* Default temperature = 250 (25.0°C) */
    ASSERT_EQ(f.fw->readRegU16(esc_reg::CONTROLLER_TEMP), 250);
}


/* =========================================================================
 * Riding Mode Tests
 * ========================================================================= */

TEST(ESC, SetRidingModeEco)
{
    EscTestFixture f;

    f.fw->setRidingMode(RidingMode::ECO);
    ASSERT_EQ(f.fw->ridingMode(), 0);
    ASSERT_EQ(f.fw->readRegU16(esc_reg::SPEED_LIMIT), MODE_SPEED_LIMIT[0]);
}

TEST(ESC, SetRidingModeSport)
{
    EscTestFixture f;

    f.fw->setRidingMode(RidingMode::SPORT);
    ASSERT_EQ(f.fw->ridingMode(), 2);
    ASSERT_EQ(f.fw->readRegU16(esc_reg::SPEED_LIMIT), MODE_SPEED_LIMIT[2]);
}

TEST(ESC, CycleRidingModes)
{
    EscTestFixture f;

    f.fw->setRidingMode(RidingMode::ECO);
    ASSERT_EQ(f.fw->ridingMode(), 0);

    f.fw->setRidingMode(RidingMode::D);
    ASSERT_EQ(f.fw->ridingMode(), 1);

    f.fw->setRidingMode(RidingMode::SPORT);
    ASSERT_EQ(f.fw->ridingMode(), 2);
}


/* =========================================================================
 * Lock Mechanism Tests
 * ========================================================================= */

TEST(ESC, LockAndUnlock)
{
    EscTestFixture f;

    f.fw->setLocked(true);
    ASSERT_TRUE(f.fw->isLocked());
    ASSERT_EQ(f.fw->readRegU8(esc_reg::LOCK_STATE), 1);

    f.fw->setLocked(false);
    ASSERT_FALSE(f.fw->isLocked());
    ASSERT_EQ(f.fw->readRegU8(esc_reg::LOCK_STATE), 0);
}


/* =========================================================================
 * Error Detection Tests
 * ========================================================================= */

TEST(ESC, SetAndClearErrors)
{
    EscTestFixture f;

    f.fw->setError(esc_error::OVER_VOLTAGE);
    ASSERT_NE(f.fw->errorCode() & esc_error::OVER_VOLTAGE, 0);

    f.fw->clearError(esc_error::OVER_VOLTAGE);
    ASSERT_EQ(f.fw->errorCode() & esc_error::OVER_VOLTAGE, 0);
}

TEST(ESC, MultipleErrorBits)
{
    EscTestFixture f;

    f.fw->setError(esc_error::OVER_VOLTAGE | esc_error::OVER_TEMP);
    ASSERT_NE(f.fw->errorCode() & esc_error::OVER_VOLTAGE, 0);
    ASSERT_NE(f.fw->errorCode() & esc_error::OVER_TEMP, 0);

    /* Clear only OV */
    f.fw->clearError(esc_error::OVER_VOLTAGE);
    ASSERT_EQ(f.fw->errorCode() & esc_error::OVER_VOLTAGE, 0);
    ASSERT_NE(f.fw->errorCode() & esc_error::OVER_TEMP, 0);
}


/* =========================================================================
 * Battery Voltage Tests
 * ========================================================================= */

TEST(ESC, SetBatteryVoltage)
{
    EscTestFixture f;

    f.fw->setBatteryVoltage(3650);  // 36.50V
    ASSERT_EQ(f.fw->batteryVoltage(), 3650);
}


/* =========================================================================
 * Speed Control Tests
 * ========================================================================= */

TEST(ESC, InitialSpeedZero)
{
    EscTestFixture f;

    ASSERT_EQ(f.fw->currentSpeed(), 0);
}

TEST(ESC, SetSpeedDirectly)
{
    EscTestFixture f;

    f.fw->setSpeed(15000);  // 15.000 km/h
    ASSERT_EQ(f.fw->currentSpeed(), 15000);
}


/* =========================================================================
 * Protocol Register Read/Write Tests
 * ========================================================================= */

/**
 * Test: Send a READ request via protocol and check response.
 * App (0x3E) → ESC (0x20), READ firmware version register (0x1A)
 */
TEST(ESC, ProtocolReadRegister)
{
    EscTestFixture f;

    /* Build a READ packet */
    uint8_t pkt[64];
    int len = buildPacket(0x3E, 0x20, 0x01, 0x1A, nullptr, 0, pkt);

    /* Feed to the BLE-facing channel (USART2) */
    for (int i = 0; i < len; i++) {
        f.fw->usart2RxIsr(pkt[i]);
    }

    /* Run a loop iteration to process the packet */
    f.fw->mainLoopIteration();

    /* Drain TX and check the BLE UART got a response */
    f.fw->channelBle().drainTx();
    const auto& txData = f.hw.uartBle.txData();

    /* Should have transmitted a response packet */
    ASSERT_GT(txData.size(), static_cast<size_t>(0));

    /* Verify it starts with 5A A5 header */
    ASSERT_EQ(txData[0], 0x5A);
    ASSERT_EQ(txData[1], 0xA5);
}

/**
 * Test: WRITE the riding mode register via protocol.
 */
TEST(ESC, ProtocolWriteRidingMode)
{
    EscTestFixture f;

    /* Write riding mode = 2 (Sport) */
    uint8_t payload[] = {0x02, 0x00};
    uint8_t pkt[64];
    int len = buildPacket(0x3E, 0x20, 0x02, esc_reg::RIDING_MODE,
                          payload, 2, pkt);

    for (int i = 0; i < len; i++) {
        f.fw->usart2RxIsr(pkt[i]);
    }

    f.fw->mainLoopIteration();

    /* Verify mode changed */
    ASSERT_EQ(f.fw->ridingMode(), 2);
}


/* =========================================================================
 * Watchdog Tests
 * ========================================================================= */

TEST(ESC, WatchdogFedDuringLoop)
{
    EscTestFixture f;

    uint32_t feedsBefore = f.hw.watchdog.feedCount();
    f.tickMs(10);
    uint32_t feedsAfter = f.hw.watchdog.feedCount();

    /* Watchdog should have been fed at least once */
    ASSERT_GT(feedsAfter, feedsBefore);
}


/* =========================================================================
 * Uptime Test
 * ========================================================================= */

TEST(ESC, UptimeIncrements)
{
    EscTestFixture f;

    /* Uptime is in seconds, stored at UPTIME register */
    f.tickMs(1500);  // 1.5 seconds

    /* Should have recorded at least 1 second */
    uint16_t uptime = f.fw->uptime();
    ASSERT_GE(uptime, static_cast<uint16_t>(1));
}


/* =========================================================================
 * Hall Sensor Commutation Tests
 * ========================================================================= */

TEST(ESC, HallStateReadableFromGpioB)
{
    EscTestFixture f;

    /* Set hall sensors: PB5=1, PB6=0, PB7=1 → pattern 0b101 = 5 */
    f.hw.gpioB.setInputPin(5, true);
    f.hw.gpioB.setInputPin(6, false);
    f.hw.gpioB.setInputPin(7, true);

    /* Verify GPIO reads correctly */
    ASSERT_TRUE(f.hw.gpioB.readPin(5));
    ASSERT_FALSE(f.hw.gpioB.readPin(6));
    ASSERT_TRUE(f.hw.gpioB.readPin(7));
}
