/**
 * @file nb_protocol.h
 * @brief Ninebot `5A A5` bus codec for the dashboard (nRF51822 side).
 *
 * Framing (firmware- and bus-confirmed on the live scooter):
 *
 *     5A A5 | LEN | SRC | DST | CMD | ARG | payload[LEN] | CK_lo CK_hi
 *
 *   - LEN counts **payload bytes only** (not SRC/DST/CMD/ARG).
 *   - CK = (sum of bytes from LEN through the last payload byte) XOR 0xFFFF, little-endian.
 *   - 115200 8N1 (stock BAUDRATE literal 0x01D7E000), half-duplex on P0.15/P0.20.
 */
#ifndef NB_PROTOCOL_H
#define NB_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

namespace dash {

/** @brief Bus device addresses. */
enum Addr : uint8_t {
    ADDR_ESC = 0x20,   /**< motor controller (a VESC in this build) */
    ADDR_BLE = 0x21,   /**< this dashboard */
    ADDR_BMS = 0x22,
    ADDR_APP = 0x3E,   /**< phone app over BLE */
    ADDR_PC  = 0x3F,
};

/** @brief Commands seen on the live bus / in the stock image. */
enum Cmd : uint8_t {
    CMD_READ      = 0x01,
    CMD_WRITE     = 0x02,
    CMD_READ_ACK  = 0x04,
    CMD_WRITE_ACK = 0x05,
    CMD_TELEMETRY = 0x64, /**< ESC -> dash: mode, batt, light, beep, speed, error */
    CMD_THROTTLE  = 0x65, /**< dash -> ESC: throttle/brake */
};

static constexpr uint8_t kPre0 = 0x5A;
static constexpr uint8_t kPre1 = 0xA5;
static constexpr size_t  kMaxPayload = 64;
static constexpr size_t  kOverhead = 9; /**< 5A A5 LEN SRC DST CMD ARG + 2 checksum */

/** @brief A decoded bus frame. */
struct Frame {
    uint8_t src = 0;
    uint8_t dst = 0;
    uint8_t cmd = 0;
    uint8_t arg = 0;
    uint8_t len = 0;
    uint8_t payload[kMaxPayload] = {0};
};

/** @brief Checksum over LEN..payload: sum XOR 0xFFFF. */
uint16_t checksum(const uint8_t* from_len, size_t count);

/**
 * @brief Serialise a frame.
 * @return bytes written, or 0 if @p out is too small / payload too long.
 */
size_t encode(const Frame& f, uint8_t* out, size_t out_cap);

/** @brief Incremental receiver: feed bytes, get frames. Resynchronises on bad preamble/checksum. */
class Decoder {
public:
    /**
     * @brief Push one received byte.
     * @param b  byte from the UART
     * @param out receives the frame when this byte completes a valid one
     * @return true when @p out was filled
     */
    bool feed(uint8_t b, Frame& out);

    /** @brief Drop any partial frame. */
    void reset() { state_ = State::Pre0; idx_ = 0; }

    uint32_t badChecksums() const { return bad_checksums_; }

private:
    enum class State : uint8_t { Pre0, Pre1, Body };
    State state_ = State::Pre0;
    uint8_t buf_[kOverhead + kMaxPayload] = {0};
    size_t idx_ = 0;       /**< bytes collected into buf_ starting at LEN */
    size_t expected_ = 0;  /**< total bytes from LEN through checksum */
    uint32_t bad_checksums_ = 0;
};

/** @brief Build the `0x64` telemetry frame the dashboard expects from an ESC. */
Frame makeTelemetry(uint8_t mode, uint8_t battery, uint8_t light,
                    uint8_t beep, uint8_t speed, uint8_t error);

/** @brief Build the `0x65` throttle/brake frame the dashboard sends to the ESC. */
Frame makeThrottle(uint8_t throttle, uint8_t brake);

} // namespace dash

#endif // NB_PROTOCOL_H
