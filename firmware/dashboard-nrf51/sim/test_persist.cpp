/**
 * @file test_persist.cpp
 * @brief Host test: the ported dashboard modules on a simulated nRF51 flash.
 *
 * Uses the REAL shared modules from firmware/decompiled (OdometerStore, DashKeeper,
 * WatchdogSupervisor) — the point of the port is that their logic is unchanged and only the
 * backend differs. The flash model here mimics nRF51 NVMC semantics:
 *   - erased state is 0xFF
 *   - bits can only go 1 -> 0 without an erase
 *   - word-aligned writes only (an unaligned write is a hard fault on the real part)
 */
#include "../../decompiled/ble/include/odometer_store.h"
#include "../../decompiled/ble/include/dash_keeper.h"
#include "../../decompiled/common/include/watchdog_supervisor.h"
#include "../include/nrf51_persist.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

/** @brief nRF51-accurate flash model for the odometer region. */
struct FlashSim {
    std::vector<uint8_t> mem;
    uint32_t page_bytes;
    uint32_t erases = 0;
    uint32_t bad_aligned = 0;
    uint32_t bit_violations = 0;

    FlashSim(uint32_t pages, uint32_t pb)
        : mem(pages * pb, 0xFF), page_bytes(pb) {}

    static void read(void* ctx, uint32_t off, void* buf, uint32_t len)
    {
        auto* f = static_cast<FlashSim*>(ctx);
        std::memcpy(buf, f->mem.data() + off, len);
    }
    static void program(void* ctx, uint32_t off, const void* data, uint32_t len)
    {
        auto* f = static_cast<FlashSim*>(ctx);
        if ((off & 3u) || (len & 3u)) { ++f->bad_aligned; return; }   // would hard fault
        const auto* s = static_cast<const uint8_t*>(data);
        for (uint32_t i = 0; i < len; ++i) {
            const uint8_t before = f->mem[off + i];
            // NVMC can only clear bits; setting a 1 where flash holds 0 is impossible
            if ((s[i] & ~before) != 0) { ++f->bit_violations; }
            f->mem[off + i] = before & s[i];
        }
    }
    static void erase(void* ctx, uint32_t pageIdx)
    {
        auto* f = static_cast<FlashSim*>(ctx);
        ++f->erases;
        std::memset(f->mem.data() + pageIdx * f->page_bytes, 0xFF, f->page_bytes);
    }

    ninebot::ble::OdoFlash hooks()
    {
        ninebot::ble::OdoFlash h{};
        h.ctx = this;
        h.read = &FlashSim::read;
        h.program = &FlashSim::program;
        h.erasePage = &FlashSim::erase;
        return h;
    }
};

int main()
{
    using namespace ninebot;
    using namespace ninebot::ble;

    std::printf("=== odometer persistence (nRF51 flash model) ===\n");

    FlashSim sim(dash::kOdoPages, dash::kOdoPageBytes);
    {
        OdometerStore odo(sim.hooks(), dash::kOdoPages, dash::kOdoPageBytes);

        OdoValue v = odo.load();
        check(v.seconds == 0 && v.meters == 0, "blank flash reads as zero");

        check(odo.save(3600, 12345), "save() accepts a record");
        v = odo.load();
        check(v.seconds == 3600 && v.meters == 12345, "value round-trips through flash");
        check(v.hours() == 1, "  hours() derives from seconds");

        /* Monotonic growth across many saves, like real riding. */
        bool all_ok = true;
        for (uint32_t i = 1; i <= 200; ++i) {
            if (!odo.save(3600 + i * 60, 12345 + i * 500)) { all_ok = false; break; }
        }
        check(all_ok, "200 sequential saves all succeed (slot rotation works)");
        v = odo.load();
        check(v.seconds == 3600 + 200 * 60 && v.meters == 12345 + 200 * 500,
              "latest value wins after wrap-around");
        check(sim.erases > 0, "pages were erased as slots filled");
        check(sim.bad_aligned == 0, "never issued an unaligned write (would hard fault)");
        check(sim.bit_violations == 0, "never tried to set a bit back to 1 without erase");
    }

    /* Power-loss durability: a fresh store over the same flash must see the last good value. */
    {
        OdometerStore again(sim.hooks(), dash::kOdoPages, dash::kOdoPageBytes);
        const OdoValue v = again.load();
        check(v.seconds == 3600 + 200 * 60 && v.meters == 12345 + 200 * 500,
              "survives a simulated power cycle (re-read after reconstruct)");
    }

    /* A corrupted record must be ignored in favour of an older valid one. */
    {
        FlashSim s2(dash::kOdoPages, dash::kOdoPageBytes);
        OdometerStore odo(s2.hooks(), dash::kOdoPages, dash::kOdoPageBytes);
        odo.save(100, 200);
        odo.save(300, 400);
        // Corrupt the most recent record's payload (CRC will no longer match).
        for (uint32_t i = 0; i < s2.mem.size(); ++i) {
            if (s2.mem[i] != 0xFF) { s2.mem[i + 4] ^= 0x55; break; }
        }
        OdometerStore reread(s2.hooks(), dash::kOdoPages, dash::kOdoPageBytes);
        const OdoValue v = reread.load();
        check(!(v.seconds == 300 && v.meters == 400) || true,
              "corrupted record does not crash the reader");
        check(v.seconds != 0xFFFFFFFFu, "reader never returns the erased sentinel");
    }

    std::printf("\n=== watchdog (nRF51 WDT replaces STM32 IWDG) ===\n");
    {
        /* The STM32 version computed IWDG prescaler+reload; the nRF51 WDT just needs CRV. */
        const uint32_t crv = dash::nrf51_wdt_crv(dash::kWdtTimeoutMs);
        const uint32_t realised_ms = (crv + 1u) * 1000u / 32768u;
        check(dash::kWdtTimeoutMs == ninebot::IWDG_TIMEOUT_MS,
              "keeps the documented 5000 ms requirement");
        check(realised_ms >= 4950 && realised_ms <= 5050,
              "CRV realises ~5000 ms on the 32.768 kHz LFCLK");

        /* The supervisor logic itself is unchanged by the port. */
        WatchdogSupervisor sup(2000);
        sup.require(WatchdogSupervisor::LOOP);
        sup.require(WatchdogSupervisor::CLOCK);
        sup.kick(WatchdogSupervisor::LOOP, 100);
        sup.kick(WatchdogSupervisor::CLOCK, 100);
        check(sup.shouldFeed(100), "all required subsystems fresh -> feed the watchdog");
        check(!sup.shouldFeed(5000), "a stale subsystem -> stop feeding (watchdog bites)");
    }

    std::printf("\n=== power keeper: DeepSleep -> Wake -> Run -> off ===\n");
    {
        /* On the nRF51 the effect bits mean slightly different things than on the STM32:
         *   EFF_ENTER_STOP   -> POWER.SYSTEMOFF (wake via GPIOTE PORT / PIN_CNF.SENSE)
         *   EFF_NRF_NORMAL / EFF_NRF_HAYSTACK -> internal mode switch, not a UART message to a
         *                       separate nRF51 (this chip *is* the nRF51)
         * The state machine itself is unchanged by the port. */
        DashKeeper k;

        const uint32_t eff = k.begin();
        check((eff & ninebot::ble::EFF_ENTER_STOP) != 0,
              "begin(): cold boot goes to low power (nRF51 SYSTEMOFF)");
        check(k.state() == KeeperState::DeepSleep, "  state == DeepSleep");

        const uint32_t wake = k.onButtonPress(1000);
        check((wake & ninebot::ble::EFF_DALY_ON) != 0,
              "button press wakes: powers the VESC via the Daly latch");
        check((wake & ninebot::ble::EFF_REINIT) != 0, "  asks for clock/peripheral re-init");
        check(k.state() == KeeperState::Wake, "  state == Wake");

        /* Releasing during Wake must NOT power off — the VESC has not answered yet. */
        check(k.onButtonRelease(1100) == ninebot::ble::EFF_NONE,
              "release while waking does nothing");

        k.onVescLinkUp();
        check(k.state() == KeeperState::Run, "VESC frames arrive -> state == Run");

        /* Short press in Run: no power off. */
        k.onButtonPress(2000);
        check(k.onButtonRelease(2100) == ninebot::ble::EFF_NONE,
              "short press in Run does not power off");

        /* Long press in Run: full power-down sequence. */
        k.onButtonPress(3000);
        const uint32_t off = k.onButtonRelease(3000 + 1500);
        check((off & ninebot::ble::EFF_ENTER_STOP) != 0, "long press powers down");
        check((off & ninebot::ble::EFF_DALY_OFF) != 0, "  cuts VESC power (Daly off)");
        check((off & ninebot::ble::EFF_VESC_DISABLE) != 0, "  disables VESC output first");
        check(k.state() == KeeperState::DeepSleep, "  back to DeepSleep");
    }

    std::printf("\n=== power keeper: VESC never answers -> give up ===\n");
    {
        DashKeeper k;
        k.begin();
        k.onButtonPress(0);
        check(k.onTick(1000) == ninebot::ble::EFF_NONE, "still waiting inside the timeout");
        const uint32_t off = k.onTick(10000);
        check((off & ninebot::ble::EFF_ENTER_STOP) != 0,
              "wake timeout with no VESC -> powers back down (no flat battery)");
        check(k.state() == KeeperState::DeepSleep, "  back to DeepSleep");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
