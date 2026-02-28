/**
 * @file test_protocol.cpp
 * @brief Protocol layer tests — checksum, packet building, parsing.
 *
 * Validates the shared protocol implementation matches the original
 * firmware behavior at:
 *   - calculateChecksum @ 0x08002720
 *   - buildPacket       @ 0x080036AC
 *   - parseProtocolByte @ 0x08007128
 *   - enqueuePacket     @ 0x080071F4
 *
 * Test patterns derived from actual Ninebot protocol captures.
 */

#include "test_framework.h"
#include "protocol.h"
#include "sim_hal.h"

using namespace ninebot;

/* =========================================================================
 * Checksum Tests
 * ========================================================================= */

/**
 * Test: Empty payload checksum.
 * A packet with no payload: LEN=0x06, SRC=0x3E, DST=0x20, CMD=0x01, ARG=0x10
 * Checksum covers LEN through ARG: 0x06 + 0x3E + 0x20 + 0x01 + 0x10 = 0x75
 * Expected: ~0x75 & 0xFFFF = 0xFF8A
 */
TEST(Protocol, ChecksumEmptyPayload)
{
    uint8_t data[] = {0x06, 0x3E, 0x20, 0x01, 0x10};
    uint16_t cs = calculateChecksum(data, 5);
    ASSERT_EQ(cs, static_cast<uint16_t>(0xFF8A));
}

/**
 * Test: Two-byte payload checksum.
 * LEN=0x08, SRC=0x20, DST=0x3E, CMD=0x03, ARG=0x10, Payload=0xE8 0x03
 * Sum = 0x08+0x20+0x3E+0x03+0x10+0xE8+0x03 = 0x0164
 * Expected: ~0x0164 & 0xFFFF = 0xFE9B
 */
TEST(Protocol, ChecksumWithPayload)
{
    uint8_t data[] = {0x08, 0x20, 0x3E, 0x03, 0x10, 0xE8, 0x03};
    uint16_t cs = calculateChecksum(data, 7);
    ASSERT_EQ(cs, static_cast<uint16_t>(0xFE9B));
}

/**
 * Test: Zero-filled data (edge case).
 */
TEST(Protocol, ChecksumAllZeros)
{
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
    uint16_t cs = calculateChecksum(data, 4);
    ASSERT_EQ(cs, static_cast<uint16_t>(0xFFFF));
}


/* =========================================================================
 * Packet Building Tests
 * ========================================================================= */

/**
 * Test: Build a READ request packet (no payload).
 * App (0x3E) → ESC (0x20), READ (0x01), register 0x10
 */
TEST(Protocol, BuildReadRequest)
{
    uint8_t buf[64];
    int len = buildPacket(0x3E, 0x20, 0x01, 0x10, nullptr, 0, buf);

    /* Header: 5A A5 */
    ASSERT_EQ(buf[0], 0x5A);
    ASSERT_EQ(buf[1], 0xA5);

    /* LEN = 2 (SRC+DST) + 0 (payload) + 2 (CMD+ARG) = 4, plus checksum overhead... 
     * Actually: LEN = payloadLen + 2 + 2 = payloadLen + 4.
     * For 0 payload: LEN = 4. But firmware buildPacket sets LEN = payloadLen + 2 + 2.
     * Let's verify the format matches. */

    /* SRC, DST, CMD, ARG */
    ASSERT_EQ(buf[3], 0x3E);  // source
    ASSERT_EQ(buf[4], 0x20);  // destination
    ASSERT_EQ(buf[5], 0x01);  // command (READ)
    ASSERT_EQ(buf[6], 0x10);  // argument

    /* Total length should be > 0 */
    ASSERT_GT(len, 0);

    /* Verify the checksum at the end is valid */
    uint16_t sum = 0;
    for (int i = 2; i < len - 2; i++) sum += buf[i];
    uint16_t expected = ~sum & 0xFFFF;
    uint16_t actual   = buf[len-2] | (buf[len-1] << 8);
    ASSERT_EQ(actual, expected);
}

/**
 * Test: Build a WRITE packet with payload.
 * App (0x3E) → ESC (0x20), WRITE (0x02), register 0x26, payload=[0x50, 0x00]
 */
TEST(Protocol, BuildWriteWithPayload)
{
    uint8_t payload[] = {0x50, 0x00};
    uint8_t buf[64];
    int len = buildPacket(0x3E, 0x20, 0x02, 0x26, payload, 2, buf);

    ASSERT_EQ(buf[0], 0x5A);
    ASSERT_EQ(buf[1], 0xA5);
    ASSERT_EQ(buf[3], 0x3E);  // source
    ASSERT_EQ(buf[4], 0x20);  // destination
    ASSERT_EQ(buf[5], 0x02);  // command (WRITE)
    ASSERT_EQ(buf[6], 0x26);  // register
    ASSERT_EQ(buf[7], 0x50);  // payload byte 0
    ASSERT_EQ(buf[8], 0x00);  // payload byte 1

    /* Checksum verification */
    uint16_t sum = 0;
    for (int i = 2; i < len - 2; i++) sum += buf[i];
    uint16_t expected = ~sum & 0xFFFF;
    uint16_t actual   = buf[len-2] | (buf[len-1] << 8);
    ASSERT_EQ(actual, expected);
}


/* =========================================================================
 * Protocol Parser Tests
 * ========================================================================= */

/**
 * Test: Full packet parse cycle.
 * Feed a valid packet byte-by-byte to a ProtocolChannel and verify callback.
 */
TEST(Protocol, ParseValidPacket)
{
    sim::SimUart uart;
    ProtocolChannel ch(&uart);

    bool received = false;
    Packet rxPkt;

    ch.setCallback([&](const Packet& pkt) {
        received = true;
        rxPkt = pkt;
    });

    /* Build a packet: App→ESC, READ, reg 0x10 */
    uint8_t buf[64];
    int len = buildPacket(0x3E, 0x20, 0x01, 0x10, nullptr, 0, buf);

    /* Feed all bytes */
    for (int i = 0; i < len; i++) {
        ch.receiveByte(buf[i]);
    }

    ASSERT_TRUE(received);
    ASSERT_EQ(rxPkt.source, 0x3E);
    ASSERT_EQ(rxPkt.destination, 0x20);
    ASSERT_EQ(rxPkt.command, 0x01);
    ASSERT_EQ(rxPkt.argument, 0x10);
}

/**
 * Test: Corrupted packet is rejected (bad checksum).
 */
TEST(Protocol, RejectBadChecksum)
{
    sim::SimUart uart;
    ProtocolChannel ch(&uart);

    bool received = false;
    ch.setCallback([&](const Packet& /*pkt*/) { received = true; });

    /* Build a valid packet then corrupt it */
    uint8_t buf[64];
    int len = buildPacket(0x3E, 0x20, 0x01, 0x10, nullptr, 0, buf);
    buf[len - 1] ^= 0xFF;  // corrupt checksum high byte

    for (int i = 0; i < len; i++) {
        ch.receiveByte(buf[i]);
    }

    ASSERT_FALSE(received);
}

/**
 * Test: Garbage before header is ignored.
 */
TEST(Protocol, IgnoreGarbageBeforeHeader)
{
    sim::SimUart uart;
    ProtocolChannel ch(&uart);

    bool received = false;
    ch.setCallback([&](const Packet& /*pkt*/) { received = true; });

    /* Send garbage */
    ch.receiveByte(0xDE);
    ch.receiveByte(0xAD);
    ch.receiveByte(0xBE);
    ch.receiveByte(0xEF);

    /* Then a valid packet */
    uint8_t buf[64];
    int len = buildPacket(0x3E, 0x20, 0x01, 0x10, nullptr, 0, buf);
    for (int i = 0; i < len; i++) ch.receiveByte(buf[i]);

    ASSERT_TRUE(received);
}


/* =========================================================================
 * TX Queue Tests
 * ========================================================================= */

/**
 * Test: Enqueue and drain a packet.
 */
TEST(Protocol, EnqueueAndDrain)
{
    sim::SimUart uart;
    ProtocolChannel ch(&uart);

    bool ok = ch.enqueuePacket(0x20, 0x3E, 0x03, 0x10, nullptr, 0);
    ASSERT_TRUE(ok);

    size_t sent = ch.drainTx();
    ASSERT_GT(sent, static_cast<size_t>(0));

    /* Verify the SimUart received the bytes */
    const auto& txData = uart.txData();
    ASSERT_GE(txData.size(), static_cast<size_t>(7));  // minimum packet size
    ASSERT_EQ(txData[0], 0x5A);
    ASSERT_EQ(txData[1], 0xA5);
}

/**
 * Test: Enqueue multiple packets and drain all.
 */
TEST(Protocol, EnqueueMultiple)
{
    sim::SimUart uart;
    ProtocolChannel ch(&uart);

    /* Enqueue 3 packets */
    ch.enqueuePacket(0x20, 0x3E, 0x03, 0x10, nullptr, 0);
    ch.enqueuePacket(0x20, 0x3E, 0x03, 0x11, nullptr, 0);
    ch.enqueuePacket(0x20, 0x3E, 0x03, 0x12, nullptr, 0);

    ch.drainTx();

    /* Should have 3 complete packets in the TX buffer */
    const auto& txData = uart.txData();
    ASSERT_GT(txData.size(), static_cast<size_t>(20));

    /* Count 5A A5 headers */
    int headerCount = 0;
    for (size_t i = 0; i + 1 < txData.size(); i++) {
        if (txData[i] == 0x5A && txData[i+1] == 0xA5) headerCount++;
    }
    ASSERT_EQ(headerCount, 3);
}

/**
 * Test: Round-trip — build, parse, callback.
 * Enqueue a packet on one channel, drain to a SimUart, feed the
 * bytes to another channel, and verify callback fires.
 */
TEST(Protocol, RoundTrip)
{
    sim::SimUart uartA, uartB;
    ProtocolChannel chA(&uartA);
    ProtocolChannel chB(&uartB);

    /* Wire: A's TX goes to B's parser */
    uartA.wireTo(&uartB);

    bool received = false;
    Packet rxPkt;
    chB.setCallback([&](const Packet& pkt) {
        received = true;
        rxPkt = pkt;
    });

    /* Enqueue on A */
    uint8_t payload[] = {0x42, 0x00};
    chA.enqueuePacket(0x20, 0x3E, 0x03, 0x10, payload, 2);
    chA.drainTx();

    /* Feed the bytes that arrived at B's SimUart into B's parser */
    /* With wireTo, uartA.sendByte() → uartB.injectRxByte() but we need
     * to read from uartB and feed to chB */
    while (uartB.isRxReady()) {
        chB.receiveByte(uartB.receiveByte());
    }

    ASSERT_TRUE(received);
    ASSERT_EQ(rxPkt.source, 0x20);
    ASSERT_EQ(rxPkt.destination, 0x3E);
    ASSERT_EQ(rxPkt.command, 0x03);
    ASSERT_EQ(rxPkt.argument, 0x10);
    ASSERT_EQ(rxPkt.payloadLength, 2);
    ASSERT_EQ(rxPkt.payload[0], 0x42);
}
