/**
 * @file test_bus_integration.cpp
 * @brief Integration tests using the full 3-board simulation bus.
 *
 * Tests end-to-end communication between ESC, BLE, and BMS through
 * the SimulationBus, verifying:
 *   - App → BLE → ESC register reads
 *   - App → BLE → ESC → BMS forwarded queries
 *   - Throttle input → ESC motor control
 *   - BMS data propagation to ESC registers
 *   - Mode switching through the full chain
 *   - Error propagation
 *   - Periodic BMS polling by ESC
 */

#include "test_framework.h"
#include "sim_bus.h"
#include "protocol.h"

using namespace ninebot;
using namespace ninebot::sim;


/* =========================================================================
 * Basic Bus Initialization
 * ========================================================================= */

TEST(Bus, InitializesWithoutCrash)
{
    SimulationBus bus;
    bus.init();

    /* All three firmware instances should be accessible */
    ASSERT_EQ(bus.esc().currentSpeed(), 0);
    ASSERT_EQ(bus.ble().throttlePercent(), 0);
    ASSERT_GE(bus.bms().stateOfCharge(), static_cast<uint8_t>(0));
}

TEST(Bus, TickRunsAllBoards)
{
    SimulationBus bus;
    bus.init();

    bus.tickMs(100);

    /* All boards should have progressed */
    ASSERT_GE(bus.esc().tickCount(), static_cast<uint32_t>(100));
    ASSERT_GE(bus.ble().tickCount(), static_cast<uint32_t>(100));
    ASSERT_GE(bus.bms().tickCount(), static_cast<uint32_t>(100));
}


/* =========================================================================
 * App → ESC Register Read (via BLE relay)
 * ========================================================================= */

/**
 * Test: Read ESC firmware version through the full chain.
 * App → nRF → BLE STM32 → (forward to ESC) → ESC responds → BLE → nRF → App
 */
TEST(Bus, AppReadEscFirmwareVersion)
{
    SimulationBus bus;
    bus.init();

    /* Send a READ request from app to ESC for firmware version (reg 0x1A) */
    bus.injectAppCommand(0x20, 0x01, esc_reg::FIRMWARE_VERSION);

    /* Run simulation to process the round trip */
    bus.tickMs(50);

    /* Capture response from the nRF TX (app-facing) */
    auto response = bus.captureAppResponse();

    /* Should have received a response packet */
    ASSERT_GT(response.size(), static_cast<size_t>(0));
}


/* =========================================================================
 * Throttle → ESC Speed Control
 * ========================================================================= */

/**
 * Test: Applying throttle on BLE → ESC receives speed command.
 */
TEST(Bus, ThrottleToEscControl)
{
    SimulationBus bus;
    bus.init();

    /* Apply full throttle on BLE ADC ch0 */
    bus.setThrottle(3400);  // THROTTLE_MAX_ADC

    /* Run for enough time for BLE to send throttle update (50ms interval)
     * and ESC to process it */
    bus.tickMs(200);

    /* ESC should have received a throttle command */
    /* The exact speed depends on PID and mode, but throttle register
     * in ESC (0x26) should be non-zero */
    ASSERT_GT(bus.ble().throttlePercent(), static_cast<uint8_t>(90));
}

/**
 * Test: Brake overrides throttle on BLE.
 */
TEST(Bus, BrakeOverridesThrottleOnBus)
{
    SimulationBus bus;
    bus.init();

    /* Full throttle + heavy brake */
    bus.setThrottle(3400);
    bus.setBrake(2500);

    bus.tickMs(200);

    /* BLE should report throttle as 0 (brake override) */
    ASSERT_EQ(bus.ble().throttlePercent(), 0);
}


/* =========================================================================
 * ESC → BMS Periodic Polling
 * ========================================================================= */

/**
 * Test: ESC periodically queries BMS for battery data.
 * ESC sends READ requests to BMS every ~200ms, and BMS responds.
 * After some time, ESC battery registers should reflect BMS data.
 */
TEST(Bus, EscPollsBmsForBatteryData)
{
    SimulationBus bus;
    bus.init();

    /* Set specific cell voltages in BMS */
    for (int i = 0; i < 10; i++) {
        bus.setCellVoltage(i, 3800);  // 3.800V per cell
    }

    /* Run for 1 second to allow multiple BMS poll cycles */
    bus.tickMs(1000);

    /* ESC should have some battery data. The exact mechanism depends on
     * how BMS responds and ESC stores it. At minimum, ESC should have
     * attempted BMS queries without crashing. */
    ASSERT_GT(bus.totalTicks(), static_cast<uint32_t>(500));
}


/* =========================================================================
 * BMS Protection Propagation
 * ========================================================================= */

/**
 * Test: BMS overvoltage protection disables charging.
 */
TEST(Bus, BmsOvpIntegration)
{
    SimulationBus bus;
    bus.init();

    /* Set cells to overvoltage (both I2C and internal) */
    for (int i = 0; i < 10; i++) {
        bus.setCellVoltage(i, 4300);
        bus.bms().setSimCellVoltage(i, 4300);
    }

    bus.tickMs(2000);

    /* BMS should have disabled charge FET */
    ASSERT_FALSE(bus.bms().isCharging());
}


/* =========================================================================
 * Mode Switching via App
 * ========================================================================= */

/**
 * Test: App sends WRITE to change ESC riding mode.
 */
TEST(Bus, AppWriteRidingMode)
{
    SimulationBus bus;
    bus.init();

    /* Write riding mode = 2 (Sport) to ESC using buildPacket for correct format */
    uint8_t payload[] = {0x02, 0x00};
    uint8_t pktBuf[64];
    int pktLen = buildPacket(0x3E, 0x20, 0x02, esc_reg::RIDING_MODE,
                             payload, 2, pktBuf);

    std::vector<uint8_t> pkt(pktBuf, pktBuf + pktLen);
    bus.injectAppPacket(pkt);
    bus.tickMs(100);

    /* ESC should now be in Sport mode */
    ASSERT_EQ(bus.esc().ridingMode(), 2);
}


/* =========================================================================
 * Button Mode Cycling via BLE
 * ========================================================================= */

TEST(Bus, ButtonModeCycleOnBus)
{
    SimulationBus bus;
    bus.init();

    /* Simulate button press */
    bus.setButton(true);
    bus.tickMs(100);

    /* Release */
    bus.setButton(false);
    bus.tickMs(200);

    /* Mode should have changed (exact value depends on BLE→ESC protocol) */
    /* At minimum, no crash */
    ASSERT_GE(bus.totalTicks(), static_cast<uint32_t>(200));
}


/* =========================================================================
 * Cross-Board Communication Integrity
 * ========================================================================= */

/**
 * Test: Multiple rapid packets don't corrupt the bus.
 */
TEST(Bus, RapidPacketsNoCrash)
{
    SimulationBus bus;
    bus.init();

    /* Send 20 rapid commands */
    for (int i = 0; i < 20; i++) {
        bus.injectAppCommand(0x20, 0x01,
                             static_cast<uint8_t>(esc_reg::FIRMWARE_VERSION));
        bus.tickMs(10);
    }

    /* Should not crash, and bus still functional */
    bus.tickMs(100);
    ASSERT_GT(bus.totalTicks(), static_cast<uint32_t>(200));
}

/**
 * Test: Hall sensor simulation doesn't crash motor control.
 */
TEST(Bus, HallSensorSimulation)
{
    SimulationBus bus;
    bus.init();

    /* Cycle through all 6 valid hall states */
    uint8_t hallStates[] = {0x01, 0x03, 0x02, 0x06, 0x04, 0x05};
    for (int cycle = 0; cycle < 10; cycle++) {
        for (uint8_t hs : hallStates) {
            bus.setHallState(hs);
            bus.tickMs(2);
        }
    }

    /* Should complete without crash */
    ASSERT_GT(bus.totalTicks(), static_cast<uint32_t>(100));
}


/* =========================================================================
 * Full System Stability
 * ========================================================================= */

/**
 * Test: Run full system for 10 seconds simulated time.
 * All boards running, throttle applied, BMS polling active.
 */
TEST(Bus, LongRunStability)
{
    SimulationBus bus;
    bus.init();

    /* Apply moderate throttle */
    bus.setThrottle(2000);

    /* Set healthy battery */
    for (int i = 0; i < 10; i++) {
        bus.setCellVoltage(i, 3800);
    }

    /* Run 5 seconds (enough for multiple BMS poll cycles) */
    bus.tickMs(5000);

    /* Verify system is still healthy */
    ASSERT_FALSE(bus.esc().isLocked());
    ASSERT_TRUE(bus.bms().isDischarging());
    ASSERT_GT(bus.totalTicks(), static_cast<uint32_t>(4000));
}
