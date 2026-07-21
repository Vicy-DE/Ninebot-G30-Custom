/**
 * @file daly_soft_uart.h
 * @brief Daly Smart BMS UART protocol client (Req 13) — frame build/parse + a
 *        byte-stream client usable over a real soft-UART (target) or a buffer
 *        (host tests).
 *
 * Header-only and dependency-free (like `ninebot_protocol_verified.hpp`) so it
 * can be unit-tested on the host and dropped into the STM32 dashboard firmware
 * unchanged. On target, the dashboard bit-bangs this on cable wire **w4**
 * (9600 8N1) per `docs/DASHBOARD_FIRMWARE.md` §5.1 and uses `0xD9` to make the
 * Daly cut/restore VESC power (power-latch Solution D).
 *
 * Wire format (Daly V4/V5 UART, ref maland16/daly-bms-uart, 13-byte fixed frame):
 * @code
 *   [0] 0xA5  start
 *   [1] addr  0x40 = host->BMS request, 0x01 = BMS->host response
 *   [2] cmd   0x90..0x98 (reads), 0xD9 discharge-MOS, 0xDA charge-MOS
 *   [3] 0x08  data length (always 8)
 *   [4..11]   8 data bytes (0x00 for read requests)  — BIG-ENDIAN values
 *   [12] chk  (sum of bytes [0..11]) & 0xFF
 * @endcode
 *
 * @note Multi-byte values in the data field are **big-endian**.
 * @note Current uses a +30000 (0x7530) offset: amps = (raw - 30000) * 0.1.
 */
#ifndef NINEBOT_DALY_SOFT_UART_H
#define NINEBOT_DALY_SOFT_UART_H

#include <cstdint>
#include <cstddef>
#if !defined(NINEBOT_DALY_NO_CLIENT)
#include <functional>   // only the streaming DalyClient needs std::function
#endif

namespace ninebot {
namespace daly {

/* ── Wire constants ──────────────────────────────────────────────────────── */
static constexpr uint8_t START_BYTE   = 0xA5;
static constexpr uint8_t ADDR_HOST     = 0x40;   ///< host -> BMS
static constexpr uint8_t ADDR_BMS      = 0x01;   ///< BMS  -> host
static constexpr uint8_t DATA_LEN      = 0x08;
static constexpr size_t  FRAME_LEN     = 13;     ///< total bytes on the wire
static constexpr uint16_t CURRENT_OFFSET = 30000; ///< 0x7530

/** Daly command IDs used by the dashboard. */
enum Cmd : uint8_t {
    CMD_SOC          = 0x90,  ///< total V / gather V / current / SOC
    CMD_MINMAX_CELL  = 0x91,  ///< max & min cell voltage
    CMD_MINMAX_TEMP  = 0x92,  ///< max & min temperature
    CMD_MOS_STATUS   = 0x93,  ///< charge/discharge MOS + remaining capacity
    CMD_STATUS_INFO  = 0x94,  ///< #cells, #temps, charger/load state
    CMD_CELL_VOLT    = 0x95,  ///< per-cell voltages (multi-frame)
    CMD_CELL_TEMP    = 0x96,  ///< per-cell temperatures (multi-frame)
    CMD_BALANCE      = 0x97,  ///< per-cell balance state
    CMD_FAULT        = 0x98,  ///< failure/fault flags
    CMD_DISCHARGE_MOS = 0xD9, ///< switch discharge MOSFET (data[0] = 1/0)
    CMD_CHARGE_MOS    = 0xDA, ///< switch charge MOSFET    (data[0] = 1/0)
};

/** Decoded `0x90` pack summary. */
struct PackInfo {
    uint16_t voltage_dV;   ///< total pack voltage, 0.1 V units
    int16_t  current_dA;   ///< pack current, 0.1 A units (+ discharge / − charge)
    uint16_t soc_dPct;     ///< state of charge, 0.1 % units
};

/** big-endian u16 read helper. */
inline uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1]);
}

/** Frame checksum = (Σ bytes[0..11]) & 0xFF. */
inline uint8_t checksum(const uint8_t* frame) {
    uint16_t sum = 0;
    for (size_t i = 0; i < FRAME_LEN - 1; ++i) sum += frame[i];
    return static_cast<uint8_t>(sum & 0xFF);
}

/** True if a 13-byte buffer is a structurally valid Daly frame for @p cmd. */
inline bool validate(const uint8_t* frame, uint8_t cmd) {
    return frame[0] == START_BYTE && frame[2] == cmd && frame[3] == DATA_LEN &&
           frame[FRAME_LEN - 1] == checksum(frame);
}

/**
 * Build a read-request frame (all 8 data bytes zero).
 * @param cmd one of the CMD_* read IDs (0x90..0x98)
 * @param out 13-byte output buffer
 * @return FRAME_LEN
 */
inline size_t buildRead(uint8_t cmd, uint8_t* out) {
    out[0] = START_BYTE;
    out[1] = ADDR_HOST;
    out[2] = cmd;
    out[3] = DATA_LEN;
    for (size_t i = 4; i < FRAME_LEN - 1; ++i) out[i] = 0x00;
    out[FRAME_LEN - 1] = checksum(out);
    return FRAME_LEN;
}

/**
 * Build a MOSFET on/off control frame (`0xD9` discharge or `0xDA` charge).
 * `0xD9 OFF` is the dashboard's true power-cut (Solution D); the start bit of
 * the first byte also doubles as the Daly `S1` wake pulse on the target.
 * @param cmd CMD_DISCHARGE_MOS or CMD_CHARGE_MOS
 * @param on  true = enable the MOSFET, false = open it
 * @param out 13-byte output buffer
 * @return FRAME_LEN
 */
inline size_t buildMosControl(uint8_t cmd, bool on, uint8_t* out) {
    out[0] = START_BYTE;
    out[1] = ADDR_HOST;
    out[2] = cmd;
    out[3] = DATA_LEN;
    out[4] = on ? 0x01 : 0x00;
    for (size_t i = 5; i < FRAME_LEN - 1; ++i) out[i] = 0x00;
    out[FRAME_LEN - 1] = checksum(out);
    return FRAME_LEN;
}

/**
 * Parse a `0x90` response frame into a PackInfo.
 * @return true on a valid 0x90 frame; @p out is then populated.
 */
inline bool parseSoc(const uint8_t* frame, PackInfo& out) {
    if (!validate(frame, CMD_SOC)) return false;
    const uint8_t* d = frame + 4;
    out.voltage_dV = be16(d + 0);
    out.current_dA = static_cast<int16_t>(static_cast<int32_t>(be16(d + 4)) -
                                          CURRENT_OFFSET);
    out.soc_dPct   = be16(d + 6);
    return true;
}

#if !defined(NINEBOT_DALY_NO_CLIENT)
/**
 * Stream client: assembles inbound bytes into 13-byte frames and emits a
 * PackInfo whenever a valid `0x90` response completes. Sending is delegated to
 * a byte-sink so the same code drives a real soft-UART or a host buffer.
 *
 * Not @sideeffects-free: requestSoc()/setDischarge() push bytes to the sink.
 * Define `NINEBOT_DALY_NO_CLIENT` (bare-metal firmware) to exclude this and the
 * `<functional>` dependency; the free functions above still build the frames.
 */
class DalyClient {
public:
    using ByteSink   = std::function<void(uint8_t)>;
    using SocHandler = std::function<void(const PackInfo&)>;

    explicit DalyClient(ByteSink tx) : tx_(std::move(tx)) {}

    void onSoc(SocHandler h) { socHandler_ = std::move(h); }

    /** @sideeffects Emit a 0x90 (SOC) read request. */
    void requestSoc() { emit(CMD_SOC); }

    /** @sideeffects Switch the discharge MOSFET (true=on powers VESC, false=cut). */
    void setDischarge(bool on) {
        uint8_t f[FRAME_LEN];
        buildMosControl(CMD_DISCHARGE_MOS, on, f);
        for (uint8_t b : f) if (tx_) tx_(b);
    }

    /** Feed one received byte; dispatches a PackInfo on a complete 0x90 frame. */
    void receiveByte(uint8_t b) {
        if (rxIdx_ == 0 && b != START_BYTE) return;     // resync on start byte
        rx_[rxIdx_++] = b;
        if (rxIdx_ == FRAME_LEN) {
            PackInfo info;
            if (parseSoc(rx_, info) && socHandler_) socHandler_(info);
            rxIdx_ = 0;
        }
    }

private:
    void emit(uint8_t cmd) {
        uint8_t f[FRAME_LEN];
        buildRead(cmd, f);
        for (uint8_t b : f) if (tx_) tx_(b);
    }

    ByteSink   tx_;
    SocHandler socHandler_;
    uint8_t    rx_[FRAME_LEN] = {};
    uint8_t    rxIdx_ = 0;
};
#endif // !NINEBOT_DALY_NO_CLIENT

} // namespace daly
} // namespace ninebot

#endif // NINEBOT_DALY_SOFT_UART_H
