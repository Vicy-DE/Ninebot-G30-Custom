/**
 * @file protocol.h
 * @brief Ninebot serial protocol — shared between all three boards.
 *
 * @note WIRE FORMAT (firmware-verified, DRV_1.6.13): the LEN byte equals the
 *       PAYLOAD byte count; the parser's expected body = LEN + 7 and the full
 *       frame = LEN + 9 bytes. This was reconciled on 2026-06-03 (an earlier
 *       revision used LEN = payload + 6, which round-tripped in this simulator
 *       but was NOT compatible with a real scooter). The byte-faithful
 *       decompilation and proof live in
 *       `common/include/ninebot_protocol_verified.hpp` and
 *       `firmware/decompiled/DECOMPILATION.md`.
 *
 * Reconstructed from DRV_1.6.13 firmware disassembly. This file contains
 * the wire protocol implementation that is identical across ESC, BLE,
 * and BMS firmware. Each board instantiates the same parser/builder code
 * with different UART channel assignments.
 *
 * Protocol framing:
 * @code
 *   [0x5A] [0xA5] [LEN] [SRC] [DST] [CMD] [ARG] [PAYLOAD...] [CHK_LO] [CHK_HI]
 * @endcode
 *
 * @see DRV_1.6.13 calculateChecksum  @ 0x08002720
 * @see DRV_1.6.13 buildPacket        @ 0x080036AC
 * @see DRV_1.6.13 parseProtocolByte  @ 0x08007128 (USART1)
 * @see DRV_1.6.13 enqueuePacket      @ 0x080071F4 (USART1)
 * @see DRV_1.6.13 uartTransmitHandler@ 0x08007610 (USART1)
 */

#ifndef NINEBOT_PROTOCOL_COMMON_H
#define NINEBOT_PROTOCOL_COMMON_H

#include "hal.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>

namespace ninebot {

/* =========================================================================
 * Protocol Constants
 * =========================================================================
 * Extracted from literal pool analysis of DRV_1.6.13.
 */

static constexpr uint8_t HEADER_BYTE_1       = 0x5A;   ///< First magic byte
static constexpr uint8_t HEADER_BYTE_2       = 0xA5;   ///< Second magic byte
static constexpr uint8_t MAX_PAYLOAD_LENGTH  = 0xF2;   ///< Max payload (242 bytes)
static constexpr uint8_t MAX_PACKET_DATA     = 0xF3;   ///< Max expected body (LEN+7); reject if larger (firmware @0x08007142)
static constexpr uint8_t HEADER_OVERHEAD     = 7;      ///< Body bytes beyond LEN value: LEN(1)+SRC+DST+CMD+ARG+CHK(2) → expected = LEN + 7
static constexpr uint8_t PACKET_BUF_SIZE     = 250;    ///< Per-slot buffer (0xFA)
static constexpr int     NUM_TX_SLOTS        = 4;      ///< Circular TX queue depth
static constexpr size_t  MAX_PACKET_SIZE     = 251;    ///< Max complete packet

/* =========================================================================
 * Device Addresses
 * ========================================================================= */

/** Bus addresses for devices on the Ninebot serial bus. */
enum class DevAddr : uint8_t {
    ESC  = 0x20,   ///< Electronic Speed Controller
    BLE  = 0x21,   ///< Bluetooth / Dashboard
    BMS  = 0x22,   ///< Battery Management System
    BMS2 = 0x23,   ///< External battery BMS
    APP  = 0x3E,   ///< Phone application (via BLE)
    PC   = 0x3F    ///< PC / debugger
};

/* =========================================================================
 * Command Types
 * ========================================================================= */

/** Protocol command byte values. */
enum class Cmd : uint8_t {
    READ          = 0x01,   ///< Read register(s)
    WRITE         = 0x02,   ///< Write register(s)
    READ_RESPONSE = 0x03,   ///< Response to read
    WRITE_ACK     = 0x05    ///< Write acknowledgment
};

/* =========================================================================
 * Parsed Packet
 * ========================================================================= */

/**
 * Parsed representation of a complete Ninebot protocol packet.
 *
 * Filled by the parser state machine after checksum verification.
 * All multi-byte values are little-endian (ARM native order).
 */
struct Packet {
    uint8_t  length;                       ///< LEN field (payload count)
    uint8_t  source;                       ///< Source device address
    uint8_t  destination;                  ///< Destination device address
    uint8_t  command;                      ///< Command type
    uint8_t  argument;                     ///< Register / sub-command
    uint8_t  payload[MAX_PAYLOAD_LENGTH];  ///< Payload data
    uint8_t  payloadLength;                ///< Actual payload byte count
    uint16_t checksum;                     ///< Received checksum

    /** Check if packet targets a specific device. */
    bool isFor(DevAddr addr) const { return destination == static_cast<uint8_t>(addr); }

    /** Check if packet originates from a specific device. */
    bool isFrom(DevAddr addr) const { return source == static_cast<uint8_t>(addr); }

    /** Read little-endian uint16_t from payload at offset. */
    uint16_t payloadU16(size_t off = 0) const {
        if (off + 1 >= payloadLength) return 0;
        return uint16_t(payload[off]) | (uint16_t(payload[off + 1]) << 8);
    }

    /** Read little-endian uint32_t from payload at offset. */
    uint32_t payloadU32(size_t off = 0) const {
        if (off + 3 >= payloadLength) return 0;
        return uint32_t(payload[off]) | (uint32_t(payload[off + 1]) << 8) |
               (uint32_t(payload[off + 2]) << 16) | (uint32_t(payload[off + 3]) << 24);
    }
};

/* =========================================================================
 * Checksum Calculation
 * =========================================================================
 * Exact reproduction of DRV_1.6.13 @ 0x08002720.
 *
 * Assembly:
 *   movs r3, #0          ; sum = 0
 *   movs r2, #0          ; i = 0
 * loop:
 *   ldrb r4, [r0, r2]    ; r4 = data[i]
 *   add  r3, r4           ; sum += data[i]
 *   uxth r3, r3           ; sum &= 0xFFFF
 *   adds r2, r2, #1       ; i++
 *   cmp  r2, r1           ; if (i < length) goto loop
 *   blo  loop
 *   mvns r0, r3           ; return ~sum
 *   uxth r0, r0           ; & 0xFFFF
 */
inline uint16_t calculateChecksum(const uint8_t* data, size_t length) {
    uint16_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return static_cast<uint16_t>(~sum);
}

/* =========================================================================
 * Packet Builder
 * =========================================================================
 * Exact reproduction of DRV_1.6.13 @ 0x080036AC.
 */
inline size_t buildPacket(uint8_t source, uint8_t destination,
                          uint8_t command, uint8_t argument,
                          const uint8_t* payload, uint8_t payloadLen,
                          uint8_t* buffer)
{
    size_t idx = 0;
    buffer[idx++] = HEADER_BYTE_1;   // 0x5A
    buffer[idx++] = HEADER_BYTE_2;   // 0xA5
    buffer[idx++] = payloadLen;      // LEN = payload byte count (firmware @0x080036BE)
    buffer[idx++] = source;          // SRC
    buffer[idx++] = destination;     // DST
    buffer[idx++] = command;         // CMD
    buffer[idx++] = argument;        // ARG

    if (payload && payloadLen > 0) {
        std::memcpy(&buffer[idx], payload, payloadLen);
        idx += payloadLen;
    }

    /* Checksum covers bytes [2..idx-1] (LEN through payload) */
    uint16_t chk = calculateChecksum(&buffer[2], idx - 2);
    buffer[idx++] = static_cast<uint8_t>(chk & 0xFF);
    buffer[idx++] = static_cast<uint8_t>((chk >> 8) & 0xFF);
    return idx;
}

/* =========================================================================
 * Packet Validation
 * ========================================================================= */

/** Validate a complete raw packet's checksum. */
inline bool validatePacket(const uint8_t* raw, size_t totalSize) {
    if (totalSize < 9) return false;
    if (raw[0] != HEADER_BYTE_1 || raw[1] != HEADER_BYTE_2) return false;
    uint16_t computed = calculateChecksum(&raw[2], totalSize - 4);
    uint16_t received = uint16_t(raw[totalSize - 2]) | (uint16_t(raw[totalSize - 1]) << 8);
    return computed == received;
}

/* =========================================================================
 * TX Buffer Slot
 * ========================================================================= */

/** Single TX buffer slot (256-byte aligned in firmware SRAM). */
struct TxSlot {
    uint32_t packetLength = 0;
    uint8_t  packetData[PACKET_BUF_SIZE] = {};
};

/* =========================================================================
 * Parser State Machine
 * =========================================================================
 * Memory layout from DRV_1.6.13 SRAM analysis:
 *   USART1 state @ 0x200003AC
 *   USART2 state @ 0x20000544
 *   USART3 state @ 0x20000394
 */

/**
 * Per-channel UART parser + TX queue state.
 *
 * Faithfully reproduces the firmware's struct layout. Each field offset
 * matches the original binary's register access patterns.
 */
struct ParserState {
    int8_t   currentTxSlot    = 0;            ///< [+0x00] TX read pointer
    int8_t   txWriteSlot      = 0;            ///< [+0x01] TX write pointer
    int8_t   pendingTxCount   = NUM_TX_SLOTS; ///< [+0x02] Available TX slots
    uint8_t  txBusy           = 0;            ///< [+0x03] TX lock
    uint8_t  txDraining       = 0;            ///< [+0x04] Drain flag
    uint8_t  rxActive         = 0;            ///< [+0x05] RX/TX mutex
    uint8_t  txEnabled        = 0;            ///< [+0x06] TX IRQ armed
    uint8_t  gotFirstHeader   = 0;            ///< [+0x07] Seen 0x5A
    uint8_t  receivingPacket  = 0;            ///< [+0x08] Body rx in progress
    uint8_t  rxByteIndex      = 0;            ///< [+0x09] RX buffer position
    uint8_t  expectedRxLength = 0;            ///< [+0x0A] Expected bytes
    uint16_t rxRunningChecksum= 0;            ///< [+0x0C] Checksum accumulator
    uint32_t txByteOffset     = 0;            ///< [+0x10] TX byte position

    void reset() {
        currentTxSlot = 0; txWriteSlot = 0;
        pendingTxCount = NUM_TX_SLOTS;
        txBusy = 0; txDraining = 0; rxActive = 0; txEnabled = 0;
        gotFirstHeader = 0; receivingPacket = 0; rxByteIndex = 0;
        expectedRxLength = 0; rxRunningChecksum = 0; txByteOffset = 0;
    }
};

/* =========================================================================
 * Protocol Channel Handler
 * =========================================================================
 * Combines parser state machine, TX queue, and UART hardware access
 * into a single reusable component. Each UART on each board instantiates
 * one of these.
 */

/**
 * Complete protocol channel: RX parser + TX queue + hardware interface.
 *
 * This is the core building block used by all three firmware images.
 * The ESC has 3 instances, BLE has 2, BMS has 1-2.
 *
 * @see DRV_1.6.13 parseProtocolByte  @ 0x08007128
 * @see DRV_1.6.13 enqueuePacket      @ 0x080071F4
 * @see DRV_1.6.13 uartTransmitHandler@ 0x08007610
 */
class ProtocolChannel {
public:
    using PacketCallback = std::function<void(const Packet&)>;

    explicit ProtocolChannel(hal::IUart* uart = nullptr) : uart_(uart) {
        state_.reset();
        std::memset(rxBuffer_, 0, sizeof(rxBuffer_));
    }

    /** Set callback invoked on valid received packet. */
    void setCallback(PacketCallback cb) { callback_ = std::move(cb); }

    /** Set UART hardware. */
    void setUart(hal::IUart* uart) { uart_ = uart; }

    /** Get parser state (for inspection/testing). */
    const ParserState& state() const { return state_; }

    /* ─── RX Path ─────────────────────────────────────────────────────
     * Exact reproduction of DRV_1.6.13 parseProtocolByte @ 0x08007128.
     *
     * State machine:
     *   IDLE → 0x5A → gotFirstHeader = 1
     *        → 0xA5 (if gotFirstHeader) → RECEIVING
     *   RECEIVING → accumulate bytes → checksum verify → dispatch
     */
    void receiveByte(uint8_t byte) {
        if (state_.receivingPacket) {
            rxBuffer_[state_.rxByteIndex] = byte;

            /* First byte = LEN: compute expected total */
            if (state_.rxByteIndex == 0) {
                state_.expectedRxLength = byte + HEADER_OVERHEAD;
                /* Reject oversized: firmware @ 0x08007142 */
                if (state_.expectedRxLength > MAX_PACKET_DATA) {
                    resetRx();
                    return;
                }
            }

            state_.rxByteIndex++;

            /* Packet complete? */
            if (state_.rxByteIndex == state_.expectedRxLength) {
                verifyAndDispatch();
                resetRx();
            } else {
                state_.rxRunningChecksum += byte;
            }
            return;
        }

        /* IDLE: header detection */
        if (byte == HEADER_BYTE_1) {
            if (!state_.gotFirstHeader) {
                state_.gotFirstHeader = 1;
                return;
            }
            /* 0x5A 0x5A: reset */
        } else if (byte == HEADER_BYTE_2) {
            if (state_.gotFirstHeader) {
                state_.receivingPacket = 1;
                state_.rxRunningChecksum = 0;
                state_.rxByteIndex = 0;
                return;
            }
        }
        resetRx();
    }

    /* ─── TX Path ─────────────────────────────────────────────────────
     * Exact reproduction of DRV_1.6.13 enqueuePacket @ 0x080071F4.
     */
    bool enqueuePacket(uint8_t source, uint8_t destination,
                       uint8_t command, uint8_t argument,
                       const uint8_t* payload = nullptr, uint8_t payloadLen = 0)
    {
        if (state_.txBusy || state_.rxActive) return false;
        state_.txBusy = 1;

        if (payloadLen > MAX_PAYLOAD_LENGTH) { state_.txBusy = 0; return false; }
        if (state_.pendingTxCount <= 0)      { state_.txBusy = 0; return false; }

        TxSlot& slot = txSlots_[state_.txWriteSlot];
        slot.packetLength = static_cast<uint32_t>(
            buildPacket(source, destination, command, argument,
                        payload, payloadLen, slot.packetData));

        state_.txWriteSlot++;
        if (state_.txWriteSlot >= NUM_TX_SLOTS) state_.txWriteSlot = 0;
        state_.pendingTxCount--;
        state_.txBusy = 0;
        return true;
    }

    /** Enqueue using typed enums. */
    bool enqueuePacket(DevAddr src, DevAddr dst, Cmd cmd, uint8_t arg,
                       const uint8_t* payload = nullptr, uint8_t payloadLen = 0)
    {
        return enqueuePacket(uint8_t(src), uint8_t(dst), uint8_t(cmd), arg,
                             payload, payloadLen);
    }

    /* ─── TX Handler ──────────────────────────────────────────────────
     * Exact reproduction of DRV_1.6.13 uartTransmitHandler @ 0x08007610.
     * Sends one byte per call from the current TX slot.
     */
    bool transmitHandler() {
        if (state_.txBusy || state_.rxActive) return false;
        state_.rxActive = 1;

        if (uart_ && !uart_->isTxEmpty()) { state_.rxActive = 0; return false; }

        if (state_.txWriteSlot == state_.currentTxSlot) {
            if (state_.txEnabled) state_.txEnabled = 0;
            state_.rxActive = 0;
            return false;
        }

        if (!state_.txEnabled) state_.txEnabled = 1;

        TxSlot& slot = txSlots_[state_.currentTxSlot];
        uint8_t b = slot.packetData[state_.txByteOffset];
        if (uart_) uart_->sendByte(b);
        lastTxByte_ = b;
        state_.txByteOffset++;

        if (state_.txByteOffset >= slot.packetLength) {
            state_.txByteOffset = 0;
            state_.currentTxSlot++;
            if (state_.currentTxSlot >= NUM_TX_SLOTS) state_.currentTxSlot = 0;
            state_.pendingTxCount++;
        }

        state_.rxActive = 0;
        return true;
    }

    /** Drain all queued TX packets. Returns total bytes sent. */
    size_t drainTx() {
        size_t count = 0;
        while (transmitHandler()) ++count;
        return count;
    }

    /** Number of packets queued for TX. */
    int txQueued() const { return NUM_TX_SLOTS - state_.pendingTxCount; }

    /** Full reset. */
    void reset() {
        state_.reset();
        std::memset(rxBuffer_, 0, sizeof(rxBuffer_));
        for (auto& s : txSlots_) s = TxSlot();
        lastTxByte_ = 0;
    }

    /** Last TX byte (for testing). */
    uint8_t lastTxByte() const { return lastTxByte_; }

private:
    /**
     * Verify checksum and dispatch packet.
     * Reproduces DRV_1.6.13 @ 0x0800715E-0x0800717C.
     */
    void verifyAndDispatch() {
        uint8_t chkLo = rxBuffer_[state_.expectedRxLength - 2];
        uint8_t chkHi = rxBuffer_[state_.expectedRxLength - 1];
        uint16_t calc = uint16_t(~(state_.rxRunningChecksum - chkLo) & 0xFFFF);
        uint16_t recv = uint16_t(chkLo | (chkHi << 8));

        if (calc == recv && callback_) {
            Packet pkt{};
            pkt.length        = rxBuffer_[0];
            pkt.source        = rxBuffer_[1];
            pkt.destination   = rxBuffer_[2];
            pkt.command       = rxBuffer_[3];
            pkt.argument      = rxBuffer_[4];
            pkt.payloadLength = (pkt.length <= MAX_PAYLOAD_LENGTH) ? pkt.length : 0;
            pkt.checksum      = recv;
            if (pkt.payloadLength > 0 && pkt.payloadLength <= MAX_PAYLOAD_LENGTH) {
                std::memcpy(pkt.payload, &rxBuffer_[5], pkt.payloadLength);
            }
            callback_(pkt);
        }
    }

    void resetRx() {
        state_.receivingPacket = 0;
        state_.gotFirstHeader = 0;
        state_.rxByteIndex = 0;
        state_.rxRunningChecksum = 0;
    }

    hal::IUart*   uart_ = nullptr;
    ParserState   state_;
    TxSlot        txSlots_[NUM_TX_SLOTS];
    uint8_t       rxBuffer_[256] = {};
    uint8_t       lastTxByte_ = 0;
    PacketCallback callback_;
};


} // namespace ninebot

#endif // NINEBOT_PROTOCOL_COMMON_H
