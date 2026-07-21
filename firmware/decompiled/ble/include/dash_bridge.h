/**
 * @file dash_bridge.h
 * @brief Ninebot ⇄ VESC bridge + ESC-register synthesis for the STM32 dashboard
 *        (Req 1, 2, 4). Header-only, HAL-free, host-testable.
 *
 * ## Why this exists
 * In the VESC build the stock ESC (address 0x20) is gone. The phone app still
 * talks the Ninebot **register protocol** (CMD 0x01 READ / 0x02 WRITE, ARG-indexed
 * register file — see `docs/REGISTER_MAP.md`) addressed to the ESC. The VESC lisp
 * (`vesc-lisp/g30_dash.lisp`) only speaks the **head-I/O protocol** (frame 0x64
 * display / 0x65 throttle). So nothing answers the app's register reads.
 *
 * `DashBridge` closes that gap: it keeps a synthetic **ESC register image**, fills
 * it from VESC telemetry (parsed from the 0x64 display frame and/or pushed directly),
 * and answers app register reads/writes locally — making the new scooter look like a
 * stock ESC to the original app. It also builds the 0x65 throttle/brake frame the
 * VESC lisp consumes. **Speed is never clamped** (Req 4): speed-limit writes are
 * accepted and stored but never used to limit the throttle frame.
 *
 * ## Data flow
 * @code
 *   phone app ──register R/W (DST 0x20)──► nRF51 ──UART──► STM32 DashBridge
 *                                                            │  onAppPacket()
 *                                          ◄── read-response ┘  (from ESC image)
 *
 *   ADC throttle/brake ──► buildThrottleFrame() ─0x65─► VESC lisp
 *   VESC lisp ─0x64 display─► parseDisplayFrame() ──► updates ESC image
 * @endcode
 */
#ifndef NINEBOT_DASH_BRIDGE_H
#define NINEBOT_DASH_BRIDGE_H

#include "ninebot_protocol_verified.hpp"
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ninebot {
namespace ble {

namespace nv = ninebot_verified;

/** Telemetry sourced from the VESC (and Daly) to publish into the ESC image. */
struct VescTelemetry {
    int16_t  speed_kmh_x10   = 0;   ///< 0.1 km/h, signed
    uint16_t voltage_V_x100  = 0;   ///< 0.01 V (pack voltage)
    int16_t  current_A_x100  = 0;   ///< 0.01 A (+ discharge / − regen)
    int16_t  temp_fet_C_x10  = 0;   ///< 0.1 °C (MOSFET)
    int16_t  temp_frame_C_x10 = 0;  ///< 0.1 °C (controller/frame)
    uint8_t  battery_pct     = 0;   ///< 0..100 %
    uint16_t fault_code      = 0;   ///< Ninebot error bitmask (from VESC fault)
    uint8_t  mode            = 0;   ///< 0=Drive/Normal, 1=Eco, 2=Sport
    uint8_t  light           = 0;   ///< 0/1
};

/** Fields the VESC lisp packs into the 0x64 display frame payload. */
struct DisplayState {
    uint8_t mode  = 0;   ///< raw mode byte (incl. icon bits)
    uint8_t batt  = 0;   ///< 0..100 %
    uint8_t light = 0;
    uint8_t beep  = 0;
    uint8_t speed = 0;   ///< km/h (integer) as the head shows it
    uint8_t error = 0;
};

/** Outbound action the bridge requests after an app write (caller relays to VESC). */
enum class BridgeAction : uint8_t { None, SetMode, SetLight, Lock, Unlock, Reboot, PowerOff };

/**
 * Synthetic-ESC bridge. Owns the ESC register image the app reads.
 *
 * ARG indices follow `docs/REGISTER_MAP.md` (ES2/G30 family). High-confidence,
 * UI-critical regs: 0x22 battery %, 0x26 speed (0.1 km/h), 0x75 mode, 0x1B error.
 * Voltage/current/temperature scalings are documented calibration points.
 */
class DashBridge {
public:
    DashBridge() {
        setReg(0x00, 0x515C);             // ESC magic (REGISTER_MAP)
        setReg(EscReg::FW_VERSION, 0x0613); // report an ESC fw version to the app
        setReg(EscReg::MODE, 0);
    }

    /* ── ESC register image ───────────────────────────────────────────────── */
    nv::RegisterFile&       registers()       { return esc_; }
    const nv::RegisterFile& registers() const { return esc_; }

    /** Set one ARG-indexed 16-bit register (little-endian in the byte image). */
    void setReg(uint8_t arg, uint16_t value) {
        uint8_t le[2] = { uint8_t(value & 0xFF), uint8_t(value >> 8) };
        esc_.write(arg, le, 2);
    }
    uint16_t getReg(uint8_t arg) const {
        return (arg < nv::REGFILE_WORDS) ? const_cast<nv::RegisterFile&>(esc_).reg[arg] : 0;
    }

    /* ── Telemetry publish (VESC/Daly → ESC image) ────────────────────────── */
    /** Publish a full telemetry snapshot into the ESC register image. */
    void updateTelemetry(const VescTelemetry& t) {
        setReg(EscReg::BATT_LEVEL, t.battery_pct);                 // 0x22 %
        setReg(EscReg::SPEED, static_cast<uint16_t>(t.speed_kmh_x10)); // 0x26 0.1km/h
        setReg(EscReg::BATT_V, t.voltage_V_x100);                  // 0x48
        setReg(EscReg::BATT_I, static_cast<uint16_t>(t.current_A_x100)); // 0x49
        setReg(EscReg::FRAME_TEMP, static_cast<uint16_t>(t.temp_frame_C_x10)); // 0x3E
        setReg(0x41, static_cast<uint16_t>(t.temp_fet_C_x10));     // MOSFET temp
        setReg(EscReg::ERROR, t.fault_code);                       // 0x1B
        setReg(0x1F, t.mode);                                      // operation mode
        setReg(EscReg::MODE, t.mode);                              // 0x75
        light_ = t.light;
    }

    /**
     * Parse a VESC→dashboard 0x64 display frame and fold it into the ESC image.
     * Payload layout mirrors `g30_dash.lisp` update-dash: [mode,batt,light,beep,
     * speed,error]. Returns true if the packet was a 0x64 head frame.
     */
    bool parseDisplayFrame(const nv::Packet& pkt, DisplayState* outState = nullptr) {
        if (pkt.cmd != nv::CMD_HEAD_IO || pkt.payloadLen < 6) return false; // 0x64
        DisplayState d;
        d.mode  = pkt.payload[0];
        d.batt  = pkt.payload[1];
        d.light = pkt.payload[2];
        d.beep  = pkt.payload[3];
        d.speed = pkt.payload[4];
        d.error = pkt.payload[5];

        setReg(EscReg::BATT_LEVEL, d.batt);                 // 0x22
        setReg(EscReg::SPEED, uint16_t(d.speed) * 10);      // 0x26 km/h → 0.1km/h
        setReg(EscReg::ERROR, d.error);                     // 0x1B
        setReg(EscReg::MODE, uint8_t(d.mode & 0x0F));       // 0x75 (strip icon bits)
        light_ = d.light;
        if (outState) *outState = d;
        return true;
    }

    /* ── 0x65 throttle/brake frame (dashboard → VESC lisp) ─────────────────── */
    /**
     * Build a 0x65 head frame the lisp's adc-input reads (throttle at payload[1],
     * brake at payload[2]). SRC=BLE(0x21) DST=ESC(0x20). **No speed clamp** (Req 4).
     * @return frame length (== 3 + 9).
     */
    size_t buildThrottleFrame(uint8_t throttle, uint8_t brake, uint8_t* out) {
        uint8_t payload[3] = { 0x00, throttle, brake };
        return nv::buildPacket(nv::BLE, nv::ESC, 0x65, 0x00, payload, 3, out);
    }

    /* ── App register protocol (phone → ESC image) ─────────────────────────── */
    /**
     * Handle a Ninebot packet that the app addressed to the ESC (DST 0x20).
     * On READ: writes a read-response frame into @p respOut and returns its size.
     * On WRITE: applies it to the ESC image, records a BridgeAction for the caller
     * to forward to the VESC, and returns 0.
     * Returns 0 (and sets action None) for packets not addressed to the ESC.
     */
    size_t onAppPacket(const nv::Packet& pkt, uint8_t* respOut) {
        lastAction_ = BridgeAction::None;
        actionValue_ = 0;
        if (pkt.dst != nv::ESC) return 0;

        switch (pkt.cmd) {
            case nv::CMD_READ: {
                uint8_t count = (pkt.payloadLen >= 1) ? pkt.payload[0] : 2;
                uint8_t data[256];
                uint8_t n = esc_.read(pkt.arg, count, data);
                if (!respOut || n == 0) return 0;
                return nv::buildPacket(nv::ESC, pkt.src, nv::CMD_READ_RESP,
                                       pkt.arg, data, n, respOut);
            }
            case nv::CMD_WRITE:
            case nv::CMD_WRITE_NR:
                applyWrite(pkt);
                return 0;
            default:
                return 0;
        }
    }

    BridgeAction lastAction() const { return lastAction_; }
    uint16_t     actionValue() const { return actionValue_; }
    uint8_t      light() const { return light_; }

private:
    /** ARG names we care about (subset of REGISTER_MAP / EscReg). */
    struct EscReg {
        static constexpr uint8_t FW_VERSION = 0x1A, ERROR = 0x1B, BATT_LEVEL = 0x22,
            SPEED = 0x26, FRAME_TEMP = 0x3E, BATT_V = 0x48, BATT_I = 0x49,
            LOCK = 0x70, UNLOCK = 0x71, SPEED_LIMIT_CTRL = 0x72, NORMAL_LIMIT = 0x73,
            ECO_LIMIT = 0x74, MODE = 0x75, REBOOT = 0x78, POWERDOWN = 0x79,
            TAILLIGHT = 0x7D;
    };

    void applyWrite(const nv::Packet& pkt) {
        // Store every write into the image first (so subsequent reads echo it)…
        esc_.write(pkt.arg, pkt.payload, pkt.payloadLen);
        uint16_t v = (pkt.payloadLen >= 1) ? pkt.payload[0] : 0;
        if (pkt.payloadLen >= 2) v |= uint16_t(pkt.payload[1]) << 8;

        switch (pkt.arg) {
            case EscReg::MODE:                 // 0x75 operating mode
                lastAction_ = BridgeAction::SetMode;  actionValue_ = v & 0xFF; break;
            case EscReg::TAILLIGHT:            // 0x7D
                lastAction_ = BridgeAction::SetLight; actionValue_ = v & 0xFF;
                light_ = v & 0x01; break;
            case EscReg::LOCK:                 // 0x70
                lastAction_ = BridgeAction::Lock;   break;
            case EscReg::UNLOCK:               // 0x71
                lastAction_ = BridgeAction::Unlock; break;
            case EscReg::REBOOT:               // 0x78
                lastAction_ = BridgeAction::Reboot; break;
            case EscReg::POWERDOWN:            // 0x79
                lastAction_ = BridgeAction::PowerOff; break;
            // 0x72/0x73/0x74 speed-limit writes: accepted + stored, never used to
            // clamp the throttle frame (Req 4 — speed cap removal). No action.
            default: break;
        }
    }

    nv::RegisterFile esc_;
    BridgeAction lastAction_ = BridgeAction::None;
    uint16_t     actionValue_ = 0;
    uint8_t      light_ = 0;
};

} // namespace ble
} // namespace ninebot

#endif // NINEBOT_DASH_BRIDGE_H
