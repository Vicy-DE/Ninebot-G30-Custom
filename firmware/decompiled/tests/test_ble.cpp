/**
 * @file test_ble.cpp
 * @brief BLE dashboard firmware tests.
 *
 * Validates the decompiled BLE firmware behavior:
 *   - Register initialization
 *   - Throttle ADC → percentage conversion
 *   - Brake ADC → percentage conversion
 *   - Button debouncing and mode cycling
 *   - Dashboard LED output for each riding mode
 *   - Protocol relay between nRF and ESC
 *
 * All thresholds derived from BLE_1.1.7.bin analysis.
 */

#include "test_framework.h"
#include "sim_hal.h"
#include "ble_firmware.h"
#include "protocol.h"

using namespace ninebot;
using namespace ninebot::ble;
using namespace ninebot::sim;


/* =========================================================================
 * Helper: Create an initialized BLE firmware instance
 * ========================================================================= */

struct BleTestFixture {
    SimBleHardware hw;
    hal::BleHal    hal;
    BleFirmware*   fw = nullptr;

    BleTestFixture() {
        /* Default: button released (PB12 high = not pressed, active low) */
        hw.gpioB.setInputPin(12, true);
        /* Default: throttle and brake at idle */
        hw.adc.setChannel(0, 300);   // below THROTTLE_MIN_ADC
        hw.adc.setChannel(1, 400);   // below BRAKE_MIN_ADC

        hal = hw.toHal();
        fw  = new BleFirmware(hal);
        fw->init();
    }

    ~BleTestFixture() { delete fw; }

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

TEST(BLE, InitThrottleZero)
{
    BleTestFixture f;
    ASSERT_EQ(f.fw->throttlePercent(), 0);
}

TEST(BLE, InitBrakeZero)
{
    BleTestFixture f;
    ASSERT_EQ(f.fw->brakePercent(), 0);
}

TEST(BLE, InitDefaultModeIsD)
{
    BleTestFixture f;
    /* Default mode = D (1) */
    ASSERT_EQ(f.fw->registers().readU8(ble_reg::RIDING_MODE), 1);
}


/* =========================================================================
 * Throttle ADC Tests
 * ========================================================================= */

/**
 * Test: Throttle at idle (below min ADC) = 0%.
 */
TEST(BLE, ThrottleIdleZeroPercent)
{
    BleTestFixture f;
    f.hw.adc.setChannel(0, 300);  // below THROTTLE_MIN_ADC (400)
    f.tickMs(100);
    ASSERT_EQ(f.fw->throttlePercent(), 0);
}

/**
 * Test: Throttle at maximum ADC = 100%.
 */
TEST(BLE, ThrottleMaxFull)
{
    BleTestFixture f;
    f.hw.adc.setChannel(0, 3400);  // THROTTLE_MAX_ADC
    f.tickMs(100);
    ASSERT_EQ(f.fw->throttlePercent(), 100);
}

/**
 * Test: Throttle at midpoint ≈ 50%.
 * Mid = (400 + 3400) / 2 = 1900
 */
TEST(BLE, ThrottleMidpoint)
{
    BleTestFixture f;
    f.hw.adc.setChannel(0, 1900);
    f.tickMs(100);
    /* Should be approximately 50% */
    ASSERT_GE(f.fw->throttlePercent(), 45);
    ASSERT_LE(f.fw->throttlePercent(), 55);
}

/**
 * Test: Throttle within deadband region → 0%.
 * Deadband is 50 counts above THROTTLE_MIN_ADC (400).
 */
TEST(BLE, ThrottleDeadbandZero)
{
    BleTestFixture f;
    f.hw.adc.setChannel(0, 430);  // within deadband (400 + 50 = 450)
    f.tickMs(100);
    ASSERT_EQ(f.fw->throttlePercent(), 0);
}


/* =========================================================================
 * Brake ADC Tests
 * ========================================================================= */

TEST(BLE, BrakeReleasedZero)
{
    BleTestFixture f;
    f.hw.adc.setChannel(1, 400);  // below BRAKE_MIN_ADC (500)
    f.tickMs(100);
    ASSERT_EQ(f.fw->brakePercent(), 0);
}

TEST(BLE, BrakeFullMax)
{
    BleTestFixture f;
    f.hw.adc.setChannel(1, 3200);  // BRAKE_MAX_ADC
    f.tickMs(100);
    ASSERT_EQ(f.fw->brakePercent(), 100);
}

/**
 * Test: Brake overrides throttle.
 * When brake is applied, throttle output should be 0 regardless of position.
 */
TEST(BLE, BrakeOverridesThrottle)
{
    BleTestFixture f;
    f.hw.adc.setChannel(0, 3400);  // Full throttle
    f.hw.adc.setChannel(1, 2000);  // Significant brake
    f.tickMs(100);
    /* Throttle percent should be 0 when brake is applied */
    ASSERT_EQ(f.fw->throttlePercent(), 0);
}


/* =========================================================================
 * Button Tests
 * ========================================================================= */

/**
 * Test: Button not pressed => no mode change.
 */
TEST(BLE, ButtonNotPressedNoChange)
{
    BleTestFixture f;
    f.hw.gpioB.setInputPin(12, true);  // released (active low)
    f.tickMs(200);
    /* Mode should remain D (1) */
    ASSERT_EQ(f.fw->registers().readU8(ble_reg::RIDING_MODE), 1);
}

/**
 * Test: Short button press cycles mode D → Sport.
 */
TEST(BLE, ShortPressCyclesMode)
{
    BleTestFixture f;

    /* Press button (PB12 goes low = pressed) */
    f.hw.gpioB.setInputPin(12, false);
    f.tickMs(100);  // hold for 100ms (short press)

    /* Release */
    f.hw.gpioB.setInputPin(12, true);
    f.tickMs(100);

    /* Mode should have changed from D(1) to next mode */
    uint8_t mode = f.fw->registers().readU8(ble_reg::RIDING_MODE);
    ASSERT_NE(mode, 1);  // Should have changed
}


/* =========================================================================
 * Dashboard LED Tests
 * ========================================================================= */

TEST(BLE, PowerLedOnAfterInit)
{
    BleTestFixture f;
    f.tickMs(100);
    /* Power LED on PB0 should be set */
    ASSERT_TRUE(f.hw.gpioB.outputPin(LED_POWER));
}


/* =========================================================================
 * Protocol Relay Tests
 * ========================================================================= */

/**
 * Test: Packet addressed to ESC is forwarded from nRF to ESC UART.
 */
TEST(BLE, ForwardPacketToEsc)
{
    BleTestFixture f;

    /* Build a packet from App to ESC: READ reg 0x10 */
    uint8_t pkt[64];
    int len = buildPacket(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt);

    /* Feed to nRF-facing channel (USART1 — simulates app→nRF→STM32) */
    for (int i = 0; i < len; i++) {
        f.fw->usart1RxIsr(pkt[i]);
    }

    f.fw->mainLoopIteration();

    /* Drain the ESC channel TX */
    f.fw->channelEsc().drainTx();

    /* The ESC UART should have received the forwarded packet */
    const auto& txData = f.hw.uartEsc.txData();
    ASSERT_GT(txData.size(), static_cast<size_t>(0));
    ASSERT_EQ(txData[0], 0x5A);
    ASSERT_EQ(txData[1], 0xA5);
}

/**
 * Test: Packet addressed to BLE is handled locally (not forwarded).
 */
TEST(BLE, HandleLocalBlePacket)
{
    BleTestFixture f;

    /* Build a READ packet to BLE address (0x21) */
    uint8_t pkt[64];
    int len = buildPacket(0x3E, 0x21, 0x01, ble_reg::RIDING_MODE,
                          nullptr, 0, pkt);

    for (int i = 0; i < len; i++) {
        f.fw->usart1RxIsr(pkt[i]);
    }

    f.fw->mainLoopIteration();

    /* Should respond back to nRF, not forward to ESC */
    f.fw->channelNrf().drainTx();
    const auto& nrfTx = f.hw.uartNrf.txData();
    ASSERT_GT(nrfTx.size(), static_cast<size_t>(0));
}


/* =========================================================================
 * Watchdog Test
 * ========================================================================= */

TEST(BLE, WatchdogFed)
{
    BleTestFixture f;
    uint32_t before = f.hw.watchdog.feedCount();
    f.tickMs(10);
    ASSERT_GT(f.hw.watchdog.feedCount(), before);
}
