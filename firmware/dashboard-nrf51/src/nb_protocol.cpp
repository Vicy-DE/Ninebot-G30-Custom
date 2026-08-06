/**
 * @file nb_protocol.cpp
 * @brief Ninebot `5A A5` framing/deframing.
 */
#include "nb_protocol.h"

namespace dash {

uint16_t checksum(const uint8_t* from_len, size_t count)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += from_len[i];
    }
    return static_cast<uint16_t>((sum & 0xFFFF) ^ 0xFFFF);
}

size_t encode(const Frame& f, uint8_t* out, size_t out_cap)
{
    if (f.len > kMaxPayload) {
        return 0;
    }
    const size_t total = kOverhead + f.len;
    if (out_cap < total) {
        return 0;
    }

    out[0] = kPre0;
    out[1] = kPre1;
    out[2] = f.len;          /* LEN counts payload only */
    out[3] = f.src;
    out[4] = f.dst;
    out[5] = f.cmd;
    out[6] = f.arg;
    for (uint8_t i = 0; i < f.len; ++i) {
        out[7 + i] = f.payload[i];
    }

    const size_t body = 5u + f.len;               /* LEN SRC DST CMD ARG + payload */
    const uint16_t ck = checksum(&out[2], body);
    out[2 + body] = static_cast<uint8_t>(ck & 0xFF);
    out[3 + body] = static_cast<uint8_t>(ck >> 8);
    return total;
}

bool Decoder::feed(uint8_t b, Frame& out)
{
    switch (state_) {
    case State::Pre0:
        if (b == kPre0) {
            state_ = State::Pre1;
        }
        return false;

    case State::Pre1:
        if (b == kPre1) {
            state_ = State::Body;
            idx_ = 0;
            expected_ = 0;
        } else if (b != kPre0) {
            state_ = State::Pre0;   /* not a preamble; resync (a lone 5A may still start one) */
        }
        return false;

    case State::Body:
        if (idx_ < sizeof(buf_)) {
            buf_[idx_] = b;
        }
        ++idx_;

        if (idx_ == 1) {                       /* just read LEN */
            if (b > kMaxPayload) {             /* implausible -> resync */
                reset();
                return false;
            }
            expected_ = 1u + 4u + b + 2u;      /* LEN + SRC DST CMD ARG + payload + checksum */
            return false;
        }
        if (idx_ < expected_) {
            return false;
        }

        /* Frame complete — verify. */
        {
            const size_t body = expected_ - 2;                   /* LEN..payload */
            const uint16_t want = checksum(buf_, body);
            const uint16_t got = static_cast<uint16_t>(buf_[body] | (buf_[body + 1] << 8));
            if (want != got) {
                ++bad_checksums_;
                reset();
                return false;
            }
            out.len = buf_[0];
            out.src = buf_[1];
            out.dst = buf_[2];
            out.cmd = buf_[3];
            out.arg = buf_[4];
            for (uint8_t i = 0; i < out.len; ++i) {
                out.payload[i] = buf_[5 + i];
            }
        }
        reset();
        return true;
    }
    return false;
}

Frame makeTelemetry(uint8_t mode, uint8_t battery, uint8_t light,
                    uint8_t beep, uint8_t speed, uint8_t error)
{
    Frame f;
    f.src = ADDR_ESC;
    f.dst = ADDR_BLE;
    f.cmd = CMD_TELEMETRY;
    f.arg = 0x00;
    f.len = 6;
    f.payload[0] = mode;
    f.payload[1] = battery;
    f.payload[2] = light;
    f.payload[3] = beep;
    f.payload[4] = speed;
    f.payload[5] = error;   /* 0 == healthy; non-zero raises the dashboard comm fault */
    return f;
}

Frame makeThrottle(uint8_t throttle, uint8_t brake)
{
    Frame f;
    f.src = ADDR_BLE;
    f.dst = ADDR_ESC;
    f.cmd = CMD_THROTTLE;
    f.arg = 0x00;
    f.len = 5;
    f.payload[0] = 0x04;      /* matches the captured live frame's leading byte */
    f.payload[1] = throttle;
    f.payload[2] = brake;
    f.payload[3] = 0x02;
    f.payload[4] = 0x00;
    return f;
}

} // namespace dash
