/**
 * @file test_nrf51822.cpp
 * @brief nRF51822 BLE co-processor firmware tests.
 *
 * Validates the decompiled nRF51822 firmware behavior against the
 * original BLE_1.1.7.bin Cortex-M0 disassembly findings:
 *
 *   - Initialization sequence (SoftDevice, BLE services, UART, WDT, PSM)
 *   - Ninebot protocol parsing (UART and BLE side, 5A A5 header)
 *   - Checksum calculation (cross-validated with STM32 implementation)
 *   - Protocol relay (UART ↔ BLE transparent bridge)
 *   - MiIO authentication state machine (full flow)
 *   - BLE connection / disconnection handling
 *   - BLE advertising with device name
 *   - PSM persistent token storage / retrieval
 *   - Watchdog feed during main loop
 *   - Binary equivalence: nRF51 checksum vs STM32 calculateChecksum
 *
 * All thresholds and behaviors derived from BLE_1.1.7.bin analysis
 * (282 functions, 58 SVC calls, 554 strings).
 *
 * @see nrf51822_analysis_BLE_1.1.7.txt
 * @see nrf51822-reprogramming.md
 */

#include "test_framework.h"
#include "sim_nrf51.h"
#include "nrf51_firmware.h"
#include "protocol.h"   /* STM32 calculateChecksum / buildPacket for cross-validation */

using namespace ninebot;
using namespace ninebot::nrf51;
using namespace ninebot::sim;


/* =========================================================================
 * Test Fixture
 * ========================================================================= */

struct Nrf51TestFixture {
    SimNrf51Hardware hw;
    Nrf51Hal         hal;
    Nrf51Firmware*   fw = nullptr;

    Nrf51TestFixture() {
        hal = hw.toHal();
        fw  = new Nrf51Firmware(hal);
        fw->init();
    }

    ~Nrf51TestFixture() { delete fw; }

    /** Run N main loop iterations. */
    void tick(uint32_t n = 1) {
        for (uint32_t i = 0; i < n; i++) {
            fw->mainLoopIteration();
        }
    }

    /** Simulate a BLE connection + enable notifications. */
    void connectBle() {
        hw.softdevice.injectConnect(0x0001);
        tick();
        /* Enable notifications by writing to NUS TX CCCD */
        uint8_t cccdVal[2] = {0x01, 0x00};
        /* We need to get the CCCD handle from the firmware — use known offset */
        fw->onGattsWrite(0, cccdVal, 2);  /* handle 0 will be ignored by NUS */
    }

    /** Build a Ninebot packet using the STM32 protocol.h builder. */
    static int buildNb(uint8_t src, uint8_t dst, uint8_t cmd, uint8_t arg,
                       const uint8_t* payload, uint8_t payloadLen,
                       uint8_t* out) {
        return static_cast<int>(
            ninebot::buildPacket(src, dst, cmd, arg, payload, payloadLen, out));
    }
};


/* =========================================================================
 * Initialization Tests
 * =========================================================================
 * Verify the init sequence matches reset handler @ 0x00018155.
 */

TEST(NRF51, InitSoftDeviceEnabled)
{
    Nrf51TestFixture f;
    ASSERT_TRUE(f.hw.softdevice.isEnabled());
}

TEST(NRF51, InitDcdcEnabled)
{
    Nrf51TestFixture f;
    ASSERT_EQ(f.hw.softdevice.dcdcMode(), 1);
}

TEST(NRF51, InitUartEnabled)
{
    Nrf51TestFixture f;
    ASSERT_TRUE(f.hw.uart.isEnabled());
}

TEST(NRF51, InitWatchdogStarted)
{
    Nrf51TestFixture f;
    ASSERT_TRUE(f.hw.wdt.isRunning());
    ASSERT_EQ(f.hw.wdt.timeout(), static_cast<uint32_t>(5000));
}

TEST(NRF51, InitAdvertisingStarted)
{
    Nrf51TestFixture f;
    ASSERT_TRUE(f.hw.softdevice.isAdvertising());
}

TEST(NRF51, InitMiioStateIdle)
{
    Nrf51TestFixture f;
    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()), static_cast<uint8_t>(MiioState::IDLE));
}

TEST(NRF51, InitBleNotConnected)
{
    Nrf51TestFixture f;
    ASSERT_FALSE(f.fw->isBleConnected());
    ASSERT_EQ(f.fw->bleConnHandle(), BLE_CONN_HANDLE_INVALID);
}

TEST(NRF51, InitPsmFlashWriteRegistered)
{
    Nrf51TestFixture f;
    ASSERT_TRUE(f.hw.psm.isFlashWriteRegistered());
}

TEST(NRF51, InitAdvDataContainsDeviceName)
{
    Nrf51TestFixture f;
    const auto& adv = f.hw.softdevice.advData();
    ASSERT_GT(adv.size(), static_cast<size_t>(2));
    /* AD type 0x09 = Complete Local Name */
    ASSERT_EQ(adv[1], 0x09);
    /* Check "NBScooter0001" is in advertising data */
    std::string name(adv.begin() + 2, adv.begin() + 2 + (adv[0] - 1));
    ASSERT_EQ(name, std::string("NBScooter0001"));
}

TEST(NRF51, InitGpioPinsConfigured)
{
    Nrf51TestFixture f;
    /* TX pin (P0.08) should be configured as output */
    uint32_t dir = f.hw.gpio.directionReg();
    ASSERT_TRUE((dir >> 8) & 1);  /* P0.08 = output */
}

TEST(NRF51, InitConnectionParametersSet)
{
    Nrf51TestFixture f;
    auto ppcp = f.hw.softdevice.ppcp();
    ASSERT_EQ(ppcp.min, static_cast<uint16_t>(16));       /* 20ms */
    ASSERT_EQ(ppcp.max, static_cast<uint16_t>(32));       /* 40ms */
    ASSERT_EQ(ppcp.latency, static_cast<uint16_t>(0));
    ASSERT_EQ(ppcp.timeout, static_cast<uint16_t>(400));   /* 4s */
}


/* =========================================================================
 * Checksum Tests
 * =========================================================================
 * Cross-validate nRF51822 checksum with STM32 calculateChecksum.
 * Both must produce identical results — same algorithm.
 */

TEST(NRF51, ChecksumMatchesStm32)
{
    /* Test vector: LEN=6, SRC=0x3E, DST=0x20, CMD=0x01, ARG=0x10 */
    uint8_t data[] = {0x06, 0x3E, 0x20, 0x01, 0x10};
    uint16_t nrf = Nrf51Firmware::calcChecksum(data, sizeof(data));
    uint16_t stm = ninebot::calculateChecksum(data, sizeof(data));
    ASSERT_EQ(nrf, stm);
}

TEST(NRF51, ChecksumMatchesStm32WithPayload)
{
    uint8_t data[] = {0x0A, 0x3E, 0x20, 0x64, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    uint16_t nrf = Nrf51Firmware::calcChecksum(data, sizeof(data));
    uint16_t stm = ninebot::calculateChecksum(data, sizeof(data));
    ASSERT_EQ(nrf, stm);
}

TEST(NRF51, ChecksumZeroData)
{
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
    uint16_t nrf = Nrf51Firmware::calcChecksum(data, sizeof(data));
    ASSERT_EQ(nrf, static_cast<uint16_t>(0xFFFF));
}

TEST(NRF51, ChecksumAllOnes)
{
    uint8_t data[] = {0xFF};
    uint16_t nrf = Nrf51Firmware::calcChecksum(data, 1);
    ASSERT_EQ(nrf, static_cast<uint16_t>(0xFF00));
}


/* =========================================================================
 * Protocol Parsing — UART Side (from STM32)
 * =========================================================================
 * Validates the 5A A5 header detection, length parsing, and checksum
 * verification. Packets from STM32 should be relayed to BLE TX queue.
 */

TEST(NRF51, ParseUartValidPacket)
{
    Nrf51TestFixture f;

    /* Build: App(0x3E) → ESC(0x20), CMD=0x01 READ, ARG=0x10 */
    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt);

    /* Feed bytes as UART ISR */
    for (int i = 0; i < len; i++) {
        f.fw->uart0RxIsr(pkt[i]);
    }

    /* Packet should be in BLE TX queue */
    const auto& bleQ = f.fw->bleTxQueue();
    ASSERT_EQ(bleQ.size(), static_cast<size_t>(1));
    /* Verify relayed packet starts with 5A A5 */
    ASSERT_EQ(bleQ[0][0], static_cast<uint8_t>(0x5A));
    ASSERT_EQ(bleQ[0][1], static_cast<uint8_t>(0xA5));
}

TEST(NRF51, ParseUartBadChecksum)
{
    Nrf51TestFixture f;

    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt);

    /* Corrupt the checksum */
    pkt[len - 1] ^= 0xFF;

    for (int i = 0; i < len; i++) {
        f.fw->uart0RxIsr(pkt[i]);
    }

    /* Bad checksum → packet should be dropped */
    ASSERT_EQ(f.fw->bleTxQueue().size(), static_cast<size_t>(0));
}

TEST(NRF51, ParseUartGarbageBeforePacket)
{
    Nrf51TestFixture f;

    /* Feed garbage */
    f.fw->uart0RxIsr(0xDE);
    f.fw->uart0RxIsr(0xAD);
    f.fw->uart0RxIsr(0xBE);
    f.fw->uart0RxIsr(0xEF);

    /* Then a valid packet */
    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt);
    for (int i = 0; i < len; i++) {
        f.fw->uart0RxIsr(pkt[i]);
    }

    ASSERT_EQ(f.fw->bleTxQueue().size(), static_cast<size_t>(1));
}

TEST(NRF51, ParseUartMultiplePackets)
{
    Nrf51TestFixture f;

    /* Send 3 packets back to back */
    for (int p = 0; p < 3; p++) {
        uint8_t pkt[64];
        int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01,
                                            static_cast<uint8_t>(0x10 + p),
                                            nullptr, 0, pkt);
        for (int i = 0; i < len; i++) {
            f.fw->uart0RxIsr(pkt[i]);
        }
    }

    ASSERT_EQ(f.fw->bleTxQueue().size(), static_cast<size_t>(3));
}

TEST(NRF51, ParseUartWithPayload)
{
    Nrf51TestFixture f;

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x64, 0x00,
                                        payload, 4, pkt);
    for (int i = 0; i < len; i++) {
        f.fw->uart0RxIsr(pkt[i]);
    }

    const auto& bleQ = f.fw->bleTxQueue();
    ASSERT_EQ(bleQ.size(), static_cast<size_t>(1));
    /* Check relayed packet length = payload + frame overhead */
    ASSERT_EQ(bleQ[0].size(), static_cast<size_t>(len));
}


/* =========================================================================
 * Protocol Parsing — BLE Side (from phone app)
 * =========================================================================
 * Packets from phone should be relayed to UART TX queue.
 */

TEST(NRF51, ParseBleValidPacket)
{
    Nrf51TestFixture f;

    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt);

    f.fw->feedBleBytes(pkt, static_cast<size_t>(len));

    const auto& uartQ = f.fw->uartTxQueue();
    ASSERT_EQ(uartQ.size(), static_cast<size_t>(1));
    ASSERT_EQ(uartQ[0][0], static_cast<uint8_t>(0x5A));
    ASSERT_EQ(uartQ[0][1], static_cast<uint8_t>(0xA5));
}

TEST(NRF51, ParseBleBadChecksum)
{
    Nrf51TestFixture f;

    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt);
    pkt[len - 2] ^= 0xFF;

    f.fw->feedBleBytes(pkt, static_cast<size_t>(len));

    ASSERT_EQ(f.fw->uartTxQueue().size(), static_cast<size_t>(0));
}


/* =========================================================================
 * Protocol Relay Tests
 * =========================================================================
 * The nRF51822 is a transparent bridge — packets from STM32 go to BLE,
 * packets from phone go to UART.
 */

TEST(NRF51, RelayUartToBlePreservesContent)
{
    Nrf51TestFixture f;

    /* Build a packet with payload */
    uint8_t payload[] = {0xCA, 0xFE, 0xBA, 0xBE};
    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x64, 0x00,
                                        payload, 4, pkt);

    for (int i = 0; i < len; i++) {
        f.fw->uart0RxIsr(pkt[i]);
    }

    const auto& bleQ = f.fw->bleTxQueue();
    ASSERT_EQ(bleQ.size(), static_cast<size_t>(1));

    /* Validate the relayed packet using STM32 validatePacket */
    ASSERT_TRUE(ninebot::validatePacket(bleQ[0].data(), bleQ[0].size()));

    /* Verify src/dst/cmd/arg preserved */
    ASSERT_EQ(bleQ[0][3], static_cast<uint8_t>(0x3E));  /* SRC */
    ASSERT_EQ(bleQ[0][4], static_cast<uint8_t>(0x20));  /* DST */
    ASSERT_EQ(bleQ[0][5], static_cast<uint8_t>(0x64));  /* CMD */
    ASSERT_EQ(bleQ[0][6], static_cast<uint8_t>(0x00));  /* ARG */
}

TEST(NRF51, RelayBleToUartPreservesContent)
{
    Nrf51TestFixture f;

    uint8_t payload[] = {0xDE, 0xAD};
    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x22, 0x01, 0x30,
                                        payload, 2, pkt);

    f.fw->feedBleBytes(pkt, static_cast<size_t>(len));

    const auto& uartQ = f.fw->uartTxQueue();
    ASSERT_EQ(uartQ.size(), static_cast<size_t>(1));

    ASSERT_TRUE(ninebot::validatePacket(uartQ[0].data(), uartQ[0].size()));
    ASSERT_EQ(uartQ[0][3], static_cast<uint8_t>(0x3E));
    ASSERT_EQ(uartQ[0][4], static_cast<uint8_t>(0x22));
    ASSERT_EQ(uartQ[0][5], static_cast<uint8_t>(0x01));
    ASSERT_EQ(uartQ[0][6], static_cast<uint8_t>(0x30));
}

TEST(NRF51, RelayUartDrainSendsToHardware)
{
    Nrf51TestFixture f;

    /* Phone→UART: feed BLE bytes → drain UART TX → check hw.uart.txData */
    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt);
    f.fw->feedBleBytes(pkt, static_cast<size_t>(len));

    /* Drain sends to hardware UART */
    size_t drained = f.fw->drainUartTx();
    ASSERT_GT(drained, static_cast<size_t>(0));

    /* Verify UART hardware received the bytes */
    const auto& txData = f.hw.uart.txData();
    ASSERT_GT(txData.size(), static_cast<size_t>(0));
    ASSERT_EQ(txData[0], static_cast<uint8_t>(0x5A));
    ASSERT_EQ(txData[1], static_cast<uint8_t>(0xA5));

    /* And validatePacket should pass */
    ASSERT_TRUE(ninebot::validatePacket(txData.data(), txData.size()));
}

TEST(NRF51, RelayBytesCountTracking)
{
    Nrf51TestFixture f;

    ASSERT_EQ(f.fw->uartToBleBytes(), static_cast<uint32_t>(0));
    ASSERT_EQ(f.fw->bleToUartBytes(), static_cast<uint32_t>(0));

    /* UART→BLE direction */
    uint8_t pkt1[64];
    int len1 = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt1);
    for (int i = 0; i < len1; i++) {
        f.fw->uart0RxIsr(pkt1[i]);
    }
    ASSERT_GT(f.fw->uartToBleBytes(), static_cast<uint32_t>(0));

    /* BLE→UART direction */
    uint8_t pkt2[64];
    int len2 = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt2);
    f.fw->feedBleBytes(pkt2, static_cast<size_t>(len2));
    ASSERT_GT(f.fw->bleToUartBytes(), static_cast<uint32_t>(0));
}


/* =========================================================================
 * BLE Connection Tests
 * ========================================================================= */

TEST(NRF51, BleConnectSetsState)
{
    Nrf51TestFixture f;

    ASSERT_FALSE(f.fw->isBleConnected());

    f.hw.softdevice.injectConnect(0x0042);
    f.tick();

    ASSERT_TRUE(f.fw->isBleConnected());
    ASSERT_EQ(f.fw->bleConnHandle(), static_cast<uint16_t>(0x0042));
}


TEST(NRF51, BleConnectStartsMiioAuth)
{
    Nrf51TestFixture f;

    f.hw.softdevice.injectConnect(0x0001);
    f.tick();

    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::WAIT_AUTH));
}

TEST(NRF51, BleDisconnectResetsState)
{
    Nrf51TestFixture f;

    f.hw.softdevice.injectConnect(0x0001);
    f.tick();
    ASSERT_TRUE(f.fw->isBleConnected());

    f.hw.softdevice.injectDisconnect();
    f.tick();

    ASSERT_FALSE(f.fw->isBleConnected());
    ASSERT_EQ(f.fw->bleConnHandle(), BLE_CONN_HANDLE_INVALID);
}

TEST(NRF51, BleDisconnectRestartsAdvertising)
{
    Nrf51TestFixture f;

    f.hw.softdevice.injectConnect(0x0001);
    f.tick();
    ASSERT_FALSE(f.hw.softdevice.isAdvertising());  /* stopped on connect */

    f.hw.softdevice.injectDisconnect();
    f.tick();

    ASSERT_TRUE(f.hw.softdevice.isAdvertising());
}

TEST(NRF51, BleDisconnectResetsMiio)
{
    Nrf51TestFixture f;

    f.hw.softdevice.injectConnect(0x0001);
    f.tick();
    ASSERT_NE(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::IDLE));

    f.hw.softdevice.injectDisconnect();
    f.tick();

    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::IDLE));
}


/* =========================================================================
 * BLE TX Queue Tests (notification sending)
 * ========================================================================= */

TEST(NRF51, BleTxDrainNotConnectedClearsQueue)
{
    Nrf51TestFixture f;

    /* Not connected — queue packets from UART */
    uint8_t pkt[64];
    int len = Nrf51TestFixture::buildNb(0x3E, 0x20, 0x01, 0x10, nullptr, 0, pkt);
    for (int i = 0; i < len; i++) {
        f.fw->uart0RxIsr(pkt[i]);
    }
    ASSERT_EQ(f.fw->bleTxQueue().size(), static_cast<size_t>(1));

    /* Drain while not connected → queue cleared, no notifications sent */
    f.fw->drainBleTx();
    ASSERT_EQ(f.fw->bleTxQueue().size(), static_cast<size_t>(0));
    ASSERT_EQ(f.hw.softdevice.notificationsSent(), static_cast<uint32_t>(0));
}


/* =========================================================================
 * MiIO Authentication State Machine Tests
 * =========================================================================
 * Tests the complete auth flow:
 *   IDLE → WAIT_AUTH → WAIT_TOKEN → (login confirm) →
 *   (cloud bind) → (app bond) → WAIT_SN → SN_ARRIVED →
 *   REGISTERED → FLASH_REGISTERED
 */

TEST(NRF51, MiioFullAuthFlow)
{
    Nrf51TestFixture f;

    /* Connect */
    f.hw.softdevice.injectConnect(0x0001);
    f.tick();
    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::WAIT_AUTH));

    /* Auth write (4+ bytes required) */
    uint8_t authData[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    f.fw->onGattsWrite(0xFFFF, authData, 8);  /* non-NUS handle → MiIO path */
    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::WAIT_TOKEN));

    /* Token write (16 bytes required) */
    uint8_t token[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
                         0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                         0x77, 0x88, 0x99, 0x00};
    f.fw->onGattsWrite(0xFFFF, token, 16);

    /* After token: login confirm → cloud bind → app bond → WAIT_SN */
    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::WAIT_SN));
    ASSERT_TRUE(f.fw->miioReg().cloudBound);
    ASSERT_TRUE(f.fw->miioReg().appBonded);

    /* SN write */
    uint8_t sn[] = "N3MAA14T0001234";
    f.fw->onGattsWrite(0xFFFF, sn, 15);

    /* After SN: register → flash register → FLASH_REGISTERED */
    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::FLASH_REGISTERED));
    ASSERT_TRUE(f.fw->isMiioFlashRegistered());
}

TEST(NRF51, MiioAuthTooShortFails)
{
    Nrf51TestFixture f;

    f.hw.softdevice.injectConnect(0x0001);
    f.tick();

    /* Auth data too short (< 4 bytes) → ERROR */
    uint8_t authData[2] = {0x01, 0x02};
    f.fw->onGattsWrite(0xFFFF, authData, 2);
    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::ERROR));
}

TEST(NRF51, MiioTokenTooShortFails)
{
    Nrf51TestFixture f;

    f.hw.softdevice.injectConnect(0x0001);
    f.tick();

    /* Valid auth */
    uint8_t authData[4] = {0x01, 0x02, 0x03, 0x04};
    f.fw->onGattsWrite(0xFFFF, authData, 4);
    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::WAIT_TOKEN));

    /* Token too short (< 16 bytes) → ERROR */
    uint8_t token[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
    f.fw->onGattsWrite(0xFFFF, token, 8);
    ASSERT_EQ(static_cast<uint8_t>(f.fw->miioState()),
              static_cast<uint8_t>(MiioState::ERROR));
}

TEST(NRF51, MiioTokenStoredAfterRegistration)
{
    Nrf51TestFixture f;

    f.hw.softdevice.injectConnect(0x0001);
    f.tick();

    /* Complete auth flow */
    uint8_t auth[4] = {0x01, 0x02, 0x03, 0x04};
    f.fw->onGattsWrite(0xFFFF, auth, 4);

    uint8_t token[16];
    for (int i = 0; i < 16; i++) token[i] = static_cast<uint8_t>(0x10 + i);
    f.fw->onGattsWrite(0xFFFF, token, 16);

    uint8_t sn[] = "TestSN00001";
    f.fw->onGattsWrite(0xFFFF, sn, 11);

    /* Token should be stored in PSM block 0x0001 */
    ASSERT_TRUE(f.hw.psm.hasBlock(0x0001));
    const auto& block = f.hw.psm.getBlock(0x0001);
    ASSERT_EQ(block.size(), static_cast<size_t>(16));
    ASSERT_EQ(block[0], static_cast<uint8_t>(0x10));
    ASSERT_EQ(block[15], static_cast<uint8_t>(0x1F));
}


/* =========================================================================
 * PSM Persistent Storage Tests
 * ========================================================================= */

TEST(NRF51, PsmLoadTokenOnInit)
{
    SimNrf51Hardware hw2;
    /* Pre-load a token before init */
    std::vector<uint8_t> preToken(16, 0x42);
    hw2.psm.preload(0x0001, preToken);

    Nrf51Hal hal2 = hw2.toHal();
    Nrf51Firmware fw2(hal2);
    fw2.init();

    /* Token should have been loaded */
    ASSERT_TRUE(fw2.storedToken().valid);
    ASSERT_EQ(fw2.storedToken().data[0], static_cast<uint8_t>(0x42));
}

TEST(NRF51, PsmNoTokenEmptyInit)
{
    Nrf51TestFixture f;
    /* No pre-loaded token → storedToken should be invalid */
    ASSERT_FALSE(f.fw->storedToken().valid);
}


/* =========================================================================
 * Watchdog Tests
 * ========================================================================= */

TEST(NRF51, WatchdogFedInMainLoop)
{
    Nrf51TestFixture f;

    uint32_t before = f.hw.wdt.feedCount();
    f.tick(5);
    ASSERT_GE(f.hw.wdt.feedCount(), before + 5);
}


/* =========================================================================
 * Binary Equivalence Tests
 * =========================================================================
 * Feed identical data through both nRF51822 and STM32 protocol paths
 * and verify identical results.
 */

TEST(NRF51, BinaryEquivalencePacketBuild)
{
    /* Build same packet using both STM32 buildPacket and nRF51 enqueue */
    Nrf51TestFixture f;

    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t stmPkt[64];
    int stmLen = static_cast<int>(
        ninebot::buildPacket(0x20, 0x3E, 0x01, 0x10,
                             payload, 3, stmPkt));

    /* Feed same packet from UART → relayed to BLE queue */
    for (int i = 0; i < stmLen; i++) {
        f.fw->uart0RxIsr(stmPkt[i]);
    }

    const auto& bleQ = f.fw->bleTxQueue();
    ASSERT_EQ(bleQ.size(), static_cast<size_t>(1));

    /* nRF51 rebuilt packet should match STM32 packet */
    const auto& nrfPkt = bleQ[0];
    ASSERT_EQ(nrfPkt.size(), static_cast<size_t>(stmLen));
    for (int i = 0; i < stmLen; i++) {
        ASSERT_EQ(nrfPkt[i], stmPkt[i]);
    }
}

TEST(NRF51, BinaryEquivalenceChecksumSweep)
{
    /* Test checksum equivalence for various payload sizes */
    for (uint8_t payloadSize = 0; payloadSize < 32; payloadSize++) {
        std::vector<uint8_t> data(payloadSize + 5);
        data[0] = payloadSize + 6;  /* LEN */
        data[1] = 0x3E;             /* SRC */
        data[2] = 0x20;             /* DST */
        data[3] = 0x01;             /* CMD */
        data[4] = payloadSize;      /* ARG */
        for (uint8_t i = 0; i < payloadSize; i++) {
            data[5 + i] = i;
        }

        uint16_t nrf = Nrf51Firmware::calcChecksum(data.data(), data.size());
        uint16_t stm = ninebot::calculateChecksum(data.data(), data.size());
        ASSERT_EQ(nrf, stm);
    }
}

TEST(NRF51, BinaryEquivalenceRoundTrip)
{
    /* Full round-trip: STM32 build → nRF51 parse → nRF51 rebuild → validate */
    Nrf51TestFixture f;

    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    uint8_t original[64];
    int origLen = static_cast<int>(
        ninebot::buildPacket(0x20, 0x3E, 0x64, 0x00,
                             payload, 6, original));

    /* UART→BLE relay */
    for (int i = 0; i < origLen; i++) {
        f.fw->uart0RxIsr(original[i]);
    }

    const auto& bleQ = f.fw->bleTxQueue();
    ASSERT_EQ(bleQ.size(), static_cast<size_t>(1));

    /* Validate the rebuilt packet */
    ASSERT_TRUE(ninebot::validatePacket(bleQ[0].data(), bleQ[0].size()));

    /* Parse back: feed the rebuilt packet into a fresh nRF51 BLE parser */
    Nrf51TestFixture f2;
    f2.fw->feedBleBytes(bleQ[0].data(), bleQ[0].size());

    /* Should produce a packet in UART TX queue */
    const auto& uartQ = f2.fw->uartTxQueue();
    ASSERT_EQ(uartQ.size(), static_cast<size_t>(1));

    /* Final packet should be identical to original */
    ASSERT_EQ(uartQ[0].size(), static_cast<size_t>(origLen));
    for (int i = 0; i < origLen; i++) {
        ASSERT_EQ(uartQ[0][i], original[i]);
    }
}


/* =========================================================================
 * Simulator Hardware Tests
 * =========================================================================
 * Verify SimNrf51Hardware components work correctly.
 */

TEST(NRF51, SimUartWiring)
{
    SimNrfUart a, b;
    a.wireTo(&b);

    a.sendByte(0x42);
    ASSERT_TRUE(b.isRxReady());
    ASSERT_EQ(b.receiveByte(), static_cast<uint8_t>(0x42));
}

TEST(NRF51, SimGpioSetClear)
{
    SimNrfGpio gpio;
    gpio.configureOutput(5);
    gpio.setPin(5);
    ASSERT_TRUE(gpio.outputPin(5));
    gpio.clearPin(5);
    ASSERT_FALSE(gpio.outputPin(5));
}

TEST(NRF51, SimTimerStartStop)
{
    SimNrfTimer timer;
    timer.start();
    timer.tick();
    timer.tick();
    ASSERT_EQ(timer.getCounter(), static_cast<uint32_t>(2));
    timer.stop();
    timer.tick();  /* Should not increment when stopped */
    ASSERT_EQ(timer.getCounter(), static_cast<uint32_t>(2));
}

TEST(NRF51, SimSoftDeviceEventInjection)
{
    SimSoftDevice sd;
    sd.bleEnable();
    ASSERT_TRUE(sd.isEnabled());

    sd.injectConnect(0x0001);
    ASSERT_TRUE(sd.isConnected());

    BleEventType type;
    uint16_t handle;
    ASSERT_TRUE(sd.getEvent(type, handle));
    ASSERT_EQ(static_cast<uint16_t>(type),
              static_cast<uint16_t>(BleEventType::CONNECTED));
    ASSERT_EQ(handle, static_cast<uint16_t>(0x0001));
}

TEST(NRF51, SimPsmStoreLoad)
{
    SimPsm psm;
    uint8_t data[4] = {0xCA, 0xFE, 0xBA, 0xBE};
    psm.store(0x0001, data, 4);

    uint8_t out[4] = {};
    uint16_t actualLen = 0;
    ASSERT_TRUE(psm.load(0x0001, out, 4, actualLen));
    ASSERT_EQ(actualLen, static_cast<uint16_t>(4));
    ASSERT_EQ(out[0], static_cast<uint8_t>(0xCA));
    ASSERT_EQ(out[3], static_cast<uint8_t>(0xBE));
}

TEST(NRF51, SimPsmLoadNonexistent)
{
    SimPsm psm;
    uint8_t out[4] = {};
    uint16_t actualLen = 0;
    ASSERT_FALSE(psm.load(0x9999, out, 4, actualLen));
    ASSERT_EQ(actualLen, static_cast<uint16_t>(0));
}

