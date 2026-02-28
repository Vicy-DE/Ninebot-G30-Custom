/**
 * @file test_main.cpp
 * @brief Comprehensive test suite for the Ninebot G30 Max protocol library.
 *
 * Tests every feature of the reconstructed protocol implementation:
 *   1.  Checksum calculation (known vectors, edge cases)
 *   2.  Packet building (header, fields, payload, checksum)
 *   3.  Packet validation (valid, corrupted, truncated)
 *   4.  Parser state machine (header detection, byte accumulation, reset)
 *   5.  Parser edge cases (overflow, double-header, interleaved garbage)
 *   6.  TX queue (enqueue, circular buffer, queue-full, drain)
 *   7.  Register file (read/write, all widths)
 *   8.  Device simulation (ESC defaults, BMS defaults, BLE defaults)
 *   9.  Register read via protocol (send READ, receive RESPONSE)
 *   10. Register write via protocol (send WRITE, receive ACK)
 *   11. Multi-board bus simulation (ESC <-> BLE <-> BMS routing)
 *   12. Full round-trip: App -> BLE -> ESC -> BMS and back
 *   13. Error injection and error code handling
 *   14. BMS cell voltage monitoring
 *   15. Speed / riding mode changes
 *   16. Stress test: rapid packet bursts
 *
 * Self-contained — no external test framework required.
 * Prints colored PASS/FAIL for each test.
 *
 * Build:
 *   g++ -std=c++17 -I include -o test_main test/test_main.cpp
 *   ./test_main
 */

#include "ninebot/protocol.h"
#include "ninebot/uart.h"
#include "ninebot/registers.h"
#include "ninebot/simulation.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <cassert>


/* ===========================================================================
 * Minimal Test Framework
 * =========================================================================== */

static int g_testsPassed = 0;
static int g_testsFailed = 0;
static int g_totalAsserts = 0;

/** ANSI color codes for terminal output. */
#define CLR_GREEN  "\033[32m"
#define CLR_RED    "\033[31m"
#define CLR_YELLOW "\033[33m"
#define CLR_CYAN   "\033[36m"
#define CLR_RESET  "\033[0m"
#define CLR_BOLD   "\033[1m"

/**
 * Assert a condition; print PASS or FAIL with context.
 */
#define TEST_ASSERT(cond, msg) do { \
    g_totalAsserts++; \
    if (!(cond)) { \
        std::printf("    " CLR_RED "FAIL" CLR_RESET " %s (line %d): %s\n", \
                    msg, __LINE__, #cond); \
        g_testsFailed++; \
        return; \
    } \
} while(0)

/**
 * Assert equality for integer types.
 */
#define TEST_EQ(actual, expected, msg) do { \
    g_totalAsserts++; \
    auto _a = (actual); auto _e = (expected); \
    if (_a != _e) { \
        std::printf("    " CLR_RED "FAIL" CLR_RESET " %s (line %d): " \
                    "expected %lld, got %lld\n", \
                    msg, __LINE__, (long long)_e, (long long)_a); \
        g_testsFailed++; \
        return; \
    } \
} while(0)

/**
 * Run a named test function.
 */
#define RUN_TEST(func) do { \
    int before = g_testsFailed; \
    std::printf("  " CLR_CYAN "%-55s" CLR_RESET, #func); \
    func(); \
    if (g_testsFailed == before) { \
        std::printf(CLR_GREEN " PASS" CLR_RESET "\n"); \
        g_testsPassed++; \
    } else { \
        std::printf("\n"); \
    } \
} while(0)

/**
 * Print a test group header.
 */
#define TEST_GROUP(name) \
    std::printf("\n" CLR_BOLD CLR_YELLOW "=== %s ===" CLR_RESET "\n", name)


using namespace ninebot;


/* ===========================================================================
 * 1. CHECKSUM TESTS
 * ===========================================================================
 *
 * Verifies calculateChecksum() against known values from the firmware.
 * The checksum is: ~(sum of bytes) & 0xFFFF
 */

void test_checksum_empty() {
    /* Empty data: sum = 0, ~0 = 0xFFFF */
    uint16_t chk = calculateChecksum(nullptr, 0);
    TEST_EQ(chk, 0xFFFF, "empty checksum should be 0xFFFF");
}

void test_checksum_single_byte() {
    /* Single byte 0x42: sum = 0x42, ~0x42 = 0xFFBD */
    uint8_t data[] = {0x42};
    uint16_t chk = calculateChecksum(data, 1);
    TEST_EQ(chk, static_cast<uint16_t>(~0x42), "single byte 0x42");
}

void test_checksum_known_packet() {
    /*
     * Known good packet from protocol.md example:
     * Read ESC serial (reg 0x10, read 14 bytes):
     *   LEN=0x06, SRC=0x3E, DST=0x20, CMD=0x01, ARG=0x10, PAYLOAD=0x0E 0x00
     * Sum = 0x06+0x3E+0x20+0x01+0x10+0x0E+0x00 = 0x83
     * Checksum = ~0x83 & 0xFFFF = 0xFF7C
     */
    uint8_t data[] = {0x06, 0x3E, 0x20, 0x01, 0x10, 0x0E, 0x00};
    uint16_t chk = calculateChecksum(data, sizeof(data));
    TEST_EQ(chk, 0xFF7C, "known packet checksum");
}

void test_checksum_all_ff() {
    /* 4 bytes of 0xFF: sum = 4*255 = 1020 = 0x03FC, ~0x03FC = 0xFC03 */
    uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint16_t chk = calculateChecksum(data, 4);
    uint16_t expected = static_cast<uint16_t>(~(uint16_t)(4 * 0xFF));
    TEST_EQ(chk, expected, "all-0xFF checksum");
}

void test_checksum_overflow_16bit() {
    /* Enough bytes to overflow 16-bit sum: 256 bytes of 0xFF
     * sum = 256*255 = 65280 = 0xFF00, ~0xFF00 = 0x00FF */
    uint8_t data[256];
    std::memset(data, 0xFF, 256);
    uint16_t chk = calculateChecksum(data, 256);
    uint16_t expected = static_cast<uint16_t>(~(uint16_t)(256 * 255));
    TEST_EQ(chk, expected, "16-bit overflow checksum");
}


/* ===========================================================================
 * 2. PACKET BUILDING TESTS
 * ===========================================================================
 */

void test_build_minimal_packet() {
    /* Build a packet with 0-length payload */
    uint8_t buffer[32];
    size_t len = buildPacket(0x20, 0x21, 0x01, 0x26, nullptr, 0, buffer);

    TEST_EQ(len, 9u, "minimal packet size (no payload)");
    TEST_EQ(buffer[0], 0x5A, "header byte 1");
    TEST_EQ(buffer[1], 0xA5, "header byte 2");
    TEST_EQ(buffer[2], 0, "LEN = 0 (no payload)");
    TEST_EQ(buffer[3], 0x20, "source = ESC");
    TEST_EQ(buffer[4], 0x21, "destination = BLE");
    TEST_EQ(buffer[5], 0x01, "command = READ");
    TEST_EQ(buffer[6], 0x26, "argument = 0x26");

    /* Verify checksum */
    TEST_ASSERT(validatePacket(buffer, len), "checksum valid");
}

void test_build_packet_with_payload() {
    /* Build a packet with 2-byte payload */
    uint8_t payload[] = {0xE8, 0x03};
    uint8_t buffer[32];
    size_t len = buildPacket(0x21, 0x20, 0x01, 0x26, payload, 2, buffer);

    TEST_EQ(len, 11u, "packet with 2-byte payload");
    TEST_EQ(buffer[2], 2, "LEN = 2 (payload only)");
    TEST_EQ(buffer[7], 0xE8, "payload[0]");
    TEST_EQ(buffer[8], 0x03, "payload[1]");
    TEST_ASSERT(validatePacket(buffer, len), "checksum valid");
}

void test_build_packet_max_payload() {
    /* Build a packet with maximum payload (242 bytes) */
    uint8_t payload[MAX_PAYLOAD_LENGTH];
    std::memset(payload, 0xAB, MAX_PAYLOAD_LENGTH);
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t len = buildPacket(0x20, 0x22, 0x02, 0x10, payload, MAX_PAYLOAD_LENGTH, buffer);

    TEST_EQ(len, 251u, "max payload packet size");
    TEST_EQ(buffer[2], MAX_PAYLOAD_LENGTH, "LEN field = payload length");
    TEST_ASSERT(validatePacket(buffer, len), "checksum valid");
}

void test_build_all_device_addresses() {
    /* Verify packet building works for all device address pairs */
    DeviceAddress addrs[] = {
        DeviceAddress::ESC, DeviceAddress::BLE, DeviceAddress::BMS,
        DeviceAddress::APP, DeviceAddress::PC
    };
    uint8_t buffer[32];

    for (auto src : addrs) {
        for (auto dst : addrs) {
            size_t len = buildPacket(
                static_cast<uint8_t>(src), static_cast<uint8_t>(dst),
                0x01, 0x10, nullptr, 0, buffer
            );
            TEST_ASSERT(validatePacket(buffer, len), "all address pairs valid");
        }
    }
}


/* ===========================================================================
 * 3. PACKET VALIDATION TESTS
 * ===========================================================================
 */

void test_validate_good_packet() {
    uint8_t buffer[32];
    size_t len = buildPacket(0x20, 0x21, 0x01, 0x26, nullptr, 0, buffer);
    TEST_ASSERT(validatePacket(buffer, len), "good packet validates");
}

void test_validate_corrupted_checksum() {
    uint8_t buffer[32];
    size_t len = buildPacket(0x20, 0x21, 0x01, 0x26, nullptr, 0, buffer);
    buffer[len - 1] ^= 0xFF;  /* Corrupt checksum high byte */
    TEST_ASSERT(!validatePacket(buffer, len), "corrupted checksum rejects");
}

void test_validate_corrupted_data() {
    uint8_t buffer[32];
    size_t len = buildPacket(0x20, 0x21, 0x01, 0x26, nullptr, 0, buffer);
    buffer[4] ^= 0x01;  /* Flip one data bit */
    TEST_ASSERT(!validatePacket(buffer, len), "corrupted data rejects");
}

void test_validate_wrong_header() {
    uint8_t buffer[32];
    size_t len = buildPacket(0x20, 0x21, 0x01, 0x26, nullptr, 0, buffer);
    buffer[0] = 0xAA;  /* Wrong header */
    TEST_ASSERT(!validatePacket(buffer, len), "wrong header rejects");
}

void test_validate_too_short() {
    uint8_t buffer[] = {0x5A, 0xA5, 0x04};
    TEST_ASSERT(!validatePacket(buffer, 3), "too-short packet rejects");
}


/* ===========================================================================
 * 4. PARSER STATE MACHINE TESTS
 * ===========================================================================
 */

void test_parser_receives_valid_packet() {
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    bool received = false;
    Packet rxPkt{};

    ch.setPacketCallback([&](const Packet& pkt) {
        received = true;
        rxPkt = pkt;
    });

    /* Build a test packet */
    uint8_t payload[] = {0x02};
    uint8_t buffer[32];
    size_t len = buildPacket(0x3E, 0x20, 0x01, 0x26, payload, 1, buffer);

    /* Feed all bytes through the parser */
    for (size_t i = 0; i < len; ++i) {
        ch.receiveByte(buffer[i]);
    }

    TEST_ASSERT(received, "callback was invoked");
    TEST_EQ(rxPkt.source, 0x3E, "source = APP");
    TEST_EQ(rxPkt.destination, 0x20, "destination = ESC");
    TEST_EQ(rxPkt.command, 0x01, "command = READ");
    TEST_EQ(rxPkt.argument, 0x26, "argument = speed register");
    TEST_EQ(rxPkt.payloadLength, 1, "payload length = 1");
    TEST_EQ(rxPkt.payload[0], 0x02, "payload[0] = 2");
}

void test_parser_rejects_bad_checksum() {
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    bool received = false;

    ch.setPacketCallback([&](const Packet&) { received = true; });

    uint8_t buffer[32];
    size_t len = buildPacket(0x3E, 0x20, 0x01, 0x26, nullptr, 0, buffer);
    buffer[len - 1] ^= 0xFF;  /* Corrupt checksum */

    for (size_t i = 0; i < len; ++i) {
        ch.receiveByte(buffer[i]);
    }

    TEST_ASSERT(!received, "bad checksum should not dispatch");
}

void test_parser_resets_on_garbage() {
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    bool received = false;
    ch.setPacketCallback([&](const Packet&) { received = true; });

    /* Send random garbage first */
    uint8_t garbage[] = {0x12, 0x34, 0x56, 0x78, 0x9A};
    for (auto b : garbage) ch.receiveByte(b);

    /* Then send a valid packet — parser should still work */
    uint8_t buffer[32];
    size_t len = buildPacket(0x20, 0x21, 0x01, 0x10, nullptr, 0, buffer);
    for (size_t i = 0; i < len; ++i) ch.receiveByte(buffer[i]);

    TEST_ASSERT(received, "valid packet after garbage");
}

void test_parser_handles_consecutive_packets() {
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    int count = 0;
    ch.setPacketCallback([&](const Packet&) { count++; });

    /* Send 5 consecutive packets */
    for (int n = 0; n < 5; ++n) {
        uint8_t payload[] = { static_cast<uint8_t>(n) };
        uint8_t buffer[32];
        size_t len = buildPacket(0x20, 0x21, 0x01, 0x10, payload, 1, buffer);
        for (size_t i = 0; i < len; ++i) ch.receiveByte(buffer[i]);
    }

    TEST_EQ(count, 5, "received 5 consecutive packets");
}


/* ===========================================================================
 * 5. PARSER EDGE CASES
 * ===========================================================================
 */

void test_parser_double_header() {
    /*
     * Firmware behavior at 0x0800719A: when gotFirstHeader is already set
     * and another 0x5A arrives, it falls through to the "not 0xA5" check
     * and RESETS all state (gotFirstHeader=0).  A third 0x5A then sets
     * gotFirstHeader=1 again, and the following 0xA5 initiates reception.
     *
     * So 0x5A 0x5A resets, then 0x5A 0xA5 starts receiving:
     *   byte 0x5A → gotFirstHeader = 1
     *   byte 0x5A → reset (gotFirstHeader = 0)
     *   byte 0x5A → gotFirstHeader = 1 (again)
     *   byte 0xA5 → transition to RECEIVING
     */
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    int count = 0;
    ch.setPacketCallback([&](const Packet&) { count++; });

    ch.receiveByte(0x5A);  // Set gotFirstHeader
    ch.receiveByte(0x5A);  // RESET (firmware: falls through to 'not 0xA5' → reset)
    ch.receiveByte(0x5A);  // Set gotFirstHeader again
    ch.receiveByte(0xA5);  // Transition to RECEIVING

    /* Feed a valid packet body */
    // LEN=0 (no payload), SRC=0x20, DST=0x21, CMD=0x01, ARG=0x10
    uint8_t body[] = {0x00, 0x20, 0x21, 0x01, 0x10};
    uint16_t chk = calculateChecksum(body, 5);
    for (auto b : body) ch.receiveByte(b);
    ch.receiveByte(chk & 0xFF);
    ch.receiveByte((chk >> 8) & 0xFF);

    TEST_EQ(count, 1, "re-sync after 0x5A 0x5A 0x5A 0xA5");
}

void test_parser_interleaved_header() {
    /* 0x5A then some non-0xA5 byte -> should reset */
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    int count = 0;
    ch.setPacketCallback([&](const Packet&) { count++; });

    ch.receiveByte(0x5A);
    ch.receiveByte(0x42);  // Not 0xA5 -> reset

    /* Now send a proper packet */
    uint8_t buffer[32];
    size_t len = buildPacket(0x20, 0x21, 0x01, 0x10, nullptr, 0, buffer);
    for (size_t i = 0; i < len; ++i) ch.receiveByte(buffer[i]);

    TEST_EQ(count, 1, "recovery after interrupted header");
}

void test_parser_oversized_length() {
    /*
     * The parser uses uint8_t arithmetic: expectedLen = (LEN + 7) & 0xFF.
     * Rejection occurs when expectedLen > MAX_PACKET_DATA_SIZE (0xF3).
     *
     * Valid rejection range: LEN values where (LEN+7)&0xFF is in [0xF4..0xFF].
     *   LEN = 0xED → (0xED+7)&0xFF = 0xF4 > 0xF3 → REJECTED ✓
     *   LEN = 0xF8 → (0xF8+7)&0xFF = 0xFF > 0xF3 → REJECTED ✓
     *
     * Wrapping cases that are NOT rejected:
     *   LEN = 0xFF → (0xFF+7)&0xFF = 0x06 ≤ 0xF3 → NOT rejected (wraps!)
     *
     * Use LEN=0xF0: (0xF0+7)&0xFF = 0xF7 > 0xF3 → rejected.
     *
     * @see DRV_1.6.13 @ 0x0800713C: adds r2, r0, #7; uxtb r2, r2
     * @see DRV_1.6.13 @ 0x08007142: cmp r2, #0xf3; bls
     */
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    int count = 0;
    ch.setPacketCallback([&](const Packet&) { count++; });

    ch.receiveByte(0x5A);
    ch.receiveByte(0xA5);
    ch.receiveByte(0xF0);  // LEN=0xF0 → expectedLen = 0xF7 > 0xF3 → REJECT

    /* Parser should have reset; send a valid packet now */
    uint8_t buffer[32];
    size_t len = buildPacket(0x20, 0x21, 0x01, 0x10, nullptr, 0, buffer);
    for (size_t i = 0; i < len; ++i) ch.receiveByte(buffer[i]);

    /* The second packet should be received (parser recovered) */
    TEST_EQ(count, 1, "recovery after oversized length");
}

void test_parser_lonely_a5() {
    /* 0xA5 without preceding 0x5A should be ignored */
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    TEST_EQ(ch.state().gotFirstHeader, 0, "initial: no header");
    ch.receiveByte(0xA5);
    TEST_EQ(ch.state().receivingPacket, 0, "lonely 0xA5 ignored");
}


/* ===========================================================================
 * 6. TX QUEUE TESTS
 * ===========================================================================
 */

void test_tx_enqueue_single() {
    SimulatedUart hw;
    UartChannelHandler ch(UartChannel::CHANNEL_EXT, &hw);

    uint8_t payload[] = {0x02};
    bool ok = ch.enqueuePacket(0x20, 0x21, 0x01, 0x26, payload, 1);
    TEST_ASSERT(ok, "enqueue succeeds");
    TEST_EQ(ch.txQueuedCount(), 1, "1 packet queued");
}

void test_tx_drain_single() {
    SimulatedUart hw;
    UartChannelHandler ch(UartChannel::CHANNEL_EXT, &hw);

    uint8_t payload[] = {0x02};
    ch.enqueuePacket(0x20, 0x21, 0x01, 0x26, payload, 1);

    size_t bytes = ch.drainTx();
    TEST_ASSERT(bytes > 0, "bytes transmitted");
    TEST_EQ(ch.txQueuedCount(), 0, "queue empty after drain");

    /* Verify the transmitted bytes form a valid packet */
    const auto& txBuf = hw.txBuffer();
    TEST_ASSERT(validatePacket(txBuf.data(), txBuf.size()), "TX output is valid packet");
    TEST_EQ(txBuf[0], 0x5A, "TX starts with 0x5A");
    TEST_EQ(txBuf[1], 0xA5, "TX has 0xA5");
}

void test_tx_queue_full() {
    SimulatedUart hw;
    UartChannelHandler ch(UartChannel::CHANNEL_EXT, &hw);

    /* Fill all 4 slots */
    for (int i = 0; i < NUM_TX_SLOTS; ++i) {
        bool ok = ch.enqueuePacket(0x20, 0x21, 0x01, static_cast<uint8_t>(i), nullptr, 0);
        TEST_ASSERT(ok, "enqueue slot succeeds");
    }

    /* 5th should fail (queue full, pendingTxCount = 0) */
    bool ok = ch.enqueuePacket(0x20, 0x21, 0x01, 0xFF, nullptr, 0);
    TEST_ASSERT(!ok, "queue full rejects 5th packet");
}

void test_tx_circular_wrap() {
    SimulatedUart hw;
    UartChannelHandler ch(UartChannel::CHANNEL_EXT, &hw);

    /* Enqueue and drain 6 packets (wraps around the 4-slot buffer) */
    for (int i = 0; i < 6; ++i) {
        uint8_t arg = static_cast<uint8_t>(i);
        bool ok = ch.enqueuePacket(0x20, 0x21, 0x01, arg, nullptr, 0);
        TEST_ASSERT(ok, "enqueue succeeds");
        ch.drainTx();
    }
    TEST_EQ(ch.txQueuedCount(), 0, "all drained after circular wrap");
}

void test_tx_oversized_payload_rejected() {
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    uint8_t bigPayload[MAX_PAYLOAD_LENGTH + 1];
    bool ok = ch.enqueuePacket(0x20, 0x21, 0x01, 0x10, bigPayload, MAX_PAYLOAD_LENGTH + 1);
    TEST_ASSERT(!ok, "oversized payload rejected");
}


/* ===========================================================================
 * 7. REGISTER FILE TESTS
 * ===========================================================================
 */

void test_register_u8() {
    RegisterFile rf;
    rf.writeU8(0x31, 0xAB);
    TEST_EQ(rf.readU8(0x31), 0xAB, "read back U8");
}

void test_register_u16() {
    RegisterFile rf;
    rf.writeU16(0x26, 0x1234);
    TEST_EQ(rf.readU16(0x26), 0x1234, "read back U16 LE");

    /* Verify byte order within the register's own slot */
    uint8_t bytes[2];
    rf.readBytes(0x26, bytes, 2);
    TEST_EQ(bytes[0], 0x34, "low byte");
    TEST_EQ(bytes[1], 0x12, "high byte");
}

void test_register_u32() {
    RegisterFile rf;
    rf.writeU32(0x34, 0xDEADBEEF);
    TEST_EQ(rf.readU32(0x34), 0xDEADBEEF, "read back U32 LE");
}

void test_register_bytes() {
    RegisterFile rf;
    const char* serial = "N2GWX123456789";
    rf.writeBytes(0x10, reinterpret_cast<const uint8_t*>(serial), 14);

    uint8_t out[14];
    rf.readBytes(0x10, out, 14);
    TEST_ASSERT(std::memcmp(out, serial, 14) == 0, "read back bytes");
}


/* ===========================================================================
 * 8. DEVICE SIMULATION DEFAULTS
 * ===========================================================================
 */

void test_esc_defaults() {
    SimulatedESC esc;
    TEST_EQ(esc.getRegU16(esc_reg::FIRMWARE_VERSION), 0x0613, "ESC FW version");
    TEST_EQ(esc.getRegU16(esc_reg::REMAINING_BATTERY), 85, "ESC battery 85%");
    TEST_EQ(esc.getRegU16(esc_reg::BATTERY_VOLTAGE), 3850, "ESC voltage 38.5V");
    TEST_EQ(esc.getRegU8(esc_reg::RIDING_MODE), 1, "ESC default mode = D");
    TEST_EQ(esc.getRegU8(esc_reg::LOCK_STATE), 0, "ESC unlocked");
    TEST_EQ(esc.getRegU16(esc_reg::ERROR_CODE), 0, "ESC no errors");
}

void test_bms_defaults() {
    SimulatedBMS bms;
    TEST_EQ(bms.getRegU16(bms_reg::REMAINING_PERCENT), 85, "BMS 85%");
    TEST_EQ(bms.getRegU16(bms_reg::VOLTAGE), 38500, "BMS 38.5V");
    TEST_EQ(bms.getRegU16(bms_reg::FULL_CAPACITY), 5100, "BMS 5100 mAh");
    TEST_EQ(bms.getRegU16(bms_reg::CYCLE_COUNT), 142, "BMS 142 cycles");
    TEST_EQ(bms.getRegU16(bms_reg::TEMPERATURE_1), 280, "BMS temp1 28.0C");

    /* Cell voltage checks */
    TEST_ASSERT(bms.minCellVoltage() >= 3800, "min cell >= 3.8V");
    TEST_ASSERT(bms.maxCellVoltage() <= 3900, "max cell <= 3.9V");
    TEST_ASSERT(bms.cellImbalance() < 20, "imbalance < 20mV");
}

void test_ble_defaults() {
    SimulatedBLE ble;
    uint8_t serial[14];
    ble.registers().readBytes(ble_reg::SERIAL_NUMBER, serial, 14);
    TEST_ASSERT(std::memcmp(serial, "N2GBL123456789", 14) == 0, "BLE serial");
    TEST_EQ(ble.getRegU16(ble_reg::FIRMWARE_VERSION), 0x0117, "BLE FW version");
}


/* ===========================================================================
 * 9. REGISTER READ VIA PROTOCOL
 * ===========================================================================
 */

void test_device_register_read() {
    SimulatedESC esc;

    /* Prepare a READ command: APP reads ESC speed register */
    uint8_t payload[] = {0x02};  // Read 2 bytes
    uint8_t request[32];
    size_t reqLen = buildPacket(
        static_cast<uint8_t>(DeviceAddress::APP),
        static_cast<uint8_t>(DeviceAddress::ESC),
        static_cast<uint8_t>(Command::READ),
        esc_reg::CURRENT_SPEED,
        payload, 1, request
    );

    /* Feed request into the ESC */
    esc.feedBytes(request, reqLen);

    /* Drain the ESC's response */
    auto response = esc.drainTxBytes();
    TEST_ASSERT(response.size() > 0, "ESC produced a response");

    /* Parse the response through a parser to verify structure */
    bool gotResponse = false;
    Packet rxPkt{};
    UartChannelHandler parser(UartChannel::CHANNEL_EXT);
    parser.setPacketCallback([&](const Packet& pkt) {
        gotResponse = true;
        rxPkt = pkt;
    });
    for (auto b : response) parser.receiveByte(b);

    TEST_ASSERT(gotResponse, "response is valid packet");
    TEST_EQ(rxPkt.source, static_cast<uint8_t>(DeviceAddress::ESC), "response from ESC");
    TEST_EQ(rxPkt.destination, static_cast<uint8_t>(DeviceAddress::APP), "response to APP");
    TEST_EQ(rxPkt.command, static_cast<uint8_t>(Command::READ_RESPONSE), "READ_RESPONSE cmd");
    TEST_EQ(rxPkt.argument, esc_reg::CURRENT_SPEED, "response for speed reg");
    TEST_EQ(rxPkt.payloadLength, 2, "response has 2 bytes");
    /* Speed should be 0 (default) */
    TEST_EQ(rxPkt.payloadU16(), 0, "speed = 0");
}

void test_device_register_read_after_set() {
    SimulatedESC esc;
    esc.setSpeed(25000);  // 25.000 km/h

    /* Read speed register */
    uint8_t payload[] = {0x02};
    uint8_t request[32];
    size_t reqLen = buildPacket(0x3E, 0x20, 0x01, esc_reg::CURRENT_SPEED,
                                payload, 1, request);
    esc.feedBytes(request, reqLen);

    auto response = esc.drainTxBytes();
    UartChannelHandler parser(UartChannel::CHANNEL_EXT);
    Packet rxPkt{};
    parser.setPacketCallback([&](const Packet& pkt) { rxPkt = pkt; });
    for (auto b : response) parser.receiveByte(b);

    TEST_EQ(rxPkt.payloadU16(), 25000, "speed = 25000 after set");
}


/* ===========================================================================
 * 10. REGISTER WRITE VIA PROTOCOL
 * ===========================================================================
 */

void test_device_register_write() {
    SimulatedESC esc;

    /* Write riding mode to Sport (2) */
    uint8_t payload[] = {0x02};  // Sport mode
    uint8_t request[32];
    size_t reqLen = buildPacket(
        0x3E, 0x20,
        static_cast<uint8_t>(Command::WRITE),
        esc_reg::RIDING_MODE,
        payload, 1, request
    );

    esc.feedBytes(request, reqLen);

    /* Verify register was updated */
    TEST_EQ(esc.getRegU8(esc_reg::RIDING_MODE), 0x02, "riding mode = Sport");

    /* Verify we got a WRITE_RESPONSE */
    auto response = esc.drainTxBytes();
    TEST_ASSERT(response.size() > 0, "write ACK produced");

    UartChannelHandler parser(UartChannel::CHANNEL_EXT);
    Packet rxPkt{};
    parser.setPacketCallback([&](const Packet& pkt) { rxPkt = pkt; });
    for (auto b : response) parser.receiveByte(b);

    TEST_EQ(rxPkt.command, static_cast<uint8_t>(Command::WRITE_RESPONSE), "WRITE_RESPONSE");
    TEST_EQ(rxPkt.argument, esc_reg::RIDING_MODE, "ACK for riding mode reg");
}

void test_device_write_lock() {
    SimulatedESC esc;
    TEST_EQ(esc.getRegU8(esc_reg::LOCK_STATE), 0, "initially unlocked");

    /* Lock the scooter */
    uint8_t payload[] = {0x01};
    uint8_t request[32];
    size_t len = buildPacket(0x3E, 0x20, 0x02, esc_reg::LOCK_STATE, payload, 1, request);
    esc.feedBytes(request, len);

    TEST_EQ(esc.getRegU8(esc_reg::LOCK_STATE), 1, "locked after write");
}


/* ===========================================================================
 * 11. MULTI-BOARD BUS SIMULATION
 * ===========================================================================
 */

void test_bus_esc_to_ble() {
    SimulationBus bus;

    /* ESC sends a packet to BLE */
    bus.esc().uart().enqueuePacket(
        DeviceAddress::ESC, DeviceAddress::BLE,
        Command::READ, ble_reg::FIRMWARE_VERSION,
        nullptr, 0
    );

    /* Transfer bytes ESC -> BLE */
    bus.transferAll();

    /* BLE should have received the packet */
    auto& rxLog = bus.ble().receivedPackets();
    TEST_ASSERT(rxLog.size() >= 1, "BLE received packet from ESC");
    TEST_EQ(rxLog[0].source, static_cast<uint8_t>(DeviceAddress::ESC), "from ESC");
}

void test_bus_app_reads_esc_speed() {
    SimulationBus bus;
    bus.esc().setSpeed(18500);  // 18.500 km/h

    /* Inject a read request from APP to ESC */
    uint8_t payload[] = {0x02};
    bus.sendTo(bus.esc(), DeviceAddress::APP, DeviceAddress::ESC,
               Command::READ, esc_reg::CURRENT_SPEED, payload, 1);

    /* ESC processes it and queues response */
    /* Transfer response bytes back (ESC -> BLE direction, but we'll capture directly) */
    auto response = bus.esc().drainTxBytes();
    TEST_ASSERT(response.size() > 0, "ESC produced speed response");

    /* Parse response */
    UartChannelHandler parser(UartChannel::CHANNEL_EXT);
    Packet rxPkt{};
    parser.setPacketCallback([&](const Packet& pkt) { rxPkt = pkt; });
    for (auto b : response) parser.receiveByte(b);

    TEST_EQ(rxPkt.payloadU16(), 18500, "speed response = 18500");
}


/* ===========================================================================
 * 12. FULL ROUND-TRIP: APP -> ESC -> BMS -> ESC -> APP
 * ===========================================================================
 */

void test_roundtrip_bms_voltage() {
    SimulationBus bus;

    /* Set BMS voltage to 37.2V (37200 mV) */
    bus.bms().setRegU16(bms_reg::VOLTAGE, 37200);

    /* Step 1: App sends read request to BMS (via ESC) */
    uint8_t readPayload[] = {0x02};
    uint8_t request[32];
    size_t reqLen = buildPacket(
        static_cast<uint8_t>(DeviceAddress::APP),
        static_cast<uint8_t>(DeviceAddress::BMS),
        static_cast<uint8_t>(Command::READ),
        bms_reg::VOLTAGE,
        readPayload, 1, request
    );

    /* Feed directly into BMS (simulating ESC forwarding) */
    bus.bms().feedBytes(request, reqLen);

    /* Step 2: BMS produces response */
    auto bmsResponse = bus.bms().drainTxBytes();
    TEST_ASSERT(bmsResponse.size() > 0, "BMS produced voltage response");

    /* Step 3: Parse BMS response */
    UartChannelHandler parser(UartChannel::CHANNEL_EXT);
    Packet rxPkt{};
    parser.setPacketCallback([&](const Packet& pkt) { rxPkt = pkt; });
    for (auto b : bmsResponse) parser.receiveByte(b);

    TEST_EQ(rxPkt.source, static_cast<uint8_t>(DeviceAddress::BMS), "from BMS");
    TEST_EQ(rxPkt.destination, static_cast<uint8_t>(DeviceAddress::APP), "to APP");
    TEST_EQ(rxPkt.argument, bms_reg::VOLTAGE, "voltage register");
    TEST_EQ(rxPkt.payloadU16(), 37200, "voltage = 37200 mV = 37.2V");
}

void test_roundtrip_bms_cell_voltages() {
    SimulationBus bus;

    /* Read individual cell voltages */
    for (int cell = 0; cell < bms_reg::NUM_CELLS; ++cell) {
        uint8_t reg = bms_reg::CELL_VOLTAGE_BASE + cell;
        uint8_t readPayload[] = {0x02};
        uint8_t request[32];
        size_t reqLen = buildPacket(0x3E, 0x22, 0x01, reg, readPayload, 1, request);

        bus.bms().feedBytes(request, reqLen);
        auto resp = bus.bms().drainTxBytes();

        UartChannelHandler p(UartChannel::CHANNEL_EXT);
        Packet pkt{};
        p.setPacketCallback([&](const Packet& pk) { pkt = pk; });
        for (auto b : resp) p.receiveByte(b);

        uint16_t cellV = pkt.payloadU16();
        TEST_ASSERT(cellV >= 3800 && cellV <= 3900, "cell voltage in range");
    }
}


/* ===========================================================================
 * 13. ERROR INJECTION AND ERROR CODE HANDLING
 * ===========================================================================
 */

void test_esc_error_code() {
    SimulatedESC esc;
    esc.setError(esc_error::HALL_SENSOR_ERROR | esc_error::THROTTLE_ERROR);

    uint16_t errors = esc.getRegU16(esc_reg::ERROR_CODE);
    TEST_ASSERT(errors & esc_error::HALL_SENSOR_ERROR, "hall error set");
    TEST_ASSERT(errors & esc_error::THROTTLE_ERROR, "throttle error set");
    TEST_ASSERT(!(errors & esc_error::BUS_OVERVOLTAGE), "overvoltage not set");
}

void test_esc_error_read_via_protocol() {
    SimulatedESC esc;
    esc.setError(esc_error::COMMUNICATION_ERROR);

    uint8_t payload[] = {0x02};
    uint8_t request[32];
    size_t len = buildPacket(0x3E, 0x20, 0x01, esc_reg::ERROR_CODE, payload, 1, request);
    esc.feedBytes(request, len);

    auto resp = esc.drainTxBytes();
    UartChannelHandler parser(UartChannel::CHANNEL_EXT);
    Packet pkt{};
    parser.setPacketCallback([&](const Packet& p) { pkt = p; });
    for (auto b : resp) parser.receiveByte(b);

    TEST_EQ(pkt.payloadU16(), static_cast<uint16_t>(esc_error::COMMUNICATION_ERROR),
            "error code via protocol");
}


/* ===========================================================================
 * 14. BMS CELL VOLTAGE MONITORING
 * ===========================================================================
 */

void test_bms_cell_imbalance() {
    SimulatedBMS bms;

    /* Set unbalanced cells: one much lower */
    uint16_t voltages[10] = {3850, 3850, 3850, 3850, 3200,
                             3850, 3850, 3850, 3850, 3850};
    bms.setCellVoltages(voltages);

    TEST_EQ(bms.minCellVoltage(), 3200, "min cell = 3.2V");
    TEST_EQ(bms.maxCellVoltage(), 3850, "max cell = 3.85V");
    TEST_EQ(bms.cellImbalance(), 650, "650mV imbalance");
}

void test_bms_capacity_update() {
    SimulatedBMS bms;
    bms.setCapacity(2550, 50);

    TEST_EQ(bms.getRegU16(bms_reg::REMAINING_CAPACITY), 2550, "capacity = 2550 mAh");
    TEST_EQ(bms.getRegU16(bms_reg::REMAINING_PERCENT), 50, "50%");
}

void test_bms_current_discharge() {
    SimulatedBMS bms;
    bms.setCurrent(-5000);  // 5A discharge

    int16_t current = static_cast<int16_t>(bms.getRegU16(bms_reg::CURRENT));
    TEST_EQ(current, -5000, "current = -5000 mA (discharging)");
}


/* ===========================================================================
 * 15. SPEED AND RIDING MODE CHANGES
 * ===========================================================================
 */

void test_esc_speed_change() {
    SimulatedESC esc;
    esc.setSpeed(0);
    TEST_EQ(esc.getRegU16(esc_reg::CURRENT_SPEED), 0, "speed = 0");

    esc.setSpeed(30000);  // 30.000 km/h
    TEST_EQ(esc.getRegU16(esc_reg::CURRENT_SPEED), 30000, "speed = 30 km/h");

    esc.setSpeed(25000);  // 25.000 km/h
    TEST_EQ(esc.getRegU16(esc_reg::CURRENT_SPEED), 25000, "speed = 25 km/h");
}

void test_esc_riding_modes() {
    SimulatedESC esc;

    /* Eco mode */
    esc.setRidingMode(0);
    TEST_EQ(esc.getRegU8(esc_reg::RIDING_MODE), 0, "Eco mode");

    /* D mode */
    esc.setRidingMode(1);
    TEST_EQ(esc.getRegU8(esc_reg::RIDING_MODE), 1, "D mode");

    /* Sport mode */
    esc.setRidingMode(2);
    TEST_EQ(esc.getRegU8(esc_reg::RIDING_MODE), 2, "Sport mode");
}

void test_esc_uptime_tick() {
    SimulatedESC esc;
    TEST_EQ(esc.getRegU16(esc_reg::UPTIME), 0, "uptime starts at 0");

    for (int i = 0; i < 10; ++i) esc.tick();
    TEST_EQ(esc.getRegU16(esc_reg::UPTIME), 10, "uptime = 10 after 10 ticks");
}

void test_esc_write_cruise_control() {
    SimulatedESC esc;

    uint8_t payload[] = {0x01};  // Enable
    uint8_t request[32];
    size_t len = buildPacket(0x3E, 0x20, 0x02, esc_reg::CRUISE_CONTROL, payload, 1, request);
    esc.feedBytes(request, len);
    esc.drainTxBytes();  // Consume ACK

    TEST_EQ(esc.getRegU8(esc_reg::CRUISE_CONTROL), 1, "cruise control enabled");
}


/* ===========================================================================
 * 16. STRESS TEST: RAPID PACKET BURSTS
 * ===========================================================================
 */

void test_stress_rapid_packets() {
    UartChannelHandler ch(UartChannel::CHANNEL_EXT);
    int count = 0;
    ch.setPacketCallback([&](const Packet&) { count++; });

    /* Send 100 packets as fast as possible */
    for (int i = 0; i < 100; ++i) {
        uint8_t payload[] = {
            static_cast<uint8_t>(i & 0xFF),
            static_cast<uint8_t>((i >> 8) & 0xFF)
        };
        uint8_t buffer[32];
        size_t len = buildPacket(0x20, 0x21, 0x01, 0x10, payload, 2, buffer);
        for (size_t j = 0; j < len; ++j) {
            ch.receiveByte(buffer[j]);
        }
    }

    TEST_EQ(count, 100, "received all 100 rapid packets");
}

void test_stress_tx_burst() {
    SimulatedUart hw;
    UartChannelHandler ch(UartChannel::CHANNEL_EXT, &hw);

    /* Send 20 packets through the TX queue (wraps multiple times) */
    int txCount = 0;
    for (int i = 0; i < 20; ++i) {
        uint8_t arg = static_cast<uint8_t>(i);
        ch.enqueuePacket(0x20, 0x21, 0x01, arg, nullptr, 0);
        hw.clearTxBuffer();
        size_t bytes = ch.drainTx();
        if (bytes > 0) txCount++;
    }

    TEST_EQ(txCount, 20, "all 20 TX packets drained");
}

void test_stress_full_bus_simulation() {
    SimulationBus bus;

    /* Simulate 50 cycles of operation */
    for (int i = 0; i < 50; ++i) {
        /* Every 5 cycles, app requests speed from ESC */
        if (i % 5 == 0) {
            bus.esc().setSpeed(static_cast<uint16_t>(i * 500));
            uint8_t payload[] = {0x02};
            bus.sendTo(bus.esc(), DeviceAddress::APP, DeviceAddress::ESC,
                       Command::READ, esc_reg::CURRENT_SPEED, payload, 1);
        }

        /* Every 10 cycles, app requests BMS voltage */
        if (i % 10 == 0) {
            uint8_t payload[] = {0x02};
            bus.sendTo(bus.bms(), DeviceAddress::APP, DeviceAddress::BMS,
                       Command::READ, bms_reg::VOLTAGE, payload, 1);
        }

        bus.cycle();
    }

    /* Verify final ESC state */
    uint16_t finalSpeed = bus.esc().getRegU16(esc_reg::CURRENT_SPEED);
    TEST_ASSERT(finalSpeed > 0, "speed was updated during simulation");

    /* Verify uptime ticked */
    uint16_t uptime = bus.esc().getRegU16(esc_reg::UPTIME);
    TEST_EQ(uptime, 50, "uptime = 50 after 50 cycles");
}


/* ===========================================================================
 * 17. PACKET HELPER TESTS
 * ===========================================================================
 */

void test_make_packet_helper() {
    Packet pkt = makePacket(DeviceAddress::ESC, DeviceAddress::BLE,
                            Command::READ, 0x26);
    TEST_EQ(pkt.source, 0x20, "makePacket source");
    TEST_EQ(pkt.destination, 0x21, "makePacket destination");
    TEST_EQ(pkt.command, 0x01, "makePacket command");
    TEST_EQ(pkt.length, 0, "makePacket length (no payload)");
}

void test_packet_address_helpers() {
    Packet pkt = makePacket(DeviceAddress::ESC, DeviceAddress::BLE,
                            Command::READ, 0x26);
    TEST_ASSERT(pkt.isFor(DeviceAddress::BLE), "isFor BLE");
    TEST_ASSERT(pkt.isFrom(DeviceAddress::ESC), "isFrom ESC");
    TEST_ASSERT(!pkt.isFor(DeviceAddress::BMS), "not for BMS");
}

void test_packet_payload_u16_u32() {
    uint8_t payload[] = {0x78, 0x56, 0x34, 0x12};
    Packet pkt = makePacket(DeviceAddress::ESC, DeviceAddress::BLE,
                            Command::READ_RESPONSE, 0x34, payload, 4);
    TEST_EQ(pkt.payloadU16(0), 0x5678, "payloadU16 at offset 0");
    TEST_EQ(pkt.payloadU16(2), 0x1234, "payloadU16 at offset 2");
    TEST_EQ(pkt.payloadU32(0), 0x12345678u, "payloadU32 at offset 0");
}


/* ===========================================================================
 * MAIN
 * =========================================================================== */

int main() {
    std::printf(CLR_BOLD "\n============================================================\n");
    std::printf("  Ninebot G30 Max Protocol Library - Test Suite\n");
    std::printf("  Reconstructed from DRV_1.6.13 / BLE_1.1.7 / BMS_1.7.4.5\n");
    std::printf("============================================================\n" CLR_RESET);

    /* --- 1. Checksum --- */
    TEST_GROUP("1. Checksum Calculation");
    RUN_TEST(test_checksum_empty);
    RUN_TEST(test_checksum_single_byte);
    RUN_TEST(test_checksum_known_packet);
    RUN_TEST(test_checksum_all_ff);
    RUN_TEST(test_checksum_overflow_16bit);

    /* --- 2. Packet Building --- */
    TEST_GROUP("2. Packet Building");
    RUN_TEST(test_build_minimal_packet);
    RUN_TEST(test_build_packet_with_payload);
    RUN_TEST(test_build_packet_max_payload);
    RUN_TEST(test_build_all_device_addresses);

    /* --- 3. Packet Validation --- */
    TEST_GROUP("3. Packet Validation");
    RUN_TEST(test_validate_good_packet);
    RUN_TEST(test_validate_corrupted_checksum);
    RUN_TEST(test_validate_corrupted_data);
    RUN_TEST(test_validate_wrong_header);
    RUN_TEST(test_validate_too_short);

    /* --- 4. Parser State Machine --- */
    TEST_GROUP("4. Parser State Machine");
    RUN_TEST(test_parser_receives_valid_packet);
    RUN_TEST(test_parser_rejects_bad_checksum);
    RUN_TEST(test_parser_resets_on_garbage);
    RUN_TEST(test_parser_handles_consecutive_packets);

    /* --- 5. Parser Edge Cases --- */
    TEST_GROUP("5. Parser Edge Cases");
    RUN_TEST(test_parser_double_header);
    RUN_TEST(test_parser_interleaved_header);
    RUN_TEST(test_parser_oversized_length);
    RUN_TEST(test_parser_lonely_a5);

    /* --- 6. TX Queue --- */
    TEST_GROUP("6. TX Queue");
    RUN_TEST(test_tx_enqueue_single);
    RUN_TEST(test_tx_drain_single);
    RUN_TEST(test_tx_queue_full);
    RUN_TEST(test_tx_circular_wrap);
    RUN_TEST(test_tx_oversized_payload_rejected);

    /* --- 7. Register File --- */
    TEST_GROUP("7. Register File");
    RUN_TEST(test_register_u8);
    RUN_TEST(test_register_u16);
    RUN_TEST(test_register_u32);
    RUN_TEST(test_register_bytes);

    /* --- 8. Device Defaults --- */
    TEST_GROUP("8. Device Simulation Defaults");
    RUN_TEST(test_esc_defaults);
    RUN_TEST(test_bms_defaults);
    RUN_TEST(test_ble_defaults);

    /* --- 9. Register Read via Protocol --- */
    TEST_GROUP("9. Register Read via Protocol");
    RUN_TEST(test_device_register_read);
    RUN_TEST(test_device_register_read_after_set);

    /* --- 10. Register Write via Protocol --- */
    TEST_GROUP("10. Register Write via Protocol");
    RUN_TEST(test_device_register_write);
    RUN_TEST(test_device_write_lock);

    /* --- 11. Multi-Board Bus --- */
    TEST_GROUP("11. Multi-Board Bus Simulation");
    RUN_TEST(test_bus_esc_to_ble);
    RUN_TEST(test_bus_app_reads_esc_speed);

    /* --- 12. Full Round-Trip --- */
    TEST_GROUP("12. Full Round-Trip (APP -> ESC -> BMS -> APP)");
    RUN_TEST(test_roundtrip_bms_voltage);
    RUN_TEST(test_roundtrip_bms_cell_voltages);

    /* --- 13. Error Handling --- */
    TEST_GROUP("13. Error Injection & Handling");
    RUN_TEST(test_esc_error_code);
    RUN_TEST(test_esc_error_read_via_protocol);

    /* --- 14. BMS Monitoring --- */
    TEST_GROUP("14. BMS Cell Voltage Monitoring");
    RUN_TEST(test_bms_cell_imbalance);
    RUN_TEST(test_bms_capacity_update);
    RUN_TEST(test_bms_current_discharge);

    /* --- 15. Speed & Modes --- */
    TEST_GROUP("15. Speed & Riding Mode Changes");
    RUN_TEST(test_esc_speed_change);
    RUN_TEST(test_esc_riding_modes);
    RUN_TEST(test_esc_uptime_tick);
    RUN_TEST(test_esc_write_cruise_control);

    /* --- 16. Stress Tests --- */
    TEST_GROUP("16. Stress Tests");
    RUN_TEST(test_stress_rapid_packets);
    RUN_TEST(test_stress_tx_burst);
    RUN_TEST(test_stress_full_bus_simulation);

    /* --- 17. Helpers --- */
    TEST_GROUP("17. Packet Helpers");
    RUN_TEST(test_make_packet_helper);
    RUN_TEST(test_packet_address_helpers);
    RUN_TEST(test_packet_payload_u16_u32);

    /* --- Summary --- */
    std::printf("\n" CLR_BOLD "============================================================\n");
    std::printf("  Results: ");
    if (g_testsFailed == 0) {
        std::printf(CLR_GREEN "ALL %d TESTS PASSED" CLR_RESET, g_testsPassed);
    } else {
        std::printf(CLR_RED "%d FAILED" CLR_RESET ", %d passed",
                    g_testsFailed, g_testsPassed);
    }
    std::printf(" (%d assertions)\n", g_totalAsserts);
    std::printf(CLR_BOLD "============================================================\n" CLR_RESET "\n");

    return g_testsFailed > 0 ? 1 : 0;
}
