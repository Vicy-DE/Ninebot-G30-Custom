/**
 * @file uart.h
 * @brief UART transport layer for Ninebot protocol communication.
 *
 * Reconstructed from DRV_1.6.13 firmware disassembly (STM32F103CBT6).
 *
 * This module implements the byte-level UART parser state machine and
 * the TX circular buffer system exactly as found in the stock firmware.
 * It is designed to be portable: hardware register access is abstracted
 * through a virtual interface (UartHardware) that can be backed by real
 * STM32 peripherals or by a software simulation.
 *
 * Architecture (per UART channel):
 * @code
 *   ┌────────────────────────────────────────────────────────┐
 *   │  RX path                                               │
 *   │  UART byte ──> parseProtocolByte() ──> state machine   │
 *   │                                         │              │
 *   │                                  checksum OK?          │
 *   │                                    yes │               │
 *   │                              dispatchPacket()          │
 *   │                                    │                   │
 *   │                          callback to application       │
 *   ├────────────────────────────────────────────────────────┤
 *   │  TX path                                               │
 *   │  enqueuePacket() ──> TxSlot[0..3] circular buffer      │
 *   │                              │                         │
 *   │                     transmitHandler()                  │
 *   │                              │                         │
 *   │                     UartHardware::sendByte()            │
 *   └────────────────────────────────────────────────────────┘
 * @endcode
 *
 * @see DRV_1.6.13 parseProtocolByte @ 0x08007128 (USART1)
 * @see DRV_1.6.13 enqueuePacket     @ 0x080071F4 (USART1)
 * @see DRV_1.6.13 uartTransmitHandler @ 0x08007610 (USART1)
 */

#ifndef NINEBOT_UART_H
#define NINEBOT_UART_H

#include "ninebot/protocol.h"
#include <functional>
#include <cstring>

namespace ninebot {

/* ===========================================================================
 * UART Channel Identifiers
 * ===========================================================================
 */

/**
 * Identifies one of the three UART channels on the ESC board.
 *
 * The DRV firmware instantiates three identical protocol parsers,
 * one per USART peripheral:
 *   - CHANNEL_BLE: USART2 (PA2/PA3), connects to BLE dashboard
 *   - CHANNEL_EXT: USART1 (PA9/PA10), external/debug port
 *   - CHANNEL_BMS: USART3 (PB10/PB11), connects to BMS board
 */
enum class UartChannel : uint8_t {
    CHANNEL_BLE = 0,  ///< USART2 -> BLE dashboard
    CHANNEL_EXT = 1,  ///< USART1 -> External / debug
    CHANNEL_BMS = 2   ///< USART3 -> BMS battery board
};

/** Number of UART channels on the ESC board. */
static constexpr int NUM_CHANNELS = 3;


/* ===========================================================================
 * TX Buffer Structures
 * ===========================================================================
 */

/** Number of TX buffer slots per channel (circular, from firmware analysis). */
static constexpr int NUM_TX_SLOTS = 4;

/** Size of each TX buffer slot data area (250 bytes). */
static constexpr size_t TX_SLOT_DATA_SIZE = 250;

/**
 * A single TX buffer slot.
 *
 * The firmware stores the packet length in the first 4 bytes,
 * followed by the raw packet data.  Four of these form a circular
 * queue per UART channel.
 *
 * @see DRV_1.6.13: txBuffers accessed via base + (slot_index << 8)
 */
struct TxSlot {
    uint32_t packetLength;                    ///< Bytes valid in packetData[]
    uint8_t  packetData[TX_SLOT_DATA_SIZE];   ///< Raw packet bytes (including header)

    TxSlot() : packetLength(0) { std::memset(packetData, 0, sizeof(packetData)); }
};


/* ===========================================================================
 * Parser State Machine
 * ===========================================================================
 *
 * Mirrors the UartParserState struct in SRAM at offsets documented in
 * ninebot_protocol_reconstructed.cpp.  The struct layout matches the
 * firmware's field offsets exactly.
 */

/**
 * Per-channel protocol parser and TX state.
 *
 * Memory layout from firmware analysis:
 * @code
 *   Offset  Type      Field
 *   0x00    int8_t    currentTxSlot      - Read pointer for TX ring
 *   0x01    int8_t    txWriteSlot        - Write pointer for TX ring
 *   0x02    int8_t    pendingTxCount     - Available TX slots (starts at 4)
 *   0x03    uint8_t   txBusy             - TX operation in progress
 *   0x04    uint8_t   txDraining         - Drain-complete flag
 *   0x05    uint8_t   rxActive           - RX/TX mutex flag
 *   0x06    uint8_t   txEnabled          - TX interrupt armed
 *   0x07    uint8_t   gotFirstHeader     - Seen 0x5A, awaiting 0xA5
 *   0x08    uint8_t   receivingPacket    - Body reception in progress
 *   0x09    uint8_t   rxByteIndex        - Current RX buffer write position
 *   0x0A    uint8_t   expectedRxLength   - Total expected bytes after header
 *   0x0C    uint16_t  rxRunningChecksum  - Accumulated checksum value
 *   0x10    uint32_t  txByteOffset       - TX byte within current slot
 *   0x14    uint32_t  rxByteOffset       - Supplementary RX offset
 * @endcode
 */
struct ParserState {
    /* TX ring buffer control */
    int8_t   currentTxSlot    = 0;   ///< Slot currently being transmitted
    int8_t   txWriteSlot      = 0;   ///< Next slot to write into
    int8_t   pendingTxCount   = NUM_TX_SLOTS; ///< Available slots (counts down)

    /* Flags (mutex / state) */
    uint8_t  txBusy           = 0;   ///< TX build in progress (mutex)
    uint8_t  txDraining       = 0;   ///< Draining completion flag
    uint8_t  rxActive         = 0;   ///< RX processing active (mutex)
    uint8_t  txEnabled        = 0;   ///< TX interrupt enabled

    /* RX state machine */
    uint8_t  gotFirstHeader   = 0;   ///< Received 0x5A, waiting for 0xA5
    uint8_t  receivingPacket  = 0;   ///< Currently receiving packet body
    uint8_t  rxByteIndex      = 0;   ///< Next write position in rxBuffer[]
    uint8_t  expectedRxLength = 0;   ///< Total expected data bytes

    /* Checksum accumulator */
    uint16_t rxRunningChecksum= 0;   ///< Running sum for checksum verification

    /* TX byte-level position */
    uint32_t txByteOffset     = 0;   ///< Byte offset within current TX slot

    /** Reset all parser state to initial values. */
    void reset() {
        currentTxSlot     = 0;
        txWriteSlot       = 0;
        pendingTxCount    = NUM_TX_SLOTS;
        txBusy            = 0;
        txDraining        = 0;
        rxActive          = 0;
        txEnabled         = 0;
        gotFirstHeader    = 0;
        receivingPacket   = 0;
        rxByteIndex       = 0;
        expectedRxLength  = 0;
        rxRunningChecksum = 0;
        txByteOffset      = 0;
    }
};


/* ===========================================================================
 * Hardware Abstraction Interface
 * ===========================================================================
 */

/**
 * Abstract interface for UART hardware operations.
 *
 * Implement this for real STM32 peripherals or for simulation/testing.
 * The protocol layer calls sendByte() when a TX byte is ready,
 * and the interrupt handler calls receiveByte() feeding into the parser.
 */
class UartHardware {
public:
    virtual ~UartHardware() = default;

    /**
     * Write one byte to the UART data register (triggers physical TX).
     * On STM32: writes to USART_DR register.
     */
    virtual void sendByte(uint8_t byte) = 0;

    /**
     * Check if the TX data register is empty (ready for next byte).
     * On STM32: reads USART_SR TXE bit.
     */
    virtual bool isTxReady() const = 0;

    /**
     * Check if transmission is fully complete (shift register empty).
     * On STM32: reads USART_SR TC bit.
     */
    virtual bool isTxComplete() const = 0;
};


/* ===========================================================================
 * UART Channel Class
 * ===========================================================================
 */

/**
 * Complete UART channel with protocol parser and TX queue.
 *
 * This class encapsulates one instance of the Ninebot protocol handler,
 * faithfully reproducing the firmware's state machine for a single USART.
 *
 * Usage:
 * @code
 *   SimulatedUart hw;
 *   ninebot::UartChannelHandler ch(ninebot::UartChannel::CHANNEL_BLE, &hw);
 *   ch.setPacketCallback([](const ninebot::Packet& pkt) {
 *       // Handle received packet
 *   });
 *
 *   // Feed incoming bytes (from ISR or polling):
 *   ch.receiveByte(0x5A);
 *   ch.receiveByte(0xA5);
 *   // ... etc
 *
 *   // Enqueue outgoing packet:
 *   ch.enqueuePacket(src, dst, cmd, arg, payload, len);
 *
 *   // Pump TX from main loop or ISR:
 *   ch.transmitHandler();
 * @endcode
 */
class UartChannelHandler {
public:
    /** Callback type invoked when a valid packet is received. */
    using PacketCallback = std::function<void(const Packet&)>;

    /**
     * Construct a UART channel handler.
     * @param channel  Channel identifier.
     * @param hardware Pointer to hardware abstraction (may be nullptr for test).
     */
    UartChannelHandler(UartChannel channel, UartHardware* hardware = nullptr)
        : channel_(channel), hardware_(hardware)
    {
        state_.reset();
        std::memset(rxBuffer_, 0, sizeof(rxBuffer_));
    }

    /* ---- Configuration ---- */

    /** Set the callback invoked when a complete, checksum-verified packet arrives. */
    void setPacketCallback(PacketCallback cb) { packetCallback_ = std::move(cb); }

    /** Set or replace the hardware backend. */
    void setHardware(UartHardware* hw) { hardware_ = hw; }

    /** @return the channel identifier. */
    UartChannel channel() const { return channel_; }

    /** @return const reference to the internal parser state (for inspection). */
    const ParserState& state() const { return state_; }

    /* ---- RX Path ---- */

    /**
     * Feed one received byte into the protocol parser state machine.
     *
     * Faithfully reproduces DRV_1.6.13 parseProtocolByte @ 0x08007128.
     *
     * State machine:
     *   IDLE -> receive 0x5A -> set gotFirstHeader
     *        -> receive 0xA5 (if gotFirstHeader) -> RECEIVING
     *   RECEIVING -> accumulate bytes, verify checksum on completion
     *            -> dispatch valid packet via callback
     *
     * @param byte The byte just read from USART_DR.
     */
    void receiveByte(uint8_t byte) {
        /* ---- RECEIVING STATE ---- */
        if (state_.receivingPacket) {
            rxBuffer_[state_.rxByteIndex] = byte;

            /* First byte (index 0) is LEN: compute total expected length */
            if (state_.rxByteIndex == 0) {
                state_.expectedRxLength = byte + HEADER_OVERHEAD;

                /* Reject oversized packets (firmware check @ 0x08007142) */
                if (state_.expectedRxLength > MAX_PACKET_DATA_SIZE) {
                    resetRxState();
                    return;
                }
            }

            state_.rxByteIndex++;

            /* Check if packet is complete */
            if (state_.rxByteIndex == state_.expectedRxLength) {
                verifyAndDispatch();
                resetRxState();
            } else {
                /* Accumulate byte into running checksum */
                state_.rxRunningChecksum += byte;
            }
            return;
        }

        /* ---- IDLE STATE: Header detection ---- */
        if (byte == HEADER_BYTE_1) {
            if (!state_.gotFirstHeader) {
                state_.gotFirstHeader = 1;
                return;
            }
            /* 0x5A 0x5A: keep waiting (re-sync) */
        } else if (byte == HEADER_BYTE_2) {
            if (state_.gotFirstHeader) {
                /* 0x5A 0xA5 -> transition to RECEIVING */
                state_.receivingPacket   = 1;
                state_.rxRunningChecksum = 0;
                state_.rxByteIndex       = 0;
                return;
            }
        }

        /* Unexpected byte or incomplete header -> reset */
        resetRxState();
    }

    /* ---- TX Path ---- */

    /**
     * Enqueue a packet for transmission on this channel.
     *
     * Reproduces DRV_1.6.13 enqueuePacket @ 0x080071F4.
     * Builds the packet into the next available TX slot.
     * Silently drops the packet if the queue is full or payload is too large.
     *
     * @param source      Source device address.
     * @param destination Destination device address.
     * @param command     Command byte.
     * @param argument    Register / argument byte.
     * @param payload     Payload data (may be nullptr).
     * @param payloadLen  Number of payload bytes.
     * @return true if the packet was successfully queued.
     */
    bool enqueuePacket(uint8_t source, uint8_t destination,
                       uint8_t command, uint8_t argument,
                       const uint8_t* payload, uint8_t payloadLen)
    {
        /* Acquire TX lock (firmware checks txBusy and rxActive) */
        if (state_.txBusy || state_.rxActive) return false;
        state_.txBusy = 1;

        /* Validate payload size */
        if (payloadLen > MAX_PAYLOAD_LENGTH) {
            state_.txBusy = 0;
            return false;
        }

        /* Check queue capacity (pendingTxCount counts available slots) */
        if (state_.pendingTxCount <= 0) {
            state_.txBusy = 0;
            return false;
        }

        /* Build packet into the next TX slot */
        TxSlot& slot = txSlots_[state_.txWriteSlot];
        slot.packetLength = static_cast<uint32_t>(
            buildPacket(source, destination, command, argument,
                        payload, payloadLen, slot.packetData)
        );

        /* Advance write pointer (circular 0..3) */
        state_.txWriteSlot++;
        if (state_.txWriteSlot >= NUM_TX_SLOTS) {
            state_.txWriteSlot = 0;
        }

        /* Consume one slot */
        state_.pendingTxCount--;

        /* Release lock */
        state_.txBusy = 0;
        return true;
    }

    /**
     * Convenience: enqueue packet using enum types.
     */
    bool enqueuePacket(DeviceAddress source, DeviceAddress destination,
                       Command command, uint8_t argument,
                       const uint8_t* payload = nullptr, uint8_t payloadLen = 0)
    {
        return enqueuePacket(static_cast<uint8_t>(source),
                             static_cast<uint8_t>(destination),
                             static_cast<uint8_t>(command), argument,
                             payload, payloadLen);
    }

    /**
     * TX handler — sends one byte per call from the current TX slot.
     *
     * Reproduces DRV_1.6.13 uartTransmitHandler @ 0x08007610.
     * Call this from the USART TX interrupt or periodically from the main loop.
     *
     * @return true if a byte was transmitted, false if idle or busy.
     */
    bool transmitHandler() {
        /* Check mutex */
        if (state_.txBusy || state_.rxActive) return false;
        state_.rxActive = 1;  // Firmware uses rxActive as TX guard

        /* Check hardware readiness */
        if (hardware_ && !hardware_->isTxReady()) {
            state_.rxActive = 0;
            return false;
        }

        /* Check if all packets have been sent */
        if (state_.txWriteSlot == state_.currentTxSlot) {
            /* All done — check transmission complete */
            if (hardware_ && hardware_->isTxComplete() && state_.txEnabled) {
                state_.txEnabled = 0;
            }
            state_.rxActive = 0;
            return false;
        }

        /* Enable TX if not already */
        if (!state_.txEnabled) {
            state_.txEnabled = 1;
        }

        /* Send next byte from current slot */
        TxSlot& slot = txSlots_[state_.currentTxSlot];
        uint8_t byteToSend = slot.packetData[state_.txByteOffset];

        if (hardware_) {
            hardware_->sendByte(byteToSend);
        }
        lastTxByte_ = byteToSend;  // Record for testing

        state_.txByteOffset++;

        /* Check if this slot is fully transmitted */
        if (state_.txByteOffset >= slot.packetLength) {
            state_.txByteOffset = 0;

            /* Advance to next slot (circular) */
            state_.currentTxSlot++;
            if (state_.currentTxSlot >= NUM_TX_SLOTS) {
                state_.currentTxSlot = 0;
            }

            /* Return slot to available pool */
            state_.pendingTxCount++;
        }

        state_.rxActive = 0;
        return true;
    }

    /**
     * Drain all queued TX packets by calling transmitHandler() repeatedly.
     * Useful for simulation / testing.
     *
     * @return Total number of bytes transmitted.
     */
    size_t drainTx() {
        size_t count = 0;
        while (transmitHandler()) {
            ++count;
        }
        return count;
    }

    /** @return the last byte sent by transmitHandler() (for testing). */
    uint8_t lastTransmittedByte() const { return lastTxByte_; }

    /** @return number of packets currently queued for TX. */
    int txQueuedCount() const { return NUM_TX_SLOTS - state_.pendingTxCount; }

    /** @return read-only access to a TX slot (for inspection). */
    const TxSlot& txSlot(int idx) const { return txSlots_[idx]; }

    /** Full reset of channel state (parser + TX queue). */
    void reset() {
        state_.reset();
        std::memset(rxBuffer_, 0, sizeof(rxBuffer_));
        for (auto& s : txSlots_) { s = TxSlot(); }
        lastTxByte_ = 0;
    }

private:
    /**
     * Verify the received packet's checksum and dispatch if valid.
     *
     * The firmware computes: calculated = ~(runningSum - chkLo) & 0xFFFF
     * and compares against: received = chkLo | (chkHi << 8).
     *
     * @see DRV_1.6.13 @ 0x0800715E - 0x0800717C
     */
    void verifyAndDispatch() {
        uint8_t chkLo = rxBuffer_[state_.expectedRxLength - 2];
        uint8_t chkHi = rxBuffer_[state_.expectedRxLength - 1];

        uint16_t calculated = static_cast<uint16_t>(
            ~(state_.rxRunningChecksum - chkLo) & 0xFFFF
        );
        uint16_t received = static_cast<uint16_t>(chkLo | (chkHi << 8));

        if (calculated == received && packetCallback_) {
            /* Parse the buffer into a Packet struct */
            Packet pkt{};
            pkt.length       = rxBuffer_[0];
            pkt.source       = rxBuffer_[1];
            pkt.destination  = rxBuffer_[2];
            pkt.command      = rxBuffer_[3];
            pkt.argument     = rxBuffer_[4];
            pkt.payloadLength= pkt.length;
            pkt.checksum     = received;

            if (pkt.payloadLength > 0 && pkt.payloadLength <= MAX_PAYLOAD_LENGTH) {
                std::memcpy(pkt.payload, &rxBuffer_[5], pkt.payloadLength);
            }

            packetCallback_(pkt);
        }
    }

    /** Reset RX parser state to IDLE. */
    void resetRxState() {
        state_.receivingPacket   = 0;
        state_.gotFirstHeader    = 0;
        state_.rxByteIndex       = 0;
        state_.rxRunningChecksum = 0;
    }

    UartChannel     channel_;
    UartHardware*   hardware_ = nullptr;
    ParserState     state_;
    TxSlot          txSlots_[NUM_TX_SLOTS];
    uint8_t         rxBuffer_[256] = {};
    uint8_t         lastTxByte_    = 0;
    PacketCallback  packetCallback_;
};


} // namespace ninebot

#endif // NINEBOT_UART_H
