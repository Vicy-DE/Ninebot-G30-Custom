/**
 * @file protocol.h
 * @brief Ninebot G30 Max serial protocol - packet format and checksum.
 *
 * Reconstructed from DRV_1.6.13 firmware disassembly (STM32F103CBT6).
 * This header defines the wire format used between all boards
 * (ESC <-> BLE <-> BMS) on the Ninebot serial bus.
 *
 * Packet layout:
 * @code
 *   [0x5A] [0xA5] [LEN] [SRC] [DST] [CMD] [ARG] [PAYLOAD...] [CHK_LO] [CHK_HI]
 *    ──┬──  ──┬──  ─┬──  ─┬──  ─┬──  ─┬──  ─┬──  ─────┬─────  ────┬────────┬────
 *    magic  magic  len  source dest  cmd  arg   0..242    checksum (LE)
 * @endcode
 *
 * UART physical layer: 115200 baud, 8N1, half-duplex (ESC<->BLE),
 * full-duplex (ESC<->BMS).
 *
 * @note  This is a research/documentation project reverse-engineered from
 *        stock firmware binaries. See safety warnings in project README.
 *
 * @see   DRV_1.6.13 calculateChecksum @ 0x08002720
 * @see   DRV_1.6.13 buildPacket       @ 0x080036AC
 */

#ifndef NINEBOT_PROTOCOL_H
#define NINEBOT_PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ninebot {

/* ===========================================================================
 * Protocol Constants
 * ===========================================================================
 * From firmware literal pool analysis (DRV_1.6.13).
 */

/** Magic header bytes that begin every Ninebot packet. */
static constexpr uint8_t HEADER_BYTE_1       = 0x5A;
static constexpr uint8_t HEADER_BYTE_2       = 0xA5;

/** Maximum payload length in a single packet (242 bytes). */
static constexpr uint8_t MAX_PAYLOAD_LENGTH   = 0xF2;

/**
 * Maximum post-header data size (243 = 0xF3).
 * The parser rejects any packet whose (LEN + 7) exceeds this value.
 * See parseProtocolByte() length validation @ 0x08007142.
 */
static constexpr uint8_t MAX_PACKET_DATA_SIZE = 0xF3;

/** Fixed overhead added to LEN to get the total RX byte count.
 *  LEN field counts ONLY the payload bytes (firmware convention).
 *  After header 0x5A 0xA5, bytes received =
 *    1(LEN) + 4(SRC+DST+CMD+ARG) + LEN(payload) + 2(checksum) = LEN + 7.
 *  Parser uses: expectedRxLength = LEN + 7.
 *  @see DRV_1.6.13 parseProtocolByte @ 0x0800713C: adds r2, r0, #7
 */
static constexpr uint8_t HEADER_OVERHEAD      = 7;

/** Maximum total packet size (header + LEN + MAX_PAYLOAD + checksum). */
static constexpr size_t  MAX_PACKET_SIZE      = 2 + 1 + 4 + MAX_PAYLOAD_LENGTH + 2; // 251


/* ===========================================================================
 * Device Addresses
 * ===========================================================================
 * From protocol.md and firmware address comparisons.
 */

/** Bus addresses for all known devices on the Ninebot serial bus. */
enum class DeviceAddress : uint8_t {
    ESC  = 0x20,  ///< Electronic Speed Controller (motor controller)
    BLE  = 0x21,  ///< Bluetooth / Dashboard board
    BMS  = 0x22,  ///< Battery Management System
    BMS2 = 0x23,  ///< External (second) battery BMS
    APP  = 0x3E,  ///< Phone application (via BLE)
    PC   = 0x3F   ///< PC / debugger (via serial adapter)
};


/* ===========================================================================
 * Command Types
 * ===========================================================================
 * From protocol register read/write pairs in firmware dispatch table.
 */

/** Ninebot protocol command byte values. */
enum class Command : uint8_t {
    READ          = 0x01,  ///< Read register(s) from target
    WRITE         = 0x02,  ///< Write register(s) to target
    READ_RESPONSE = 0x03,  ///< Response to a read command (unsolicited)
    WRITE_RESPONSE= 0x05   ///< Acknowledgment of a write command
};


/* ===========================================================================
 * Packet Structure
 * ===========================================================================
 */

/**
 * Parsed representation of a Ninebot protocol packet.
 *
 * This structure holds all fields extracted from the wire format after
 * the 0x5A 0xA5 header. The parser fills this when a complete,
 * checksum-verified packet arrives.
 */
struct Packet {
    uint8_t  length;                            ///< LEN field (payload byte count only)
    uint8_t  source;                            ///< Source device address
    uint8_t  destination;                       ///< Destination device address
    uint8_t  command;                           ///< Command type (see Command enum)
    uint8_t  argument;                          ///< Register / sub-command
    uint8_t  payload[MAX_PAYLOAD_LENGTH];       ///< Payload data
    uint8_t  payloadLength;                     ///< Actual payload byte count
    uint16_t checksum;                          ///< Received (or computed) checksum

    /** @return true if the packet targets the given device address. */
    bool isFor(DeviceAddress addr) const {
        return destination == static_cast<uint8_t>(addr);
    }

    /** @return true if the packet originated from the given device. */
    bool isFrom(DeviceAddress addr) const {
        return source == static_cast<uint8_t>(addr);
    }

    /** @return payload interpreted as a little-endian uint16_t at offset. */
    uint16_t payloadU16(size_t offset = 0) const {
        if (offset + 1 >= payloadLength) return 0;
        return static_cast<uint16_t>(payload[offset]) |
               (static_cast<uint16_t>(payload[offset + 1]) << 8);
    }

    /** @return payload interpreted as a little-endian uint32_t at offset. */
    uint32_t payloadU32(size_t offset = 0) const {
        if (offset + 3 >= payloadLength) return 0;
        return static_cast<uint32_t>(payload[offset]) |
               (static_cast<uint32_t>(payload[offset + 1]) << 8) |
               (static_cast<uint32_t>(payload[offset + 2]) << 16) |
               (static_cast<uint32_t>(payload[offset + 3]) << 24);
    }
};


/* ===========================================================================
 * Checksum
 * ===========================================================================
 */

/**
 * Calculate the Ninebot protocol checksum.
 *
 * Sums all bytes in the given range, then returns the bitwise complement
 * masked to 16 bits.  This matches the firmware implementation at
 * DRV_1.6.13 @ 0x08002720:
 *
 * @code{.asm}
 *   movs   r3, #0          ; sum = 0
 *   movs   r2, #0          ; i   = 0
 * loop:
 *   ldrb   r4, [r0, r2]    ; r4  = data[i]
 *   add    r3, r4           ; sum += data[i]
 *   uxth   r3, r3           ; sum &= 0xFFFF
 *   adds   r2, r2, #1       ; i++
 *   cmp    r2, r1           ; if (i < length) goto loop
 *   blo    loop
 *   mvns   r0, r3           ; return ~sum
 *   uxth   r0, r0
 * @endcode
 *
 * @param data   Pointer to start of data (typically &packet[2], the LEN field).
 * @param length Number of bytes to sum.
 * @return       ~(sum of bytes) & 0xFFFF
 */
inline uint16_t calculateChecksum(const uint8_t* data, size_t length) {
    uint16_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return static_cast<uint16_t>(~sum);
}


/* ===========================================================================
 * Packet Builder
 * ===========================================================================
 */

/**
 * Build a complete Ninebot protocol packet in the output buffer.
 *
 * Reconstructed from DRV_1.6.13 buildPacket @ 0x080036AC.
 * Writes header, addresses, command/argument, copies payload,
 * and appends the 16-bit checksum in little-endian.
 *
 * @param source        Source device address (e.g., DeviceAddress::ESC).
 * @param destination   Destination device address.
 * @param command       Command byte (see Command enum).
 * @param argument      Register / argument byte.
 * @param payload       Pointer to payload data (may be nullptr if payloadLen == 0).
 * @param payloadLen    Number of payload bytes (0..242).
 * @param[out] buffer   Output buffer; must hold at least (payloadLen + 9) bytes.
 *
 * @return Total number of bytes written to buffer (= payloadLen + 9).
 */
inline size_t buildPacket(uint8_t source, uint8_t destination,
                          uint8_t command, uint8_t argument,
                          const uint8_t* payload, uint8_t payloadLen,
                          uint8_t* buffer)
{
    size_t idx = 0;

    /* Magic header (not included in checksum) */
    buffer[idx++] = HEADER_BYTE_1;  // 0x5A
    buffer[idx++] = HEADER_BYTE_2;  // 0xA5

    /* LEN field: payload byte count only (firmware convention).
     * @see DRV_1.6.13 buildPacket @ 0x080036BE: strb r2, [r4, #2] */
    buffer[idx++] = payloadLen;

    /* Address and command fields */
    buffer[idx++] = source;
    buffer[idx++] = destination;
    buffer[idx++] = command;
    buffer[idx++] = argument;

    /* Payload data */
    if (payload && payloadLen > 0) {
        std::memcpy(&buffer[idx], payload, payloadLen);
        idx += payloadLen;
    }

    /* Checksum calculated over bytes [2..idx-1] (LEN through end of payload) */
    size_t checksumLen = idx - 2;  // everything after the 2-byte header
    uint16_t chk = calculateChecksum(&buffer[2], checksumLen);

    /* Append checksum in little-endian */
    buffer[idx++] = static_cast<uint8_t>(chk & 0xFF);
    buffer[idx++] = static_cast<uint8_t>((chk >> 8) & 0xFF);

    return idx;
}


/**
 * Build a Packet struct from field values (convenience wrapper).
 *
 * @param source      Source device address.
 * @param destination Destination device address.
 * @param command     Command byte.
 * @param argument    Register byte.
 * @param payload     Payload data pointer (may be nullptr).
 * @param payloadLen  Number of payload bytes.
 * @return Populated Packet struct with computed checksum.
 */
inline Packet makePacket(DeviceAddress source, DeviceAddress destination,
                         Command command, uint8_t argument,
                         const uint8_t* payload = nullptr,
                         uint8_t payloadLen = 0)
{
    Packet pkt{};
    pkt.source       = static_cast<uint8_t>(source);
    pkt.destination  = static_cast<uint8_t>(destination);
    pkt.command      = static_cast<uint8_t>(command);
    pkt.argument     = argument;
    pkt.payloadLength= payloadLen;
    pkt.length       = payloadLen;

    if (payload && payloadLen > 0) {
        std::memcpy(pkt.payload, payload, payloadLen);
    }

    /* Compute the checksum the same way the firmware does */
    uint8_t tmp[MAX_PACKET_SIZE];
    size_t totalSize = buildPacket(pkt.source, pkt.destination, pkt.command,
                                   pkt.argument, payload, payloadLen, tmp);
    pkt.checksum = static_cast<uint16_t>(tmp[totalSize - 2]) |
                   (static_cast<uint16_t>(tmp[totalSize - 1]) << 8);
    return pkt;
}


/**
 * Validate the checksum of a raw packet buffer.
 *
 * @param rawPacket   Pointer to complete packet starting with 0x5A 0xA5.
 * @param totalSize   Total number of bytes in the packet.
 * @return true if the embedded checksum matches the computed value.
 */
inline bool validatePacket(const uint8_t* rawPacket, size_t totalSize) {
    if (totalSize < 9) return false;  // Minimum: header(2) + len(1) + fields(4) + chk(2)
    if (rawPacket[0] != HEADER_BYTE_1 || rawPacket[1] != HEADER_BYTE_2) return false;

    size_t checksumDataLen = totalSize - 4;  // Exclude header(2) and checksum(2)
    uint16_t computed = calculateChecksum(&rawPacket[2], checksumDataLen);

    uint16_t received = static_cast<uint16_t>(rawPacket[totalSize - 2]) |
                        (static_cast<uint16_t>(rawPacket[totalSize - 1]) << 8);

    return computed == received;
}


} // namespace ninebot

#endif // NINEBOT_PROTOCOL_H
