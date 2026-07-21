/**
 * @file sim_chip.h
 * @brief Functional STM32F103C8 model for the dashboard chip simulator.
 *
 * Models the peripherals the dashboard firmware uses, with **register-accurate
 * IWDG behaviour** and **brick auditing** — enough to verify, on the host, that
 * the firmware:
 *   - programs and feeds a real 5000 ms watchdog,
 *   - lets the watchdog RESET the MCU when a required subsystem is missing,
 *   - RECOVERS after a reset (no permanent boot loop),
 *   - never writes the bootloader / option-byte flash regions (the real brick
 *     vectors), never sets read-protection.
 *
 * This is a *functional* simulator (peripheral semantics + time), not a
 * cycle-accurate ISS. It runs the identical `DashApp` logic that ships, through
 * the same `dash::hal` interface. For instruction-exact simulation of the linked
 * `.elf`, use Renode/QEMU (see firmware/dashboard/sim/README.md).
 */
#ifndef DASH_SIM_CHIP_H
#define DASH_SIM_CHIP_H

#include <cstdint>
#include <cstddef>
#include <deque>
#include <vector>
#include <string>

namespace dash {
namespace sim {

/** Memory regions that must never be programmed by the application. */
static constexpr uint32_t BOOTLOADER_LO = 0x08000000, BOOTLOADER_HI = 0x08000FFF;
static constexpr uint32_t OPTION_LO     = 0x1FFFF800, OPTION_HI     = 0x1FFFF80F;
static constexpr uint32_t APP_BASE      = 0x08001000;
static constexpr uint32_t SRAM_LO = 0x20000000, SRAM_HI = 0x20005000;

class SimChip {
public:
    /* ── time ──────────────────────────────────────────────────────────── */
    uint32_t time_ms = 0;

    /* ── reset / brick audit ───────────────────────────────────────────── */
    bool        reset_pending = false;
    uint32_t    reset_count   = 0;
    uint32_t    last_reset_ms = 0;
    uint32_t    last_reset_interval_ms = 0;
    bool        bricked = false;
    std::string brick_reason;

    /* ── clock (assumed to come up; firmware busy-waits on RDY) ─────────── */
    bool clock_ready = true;

    /* ── IWDG (register-accurate enough to validate the 5 s requirement) ── */
    bool     iwdg_started = false;
    uint8_t  iwdg_pr  = 0;
    uint16_t iwdg_rlr = 0;
    uint32_t iwdg_timeout_ms = 0;
    uint32_t iwdg_last_reload_ms = 0;
    uint32_t iwdg_lsi_hz = 40000;

    void iwdg_program(uint8_t pr, uint16_t rlr) {
        if (pr > 6 || rlr > 4095) { brick("IWDG programmed with invalid PR/RLR"); return; }
        iwdg_pr = pr; iwdg_rlr = rlr;
        uint32_t presc = 4u << pr;
        iwdg_timeout_ms = uint32_t((uint64_t)presc * (rlr + 1) * 1000u / iwdg_lsi_hz);
        iwdg_started = true;
        iwdg_last_reload_ms = time_ms;
    }
    void iwdg_reload() { iwdg_last_reload_ms = time_ms; }

    /* ── USART ─────────────────────────────────────────────────────────── */
    std::deque<uint8_t>  u1_rx, u2_rx;   // injected RX (toward the firmware)
    std::vector<uint8_t> u1_tx, u2_tx;   // captured TX (from the firmware)
    void inject_u1(const uint8_t* p, size_t n) { for (size_t i=0;i<n;++i) u1_rx.push_back(p[i]); }
    void inject_u2(const uint8_t* p, size_t n) { for (size_t i=0;i<n;++i) u2_rx.push_back(p[i]); }

    /* ── ADC ───────────────────────────────────────────────────────────── */
    bool     adc_ok = true;              // false → simulate a missing/failed ADC
    uint16_t adc_ch[16] = {};

    /* ── GPIO ──────────────────────────────────────────────────────────── */
    bool button = false;                 // injected (true = pressed)
    bool led[6] = {};                    // captured LED state

    /* ── VTOR / flash audit ────────────────────────────────────────────── */
    uint32_t vtor = 0;
    uint32_t flash_writes = 0;           // count of any app flash program/erase

    /** Any program/erase the application attempts is audited here. */
    void flash_program(uint32_t addr) {
        ++flash_writes;
        if (addr >= BOOTLOADER_LO && addr <= BOOTLOADER_HI)
            brick("application wrote the BOOTLOADER region 0x08000000-0x08000FFF");
        if (addr >= OPTION_LO && addr <= OPTION_HI)
            brick("application wrote OPTION BYTES (RDP/WRP) — irreversible brick risk");
    }
    void set_rdp(uint8_t level) {
        if (level >= 2) brick("application set RDP Level 2 — PERMANENT brick");
    }

    /* ── reset + time advance ──────────────────────────────────────────── */
    /** Mark the MCU as reset (called by the harness after reset_pending). */
    void on_reset() {
        if (reset_count) last_reset_interval_ms = time_ms - last_reset_ms;
        last_reset_ms = time_ms;
        ++reset_count;
        reset_pending = false;
        // peripherals re-init on reset; queues persist (the bus peers keep talking)
        iwdg_started = false; iwdg_last_reload_ms = time_ms;
    }

    /** Advance simulated time; fire the IWDG reset if it was not reloaded. */
    void advance(uint32_t dt_ms) {
        time_ms += dt_ms;
        if (iwdg_started && (time_ms - iwdg_last_reload_ms) >= iwdg_timeout_ms)
            reset_pending = true;
    }

    void brick(const char* why) { if (!bricked) { bricked = true; brick_reason = why; } }
};

/** The single chip instance the sim HAL drives and the harness inspects. */
SimChip& chip();

} // namespace sim
} // namespace dash

#endif // DASH_SIM_CHIP_H
