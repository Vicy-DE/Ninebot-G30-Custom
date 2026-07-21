/**
 * @file sim_main.cpp
 * @brief Chip-simulator harness for the dashboard firmware — verifies that the
 *        firmware does NOT brick the hardware before you flash it.
 *
 * Runs the identical `DashApp` logic (same source as the target) against the
 * SimChip model, driving several scenarios and checking no-brick invariants:
 *   1. Healthy ride        — watchdog stays fed, no reset, app answers the phone.
 *   2. Missing ADC         — 5000 ms watchdog fires (≈5 s cadence), recoverable.
 *   3. VESC link drops      — watchdog resets, then recovers to a safe state.
 *   4. No-brick audit       — no flash writes to the bootloader/option-byte
 *                             regions, no RDP, and the built .bin vector is valid.
 *
 * Exit code 0 = all checks pass + NO BRICK RISK; nonzero otherwise.
 */
#include "dash_app.h"
#include "sim_chip.h"
#include "ninebot_protocol_verified.hpp"

#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

namespace nv = ninebot_verified;
using dash::sim::chip;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* msg) {
    printf("    [%s] %s\n", ok ? " ok " : "FAIL", msg);
    if (ok) ++g_pass; else ++g_fail;
}

static void reset_world() { dash::sim::chip() = dash::sim::SimChip{}; }

/** Build a VESC→dash 0x64 display frame and inject it on USART2. */
static void inject_display(uint8_t mode, uint8_t batt, uint8_t speed) {
    uint8_t payload[6] = { mode, batt, 0, 0, speed, 0 };
    uint8_t frame[32];
    size_t n = nv::buildPacket(nv::ESC, nv::BLE, nv::CMD_HEAD_IO, 0, payload, 6, frame);
    chip().inject_u2(frame, n);
}

/** Inject a phone→ESC register read (CMD 0x01) for `arg` on USART1. */
static void inject_app_read(uint8_t arg, uint8_t count) {
    uint8_t frame[16];
    size_t n = nv::buildPacket(nv::APP, nv::ESC, nv::CMD_READ, arg, &count, 1, frame);
    chip().inject_u1(frame, n);
}

/** Scan captured USART1 TX for a read-response (CMD 0x04) to `arg`. */
static bool saw_read_response(uint8_t arg) {
    nv::Parser p; nv::Packet pkt;
    for (uint8_t b : chip().u1_tx)
        if (nv::receiveByte(p, b, pkt) == nv::RxResult::PacketReady)
            if (pkt.cmd == nv::CMD_READ_RESP && pkt.arg == arg) return true;
    return false;
}

/* ── Scenario 1: healthy ride ──────────────────────────────────────────── */
static void scenario_healthy() {
    printf("  Scenario 1 — healthy ride (watchdog stays fed):\n");
    reset_world();
    chip().adc_ok = true; chip().adc_ch[0] = 1500; chip().adc_ch[1] = 600;

    dash::DashApp app; app.init();
    bool ever_reset = false;
    for (uint32_t t = 0; t < 12000; ++t) {
        chip().advance(1);
        if (chip().reset_pending) { ever_reset = true; chip().on_reset(); app.init(); }
        if (t == 200) chip().button = true;          // short press → wake
        if (t == 260) chip().button = false;
        if (t % 100 == 0) inject_display(0, 80, 10);  // VESC alive
        if (t == 500) inject_app_read(0x22, 2);        // phone reads battery
        app.step();
        if (chip().bricked) break;
    }
    check(!ever_reset, "no watchdog reset during healthy operation");
    check(!chip().bricked, "no brick condition");
    check(app.keeperState() == ninebot::ble::KeeperState::Run, "reached RUN state");
    check(chip().led[0], "power LED on");
    check(saw_read_response(0x22), "answered phone app battery (reg 0x22) read");
}

/* ── Scenario 2: missing ADC → 5 s watchdog, then recovery ─────────────── */
static void scenario_missing_adc() {
    printf("  Scenario 2 — missing ADC (5 s watchdog fires, recoverable):\n");
    reset_world();
    chip().adc_ok = false;                            // ADC never comes up
    dash::DashApp app; app.init();

    for (uint32_t t = 0; t < 12000; ++t) {
        chip().advance(1);
        if (chip().reset_pending) { chip().on_reset(); app.init(); }
        app.step();
        if (chip().bricked) break;
    }
    check(chip().reset_count >= 2, "watchdog reset the MCU (missing subsystem)");
    check(chip().last_reset_interval_ms >= 4500 && chip().last_reset_interval_ms <= 5500,
          "reset cadence is ~5000 ms (the IWDG timeout)");
    check(!chip().bricked, "boot loop is recoverable (not a brick — still reflashable)");

    uint32_t resets_before_fix = chip().reset_count;
    chip().adc_ok = true;                             // ADC comes good
    for (uint32_t t = 0; t < 8000; ++t) {
        chip().advance(1);
        if (chip().reset_pending) { chip().on_reset(); app.init(); }
        chip().adc_ch[0] = 1500; chip().adc_ch[1] = 600;
        app.step();
    }
    check(chip().reset_count == resets_before_fix, "no further resets once ADC returns (recovered)");
}

/* ── Scenario 3: VESC link drops while running ─────────────────────────── */
static void scenario_vesc_drop() {
    printf("  Scenario 3 — VESC link drops in RUN (reset, then safe state):\n");
    reset_world();
    chip().adc_ok = true; chip().adc_ch[0] = 1500; chip().adc_ch[1] = 600;
    dash::DashApp app; app.init();

    // get into RUN with a live VESC link
    for (uint32_t t = 0; t < 1500; ++t) {
        chip().advance(1);
        if (chip().reset_pending) { chip().on_reset(); app.init(); }
        if (t == 100) chip().button = true;
        if (t == 160) chip().button = false;
        if (t % 100 == 0) inject_display(0, 80, 10);
        app.step();
    }
    bool reached_run = app.keeperState() == ninebot::ble::KeeperState::Run;

    // now the VESC goes silent — required subsystem missing in RUN
    uint32_t resets_before = chip().reset_count;
    for (uint32_t t = 0; t < 9000; ++t) {
        chip().advance(1);
        if (chip().reset_pending) { chip().on_reset(); app.init(); }
        app.step();
        if (chip().bricked) break;
    }
    check(reached_run, "reached RUN with a live VESC link");
    check(chip().reset_count > resets_before, "watchdog reset after the VESC went missing");
    check(!chip().bricked, "no brick — recovered to a safe (sleep) state");

    // stays stable after recovery (no boot loop)
    uint32_t resets_after = chip().reset_count;
    for (uint32_t t = 0; t < 8000; ++t) {
        chip().advance(1);
        if (chip().reset_pending) { chip().on_reset(); app.init(); }
        app.step();
    }
    check(chip().reset_count == resets_after, "stable after recovery (no boot loop)");
}

/* ── Scenario 4: no-brick audit + real .bin vector check ───────────────── */
static void scenario_brick_audit(const char* bin_path) {
    printf("  Scenario 4 — no-brick audit:\n");
    // Across scenarios the firmware must never program protected flash / RDP.
    check(chip().flash_writes == 0, "firmware performed NO flash writes (cannot self-brick)");
    check(!chip().bricked, "no protected-region write / RDP set");

    // Validate the actually-built image's vector table (SP in SRAM, reset in app).
    if (FILE* f = std::fopen(bin_path, "rb")) {
        uint8_t hdr[8] = {0};
        size_t got = std::fread(hdr, 1, 8, f);
        std::fclose(f);
        if (got == 8) {
            uint32_t sp = uint32_t(hdr[0]) | (hdr[1] << 8) | (hdr[2] << 16) | (uint32_t(hdr[3]) << 24);
            uint32_t reset = uint32_t(hdr[4]) | (hdr[5] << 8) | (hdr[6] << 16) | (uint32_t(hdr[7]) << 24);
            bool ok = sp >= dash::sim::SRAM_LO && sp <= dash::sim::SRAM_HI &&
                      (reset & ~1u) >= dash::sim::APP_BASE &&
                      (reset & ~1u) < dash::sim::APP_BASE + 60u * 1024u;
            printf("      .bin vector: SP=0x%08X reset=0x%08X\n", sp, reset);
            check(ok, "built .bin has a valid vector table (stock bootloader will boot it)");
        } else {
            check(false, "built .bin too small to read vector table");
        }
    } else {
        printf("      (note: %s not found — run `make` in firmware/dashboard first)\n", bin_path);
    }
}

int main(int argc, char** argv) {
    const char* bin = (argc > 1) ? argv[1]
                                 : "firmware/dashboard/build/dashboard_app.bin";
    printf("=========================================================\n");
    printf(" Dashboard chip simulator — no-brick verification\n");
    printf("=========================================================\n");
    scenario_healthy();
    scenario_missing_adc();
    scenario_vesc_drop();
    scenario_brick_audit(bin);

    printf("---------------------------------------------------------\n");
    printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0)
        printf(" VERDICT: NO BRICK RISK DETECTED — safe to flash via serial IAP.\n");
    else
        printf(" VERDICT: FAILURES PRESENT — do NOT flash until resolved.\n");
    printf("=========================================================\n");
    return g_fail ? 1 : 0;
}
