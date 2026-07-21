/**
 * @file vesc_tunnel.h
 * @brief VESC binary packet framing + CRC for the second NUS ("VESC App")
 *        service (Req 3, Req 10). Header-only, host-testable.
 *
 * VESC Tool mobile talks the native VESC packet protocol. The nRF51 exposes a
 * second Nordic UART Service whose RX/TX characteristics tunnel these packets
 * verbatim to/from the VESC UART. This module only does the **framing + CRC**;
 * the SoftDevice glue lives in `nrf51_main`/`vesc_nus`.
 *
 * VESC frame (vedderb/bldc `packet.c`):
 * @code
 *   short payload (len ≤ 255):  0x02 | len(u8)  | payload | CRC16(payload) BE | 0x03
 *   long  payload (len  > 255): 0x03 | len(u16 BE) | payload | CRC16(payload) BE | 0x03
 * @endcode
 * CRC16 = CCITT/XMODEM (poly 0x1021, init 0x0000) over the payload only.
 */
#ifndef NINEBOT_VESC_TUNNEL_H
#define NINEBOT_VESC_TUNNEL_H

#include <cstdint>
#include <cstddef>

namespace ninebot {
namespace nrf51 {
namespace vesc {

static constexpr uint8_t SOF_SHORT = 0x02;  ///< start, 8-bit length
static constexpr uint8_t SOF_LONG  = 0x03;  ///< start, 16-bit length
static constexpr uint8_t EOF_BYTE  = 0x03;  ///< end-of-frame

/** CRC16-CCITT/XMODEM (poly 0x1021, init 0x0000), as used by VESC packet.c. */
inline uint16_t crc16(const uint8_t* data, size_t len) {
    static const uint16_t tab[16] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
        0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
    };
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc = uint16_t((crc << 4) ^ tab[((crc >> 12) ^ (data[i] >> 4)) & 0x0F]);
        crc = uint16_t((crc << 4) ^ tab[((crc >> 12) ^ (data[i] >> 0)) & 0x0F]);
    }
    return crc;
}

/**
 * Frame a VESC payload into a complete wire packet.
 * @param payload payload bytes
 * @param len     payload length
 * @param out     output buffer (≥ len + 6)
 * @return total framed length, or 0 on overflow of the 16-bit length field.
 */
inline size_t frame(const uint8_t* payload, size_t len, uint8_t* out) {
    if (len > 0xFFFF) return 0;
    size_t i = 0;
    if (len <= 255) {
        out[i++] = SOF_SHORT;
        out[i++] = uint8_t(len);
    } else {
        out[i++] = SOF_LONG;
        out[i++] = uint8_t(len >> 8);
        out[i++] = uint8_t(len & 0xFF);
    }
    for (size_t j = 0; j < len; ++j) out[i++] = payload[j];
    uint16_t c = crc16(payload, len);
    out[i++] = uint8_t(c >> 8);
    out[i++] = uint8_t(c & 0xFF);
    out[i++] = EOF_BYTE;
    return i;
}

/**
 * Validate and extract the payload from a complete framed packet.
 * @param in        framed bytes
 * @param inLen     length of @p in
 * @param payloadOut receives a pointer into @p in at the payload start
 * @param payloadLen receives the payload length
 * @return true if SOF/length/CRC/EOF are all consistent.
 */
inline bool unframe(const uint8_t* in, size_t inLen,
                    const uint8_t** payloadOut, size_t* payloadLen) {
    if (inLen < 5) return false;
    size_t i = 0, len = 0;
    if (in[0] == SOF_SHORT)      { len = in[1]; i = 2; }
    else if (in[0] == SOF_LONG)  { len = (size_t(in[1]) << 8) | in[2]; i = 3; }
    else return false;
    if (i + len + 3 > inLen) return false;
    const uint8_t* p = in + i;
    uint16_t recv = uint16_t((in[i + len] << 8) | in[i + len + 1]);
    if (in[i + len + 2] != EOF_BYTE) return false;
    if (crc16(p, len) != recv) return false;
    *payloadOut = p;
    *payloadLen = len;
    return true;
}

} // namespace vesc
} // namespace nrf51
} // namespace ninebot

#endif // NINEBOT_VESC_TUNNEL_H
