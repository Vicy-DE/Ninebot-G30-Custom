// ninebot_protocol_verified.hpp
// =============================================================================
// Byte-faithful C++ decompilation of the Ninebot G30 Max ESC protocol core,
// reconstructed *directly from the DRV_1.6.13_Compat.bin disassembly* and
// verified against the binary's behavior (see tests/test_decompiled_protocol.cpp
// and firmware/decompiled/DECOMPILATION.md).
//
// This is header-only and dependency-free on purpose: it can be compiled and
// run on a host to prove equivalence, and dropped into firmware unchanged.
//
// Source functions (DRV_1.6.13_Compat.bin, app base 0x08001000):
//   calculateChecksum      @ 0x08002720
//   buildPacket            @ 0x080036AC
//   parseProtocolByte      @ 0x08007128   (per-UART, state @ 0x200003AC)
//   dispatchReceivedPacket @ 0x08005468
//
// ⚠️ Wire-format note (corrects firmware/decompiled/common/include/protocol.h
//    and docs/protocol.md): the on-wire LEN field equals the PAYLOAD byte count.
//    The full frame is  5A A5 LEN SRC DST CMD ARG payload[LEN] CK_lo CK_hi,
//    i.e. total length = LEN + 9, and the parser's expected body = LEN + 7.
//    The older protocol.h used LEN = payload + 6 (self-consistent in its own
//    simulator but NOT compatible with a real scooter).
// =============================================================================
#ifndef NINEBOT_PROTOCOL_VERIFIED_HPP
#define NINEBOT_PROTOCOL_VERIFIED_HPP

#include <cstdint>
#include <cstddef>

namespace ninebot_verified {

// ── Wire constants ──────────────────────────────────────────────────────────
static constexpr uint8_t HDR1 = 0x5A;   // buildPacket @0x080036B6: movs r5,#0x5a
static constexpr uint8_t HDR2 = 0xA5;   // buildPacket @0x080036BA: movs r5,#0xa5

// Device addresses (dispatchReceivedPacket switch on DST = buf[2]).
enum Addr : uint8_t { ESC = 0x20, BLE = 0x21, BMS = 0x22, BMS2 = 0x23,
                      APP = 0x3E, PC = 0x3F };

// Frame field offsets within the body (everything after the 5A A5 header).
// buildPacket writes: [2]=LEN [3]=SRC [4]=DST [5]=CMD [6]=ARG [7..]=payload.
// The parser stores the body starting at index 0, so within the rx buffer:
enum BodyOff { OFF_LEN = 0, OFF_SRC = 1, OFF_DST = 2, OFF_CMD = 3, OFF_ARG = 4,
               OFF_PAYLOAD = 5 };

// LEN(=payload) + 7 body bytes; parser rejects expected > 0xF3 (@0x08007142).
static constexpr uint8_t MAX_EXPECTED_BODY = 0xF3;
static constexpr uint8_t BODY_OVERHEAD     = 7;   // adds r2,r0,#7 @0x0800713C

// ─────────────────────────────────────────────────────────────────────────────
// calculateChecksum  @ 0x08002720
//
//   push {r4,lr}; r3=0(sum); r2=0(i)
// loop: r4=data[i]; r3=(r3+r4)&0xFFFF (uxth); i++; cmp i,len; blo loop
//   r0 = ~r3 & 0xFFFF (mvns + uxth); return
//
// i.e. checksum = (~Σ data[i]) & 0xFFFF  ==  (Σ data[i]) ^ 0xFFFF.
// ─────────────────────────────────────────────────────────────────────────────
inline uint16_t calculateChecksum(const uint8_t* data, uint16_t length) {
    uint16_t sum = 0;
    for (uint16_t i = 0; i < length; ++i)
        sum = static_cast<uint16_t>(sum + data[i]);
    return static_cast<uint16_t>(~sum);
}

// ─────────────────────────────────────────────────────────────────────────────
// buildPacket  @ 0x080036AC
//
//   out[0]=0x5A; out[1]=0xA5; out[2]=len; out[3]=src; out[4]=dst;
//   out[5]=cmd; out[6]=arg; i=7; for(j=0;j<len;j++) out[i++]=payload[j];
//   chk = calculateChecksum(out+2, i-2); out[i]=chk&0xFF; out[i+1]=chk>>8;
//
// NOTE: `len` is the PAYLOAD byte count (stored verbatim at out[2] and used as
// the copy count: `cmp r6,r2; blo` with r2=len). Returns total frame size.
// ─────────────────────────────────────────────────────────────────────────────
inline size_t buildPacket(uint8_t src, uint8_t dst, uint8_t cmd, uint8_t arg,
                          const uint8_t* payload, uint8_t len, uint8_t* out) {
    out[0] = HDR1;
    out[1] = HDR2;
    out[2] = len;        // LEN == payload count (firmware: strb r2,[r4,#2])
    out[3] = src;
    out[4] = dst;
    out[5] = cmd;
    out[6] = arg;
    size_t i = 7;
    for (uint8_t j = 0; j < len; ++j) out[i++] = payload[j];
    uint16_t chk = calculateChecksum(out + 2, static_cast<uint16_t>(i - 2));
    out[i++] = static_cast<uint8_t>(chk & 0xFF);
    out[i++] = static_cast<uint8_t>((chk >> 8) & 0xFF);
    return i;            // == len + 9
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser state machine (parseProtocolByte @ 0x08007128).
// Field names mirror the firmware struct @ 0x200003AC (offsets in comments).
// Buffer mirrors the rx body buffer @ 0x200010F8.
// ─────────────────────────────────────────────────────────────────────────────
struct Parser {
    uint8_t  sawHdr1   = 0;   // [+0x07] saw 0x5A
    uint8_t  inPacket  = 0;   // [+0x08] header complete, collecting body
    uint8_t  idx       = 0;   // [+0x09] body byte index
    uint8_t  expected  = 0;   // [+0x0A] expected body length (= LEN + 7)
    uint16_t runSum    = 0;   // [+0x0C] running checksum accumulator
    uint8_t  buf[256]  = {};  // rx body buffer

    void reset() { sawHdr1 = inPacket = idx = expected = 0; runSum = 0; }
};

// A decoded, checksum-valid packet handed to the dispatcher.
struct Packet {
    const uint8_t* body;    // points at parser buf: [0]=LEN..[expected-1]=CK_hi
    uint8_t len;            // LEN field (payload count)
    uint8_t src, dst, cmd, arg;
    const uint8_t* payload; // body + 5
    uint8_t payloadLen;     // == len
    uint16_t checksum;      // received checksum
};

// Result of feeding one byte.
enum class RxResult { NeedMore, Rejected, PacketReady };

// Feed exactly one received byte. On PacketReady, `out` is populated and points
// into parser-owned storage (valid until the next receiveByte call).
// Faithful to parseProtocolByte @0x08007128.
inline RxResult receiveByte(Parser& p, uint8_t b, Packet& out) {
    if (p.inPacket) {                                   // @0x08007134 onward
        p.buf[p.idx] = b;                               // buf[idx] = byte
        if (p.idx == 0) {                               // first body byte = LEN
            p.expected = static_cast<uint8_t>(b + BODY_OVERHEAD);   // LEN + 7
            if (p.expected > MAX_EXPECTED_BODY) {        // @0x08007142 reject
                p.reset();
                return RxResult::Rejected;
            }
        }
        p.idx = static_cast<uint8_t>(p.idx + 1);        // @0x0800714E idx++
        if (p.idx == p.expected) {                      // last byte received
            uint8_t ckLo = p.buf[p.expected - 2];
            uint8_t ckHi = p.buf[p.expected - 1];
            // firmware subtracts the already-accumulated ckLo, then inverts
            uint16_t calc = static_cast<uint16_t>(~(p.runSum - ckLo));
            uint16_t recv = static_cast<uint16_t>(ckLo | (ckHi << 8));
            RxResult r = RxResult::Rejected;
            if (calc == recv) {                          // valid → dispatch
                out.body = p.buf;
                out.len = p.buf[OFF_LEN];
                out.src = p.buf[OFF_SRC];
                out.dst = p.buf[OFF_DST];
                out.cmd = p.buf[OFF_CMD];
                out.arg = p.buf[OFF_ARG];
                out.payload = p.buf + OFF_PAYLOAD;
                out.payloadLen = p.buf[OFF_LEN];
                out.checksum = recv;
                r = RxResult::PacketReady;
            }
            // reset, but keep buf contents for the returned Packet view
            p.sawHdr1 = p.inPacket = p.idx = 0; p.runSum = 0;
            return r;
        }
        p.runSum = static_cast<uint16_t>(p.runSum + b); // accumulate (@0x0800718A)
        return RxResult::NeedMore;
    }

    // IDLE: header detection (@0x08007190 onward)
    if (b == HDR1) {
        if (!p.sawHdr1) { p.sawHdr1 = 1; return RxResult::NeedMore; }
        // 0x5A again while already seen → stays armed (firmware re-checks A5)
        return RxResult::NeedMore;
    }
    if (b == HDR2) {
        if (p.sawHdr1) {                                 // 5A A5 → start body
            p.inPacket = 1; p.runSum = 0; p.idx = 0;
            return RxResult::NeedMore;
        }
    }
    p.reset();                                           // @0x080071AE
    return RxResult::Rejected;
}

// ─────────────────────────────────────────────────────────────────────────────
// dispatchReceivedPacket  @ 0x08005468  (routing portion)
// Switches on DST = body[2]. 0x20 → handled locally (this ESC); 0x21 → BLE bus;
// 0x22/0x23 → BMS bus; 0x3E/0x3F (and 0x3D) → App/PC. The full firmware also
// builds register read/write responses (subroutines @0x08006550, @0x08005af8);
// those are register-file specific and out of scope for the wire-core here.
// ─────────────────────────────────────────────────────────────────────────────
enum class Route { Self, ToBle, ToBms, ToApp, Unknown };

inline Route routeByDst(uint8_t dst) {
    switch (dst) {
        case ESC:               return Route::Self;
        case BLE:               return Route::ToBle;
        case BMS: case BMS2:    return Route::ToBms;
        case 0x3D: case APP: case PC: return Route::ToApp;
        default:                return Route::Unknown;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Register access (App_to_ESC_handler @ 0x08005624).
//
// When a packet is addressed to the ESC (DST=0x20), it is routed by SRC, then a
// tbb jump table @0x08005650 switches on CMD = body[3]:
//   CMD 0x01 → 0x0800567E : READ  — send regfile[ARG..] back to the source
//   CMD 0x02 → 0x0800569C : WRITE — copy payload into regfile[ARG..], then ACK
//   CMD 0x03 → 0x0800569C : WRITE (no/var response)
//   CMD 0x00,0x04..0x06   → 0x08005698 : ignored (return)
//   CMD 0x07..0x0A        → 0x08005744 : extended (subscribe/stream)
//   CMD 0x18,0x50,0x57-0x59,0x5C : IAP / update / calibration opcodes
//
// The register file is a flat array of 16-bit words in SRAM @ 0x200007D6,
// indexed by the ARG byte (`add.w r0, r7, fp, lsl #1`, r7=regfile, fp=ARG).
// READ returns ARG-indexed words; WRITE stores payload there (memcpy @0x80011AC),
// with ARG 0x17 / 0xE0 additionally setting a refresh flag.
// ─────────────────────────────────────────────────────────────────────────────
// CMD codes (firmware tbb table + ninebot-docs): read, write(+resp), write(no resp),
// read-response (Ninebot uses 0x04), firmware-update sequence, head I/O.
enum Cmd : uint8_t { CMD_READ = 0x01, CMD_WRITE = 0x02, CMD_WRITE_NR = 0x03,
                     CMD_READ_RESP = 0x04, CMD_FW_UPD0 = 0x07, CMD_HEAD_IO = 0x64 };

// Named ARG indices into the register files (semantics: ninebot-docs ES2 family;
// see docs/REGISTER_MAP.md). Mechanism (ARG index) is firmware-confirmed.
namespace EscReg {
    enum : uint8_t { SERIAL = 0x10, PIN = 0x17, FW_VERSION = 0x1A, ERROR = 0x1B,
                     ALARM = 0x1C, BATT_LEVEL = 0x22, SPEED = 0x26, TOTAL_DIST = 0x29,
                     FRAME_TEMP = 0x3E, SUPPLY_V = 0x47, BATT_V = 0x48, BATT_I = 0x49,
                     MODE = 0x75, KERS = 0x7B, CRUISE = 0x7C, TAILLIGHT = 0x7D };
}
namespace BmsReg {
    enum : uint8_t { SERIAL = 0x10, FW_VERSION = 0x17, FACTORY_CAP = 0x18,
                     STATUS = 0x30, REMAIN_MAH = 0x31, SOC_PCT = 0x32, CURRENT = 0x33,
                     PACK_V = 0x34, TEMP = 0x35, BALANCE = 0x36, HEALTH = 0x3B,
                     CELL1 = 0x40 /* cells 1..10 = 0x40..0x49 */ };
}

// Per-board register-file base addresses (16-bit words, indexed by the ARG byte).
// The READ/WRITE dispatch is byte-identical across boards; only the base differs.
//   ESC  (DRV_1.6.13)   @ 0x200007D6   (App_to_ESC_handler  @0x08005624)
//   BMS  (BMS_1.7.4.5)  @ 0x20000400   (BMS_dispatch        @0x08005610, CMD1=read/CMD2=write)
static constexpr uint32_t ESC_REGFILE_ADDR = 0x200007D6;
static constexpr uint32_t BMS_REGFILE_ADDR = 0x20000400;
static constexpr size_t   REGFILE_WORDS    = 128;   // ARG is a byte → up to 256 words

// Host model of the ARG-indexed 16-bit register file. `bytes()` views it as the
// little-endian byte image the firmware reads/writes.
struct RegisterFile {
    uint16_t reg[REGFILE_WORDS] = {};
    uint8_t  refreshFlag = 0;   // mirrors [sl,#0x11] set on ARG 0x17/0xE0 writes

    uint8_t* bytes() { return reinterpret_cast<uint8_t*>(reg); }

    // READ: copy `count` bytes starting at ARG-indexed byte offset (ARG*2) → out.
    // Returns bytes copied (0 if out of range).
    uint8_t read(uint8_t arg, uint8_t count, uint8_t* out) {
        size_t off = size_t(arg) * 2;
        if (off + count > sizeof(reg)) return 0;
        for (uint8_t i = 0; i < count; ++i) out[i] = bytes()[off + i];
        return count;
    }
    // WRITE: store `len` payload bytes at ARG-indexed offset. Sets refreshFlag
    // for ARG 0x17 / 0xE0 (firmware @0x080056C4).
    bool write(uint8_t arg, const uint8_t* payload, uint8_t len) {
        size_t off = size_t(arg) * 2;
        if (off + len > sizeof(reg)) return false;
        for (uint8_t i = 0; i < len; ++i) bytes()[off + i] = payload[i];
        if (arg == 0x17 || arg == 0xE0) refreshFlag = 1;
        return true;
    }
};

// Handle a parsed packet addressed to the ESC against a register file.
// On READ, builds the response frame into `respOut` (ESC→src) and returns its
// size; on WRITE, applies it and returns 0. Mirrors the CMD dispatch above.
inline size_t handleEscPacket(const Packet& pkt, RegisterFile& rf, uint8_t* respOut) {
    switch (pkt.cmd) {
        case CMD_READ: {
            // payload[0] of a read request = number of BYTES requested (firmware
            // reads body[5]); respond with that many ARG-indexed bytes.
            uint8_t count = (pkt.payloadLen >= 1) ? pkt.payload[0] : 0;
            uint8_t data[256];
            uint8_t n = rf.read(pkt.arg, count, data);
            if (!respOut || n == 0) return 0;
            return buildPacket(ESC, pkt.src, CMD_READ, pkt.arg, data, n, respOut);
        }
        case CMD_WRITE:
        case CMD_WRITE_NR:
            rf.write(pkt.arg, pkt.payload, pkt.payloadLen);
            return 0;
        default:
            return 0;   // ignored / extended opcodes not modeled here
    }
}

} // namespace ninebot_verified
#endif // NINEBOT_PROTOCOL_VERIFIED_HPP
