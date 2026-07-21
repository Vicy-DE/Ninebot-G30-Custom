/**
 * @file test_new_modules.cpp
 * @brief Host tests for the new custom-firmware modules:
 *        daly_soft_uart, dash_bridge, dash_keeper (STM32 dashboard) and
 *        vesc_tunnel, haystack, mode_ctrl (nRF51 BLE).
 *
 * These lock the on-wire byte layouts so the firmware stays compatible with the
 * VESC lisp, the Daly BMS, the original phone app, and Apple FindMy.
 */

#include "test_framework.h"
#include "ninebot_protocol_verified.hpp"
#include "daly_soft_uart.h"
#include "dash_bridge.h"
#include "dash_keeper.h"
#include "vesc_tunnel.h"
#include "haystack.h"
#include "mode_ctrl.h"
#include "watchdog_supervisor.h"
#include "odometer_store.h"

namespace nv = ninebot_verified;

/** Keeps a Parser alive so the decoded Packet's payload pointer stays valid. */
struct FrameParser {
    nv::Parser p;
    nv::Packet pkt{};
    bool feed(const uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; ++i)
            if (nv::receiveByte(p, buf[i], pkt) == nv::RxResult::PacketReady)
                return true;
        return false;
    }
};

/* ===================================================================== Daly */

TEST(Daly, ReadRequestBytes) {
    uint8_t f[13];
    size_t n = ninebot::daly::buildRead(ninebot::daly::CMD_SOC, f);
    ASSERT_EQ(n, size_t(13));
    ASSERT_EQ(int(f[0]), 0xA5);
    ASSERT_EQ(int(f[1]), 0x40);
    ASSERT_EQ(int(f[2]), 0x90);
    ASSERT_EQ(int(f[3]), 0x08);
    ASSERT_EQ(int(f[12]), 0x7D);   // checksum
    ASSERT_TRUE(ninebot::daly::validate(f, ninebot::daly::CMD_SOC));
}

TEST(Daly, DischargeOffFrame) {
    uint8_t f[13];
    ninebot::daly::buildMosControl(ninebot::daly::CMD_DISCHARGE_MOS, false, f);
    ASSERT_EQ(int(f[2]), 0xD9);
    ASSERT_EQ(int(f[4]), 0x00);    // OFF
    ASSERT_TRUE(ninebot::daly::validate(f, ninebot::daly::CMD_DISCHARGE_MOS));
    uint8_t on[13];
    ninebot::daly::buildMosControl(ninebot::daly::CMD_DISCHARGE_MOS, true, on);
    ASSERT_EQ(int(on[4]), 0x01);   // ON
    ASSERT_EQ(int(on[12]), 0xC7);
}

TEST(Daly, ParseSoc) {
    // A5 01 90 08 | V=0x0190(40.0V) | gather 0 | I=0x7594(+10.0A) | SOC=0x0258(60.0%)
    uint8_t r[13] = {0xA5,0x01,0x90,0x08, 0x01,0x90, 0x00,0x00, 0x75,0x94, 0x02,0x58, 0x00};
    r[12] = ninebot::daly::checksum(r);
    ASSERT_EQ(int(r[12]), 0x32);
    ninebot::daly::PackInfo info{};
    ASSERT_TRUE(ninebot::daly::parseSoc(r, info));
    ASSERT_EQ(int(info.voltage_dV), 400);
    ASSERT_EQ(int(info.current_dA), 100);
    ASSERT_EQ(int(info.soc_dPct), 600);
}

TEST(Daly, ClientStreamRoundTrip) {
    std::vector<uint8_t> sent;
    ninebot::daly::PackInfo got{}; bool fired = false;
    ninebot::daly::DalyClient cli([&](uint8_t b){ sent.push_back(b); });
    cli.onSoc([&](const ninebot::daly::PackInfo& p){ got = p; fired = true; });
    cli.requestSoc();
    ASSERT_EQ(sent.size(), size_t(13));
    ASSERT_EQ(int(sent[2]), 0x90);
    // Feed back a synthetic response and confirm dispatch.
    uint8_t r[13] = {0xA5,0x01,0x90,0x08, 0x02,0x8A, 0x00,0x00, 0x75,0x30, 0x03,0xE8, 0x00};
    r[12] = ninebot::daly::checksum(r);
    for (uint8_t b : r) cli.receiveByte(b);
    ASSERT_TRUE(fired);
    ASSERT_EQ(int(got.voltage_dV), 650);   // 0x028A = 65.0V
    ASSERT_EQ(int(got.current_dA), 0);     // 0x7530 - 30000 = 0
    ASSERT_EQ(int(got.soc_dPct), 1000);    // 0x03E8 = 100.0%
}

/* =============================================================== DashBridge */

TEST(DashBridge, AppReadsSynthesizedBattery) {
    ninebot::ble::DashBridge br;
    ninebot::ble::VescTelemetry t;
    t.battery_pct = 77; t.speed_kmh_x10 = 250; t.voltage_V_x100 = 5200;
    br.updateTelemetry(t);
    ASSERT_EQ(int(br.getReg(0x22)), 77);
    ASSERT_EQ(int(br.getReg(0x26)), 250);

    // App reads battery level (ARG 0x22) addressed to the ESC (0x20).
    uint8_t req[32];
    uint8_t cnt = 2;
    size_t rn = nv::buildPacket(nv::APP, nv::ESC, nv::CMD_READ, 0x22, &cnt, 1, req);
    FrameParser fp;
    ASSERT_TRUE(fp.feed(req, rn));
    uint8_t resp[64];
    size_t respLen = br.onAppPacket(fp.pkt, resp);
    ASSERT_GT(respLen, size_t(0));
    FrameParser fr;
    ASSERT_TRUE(fr.feed(resp, respLen));
    ASSERT_EQ(int(fr.pkt.cmd), int(nv::CMD_READ_RESP));
    ASSERT_EQ(int(fr.pkt.arg), 0x22);
    ASSERT_EQ(int(fr.pkt.payload[0]), 77);   // little-endian low byte
}

TEST(DashBridge, ThrottleFrameMatchesLispOffsets) {
    ninebot::ble::DashBridge br;
    uint8_t out[32];
    size_t n = br.buildThrottleFrame(200, 10, out);
    FrameParser fp;
    ASSERT_TRUE(fp.feed(out, n));
    ASSERT_EQ(int(fp.pkt.cmd), 0x65);
    ASSERT_EQ(int(fp.pkt.src), int(nv::BLE));
    ASSERT_EQ(int(fp.pkt.dst), int(nv::ESC));
    // g30_dash.lisp reads throttle at payload[1], brake at payload[2].
    ASSERT_EQ(int(fp.pkt.payload[1]), 200);
    ASSERT_EQ(int(fp.pkt.payload[2]), 10);
}

TEST(DashBridge, ParseDisplayFrameUpdatesRegisters) {
    ninebot::ble::DashBridge br;
    uint8_t payload[6] = {4 /*mode*/, 80 /*batt*/, 1 /*light*/, 0 /*beep*/,
                          25 /*speed km/h*/, 0 /*error*/};
    uint8_t frame[32];
    size_t n = nv::buildPacket(nv::ESC, nv::BLE, nv::CMD_HEAD_IO, 0x00, payload, 6, frame);
    FrameParser fp;
    ASSERT_TRUE(fp.feed(frame, n));
    ninebot::ble::DisplayState ds;
    ASSERT_TRUE(br.parseDisplayFrame(fp.pkt, &ds));
    ASSERT_EQ(int(ds.batt), 80);
    ASSERT_EQ(int(br.getReg(0x22)), 80);
    ASSERT_EQ(int(br.getReg(0x26)), 250);    // 25 km/h → 0.1 km/h units
    ASSERT_EQ(int(br.getReg(0x1B)), 0);
}

TEST(DashBridge, WriteModeRaisesActionAndNoSpeedClamp) {
    ninebot::ble::DashBridge br;
    ninebot::ble::VescTelemetry t; t.speed_kmh_x10 = 300; br.updateTelemetry(t);

    // Write mode 0x75 = 2 (Sport)
    uint8_t v = 2;
    uint8_t w[32];
    size_t wn = nv::buildPacket(nv::APP, nv::ESC, nv::CMD_WRITE, 0x75, &v, 1, w);
    FrameParser fp; ASSERT_TRUE(fp.feed(w, wn));
    br.onAppPacket(fp.pkt, nullptr);
    ASSERT_TRUE(br.lastAction() == ninebot::ble::BridgeAction::SetMode);
    ASSERT_EQ(int(br.actionValue()), 2);
    ASSERT_EQ(int(br.getReg(0x75)), 2);

    // Write a low speed limit (0x73) — must NOT change reported speed (Req 4).
    uint8_t lim = 5;
    uint8_t w2[32];
    size_t wn2 = nv::buildPacket(nv::APP, nv::ESC, nv::CMD_WRITE, 0x73, &lim, 1, w2);
    FrameParser fp2; ASSERT_TRUE(fp2.feed(w2, wn2));
    br.onAppPacket(fp2.pkt, nullptr);
    ASSERT_TRUE(br.lastAction() == ninebot::ble::BridgeAction::None);
    ASSERT_EQ(int(br.getReg(0x26)), 300);    // speed untouched
}

/* =============================================================== DashKeeper */

TEST(DashKeeper, WakeRunPowerOffCycle) {
    using namespace ninebot::ble;
    DashKeeper k;
    ASSERT_EQ(k.begin(), uint32_t(EFF_ENTER_STOP));
    ASSERT_TRUE(k.state() == KeeperState::DeepSleep);

    uint32_t e = k.onButtonPress(1000);
    ASSERT_TRUE(k.state() == KeeperState::Wake);
    ASSERT_TRUE(e & EFF_DALY_ON);
    ASSERT_TRUE(e & EFF_NRF_NORMAL);

    k.onVescLinkUp();
    ASSERT_TRUE(k.state() == KeeperState::Run);

    k.onButtonPress(5000);                       // start of a long press
    uint32_t off = k.onButtonRelease(5000 + 1600); // ≥1500 ms → power off
    ASSERT_TRUE(k.state() == KeeperState::DeepSleep);
    ASSERT_TRUE(off & EFF_DALY_OFF);
    ASSERT_TRUE(off & EFF_NRF_HAYSTACK);
    ASSERT_TRUE(off & EFF_VESC_DISABLE);
}

TEST(DashKeeper, WakeTimeoutFallsBackToSleep) {
    using namespace ninebot::ble;
    DashKeeper k;
    k.begin();
    k.onButtonPress(0);
    ASSERT_TRUE(k.state() == KeeperState::Wake);
    uint32_t e = k.onTick(4000);                 // VESC never appeared
    ASSERT_TRUE(k.state() == KeeperState::DeepSleep);
    ASSERT_TRUE(e & EFF_DALY_OFF);
}

/* =============================================================== VESC tunnel */

TEST(VescTunnel, Crc16KnownVector) {
    const uint8_t msg[9] = {'1','2','3','4','5','6','7','8','9'};
    ASSERT_EQ(int(ninebot::nrf51::vesc::crc16(msg, 9)), 0x31C3); // CRC16/XMODEM
}

TEST(VescTunnel, ShortFrameRoundTrip) {
    uint8_t payload[1] = {0x04};                 // e.g. COMM_GET_VALUES
    uint8_t framed[16];
    size_t n = ninebot::nrf51::vesc::frame(payload, 1, framed);
    ASSERT_EQ(int(framed[0]), 0x02);
    const uint8_t* p; size_t plen;
    ASSERT_TRUE(ninebot::nrf51::vesc::unframe(framed, n, &p, &plen));
    ASSERT_EQ(plen, size_t(1));
    ASSERT_EQ(int(p[0]), 0x04);
}

TEST(VescTunnel, LongFrameUsesLongHeader) {
    uint8_t payload[300];
    for (int i = 0; i < 300; ++i) payload[i] = uint8_t(i);
    uint8_t framed[320];
    size_t n = ninebot::nrf51::vesc::frame(payload, 300, framed);
    ASSERT_EQ(int(framed[0]), 0x03);             // long start byte
    const uint8_t* p; size_t plen;
    ASSERT_TRUE(ninebot::nrf51::vesc::unframe(framed, n, &p, &plen));
    ASSERT_EQ(plen, size_t(300));
    ASSERT_EQ(int(p[299]), int(uint8_t(299)));
}

/* ================================================================= Haystack */

TEST(Haystack, AdvLayout) {
    uint8_t key[28];
    for (int i = 0; i < 28; ++i) key[i] = uint8_t(i);
    key[0] = 0x85;                               // upper bits = 0b10 = 2
    uint8_t adv[31];
    ninebot::nrf51::haystack::buildAdv(key, adv, /*status*/0x00);
    ASSERT_EQ(int(adv[0]), 0x1E);
    ASSERT_EQ(int(adv[1]), 0xFF);
    ASSERT_EQ(int(adv[2]), 0x4C);
    ASSERT_EQ(int(adv[3]), 0x00);
    ASSERT_EQ(int(adv[4]), 0x12);
    ASSERT_EQ(int(adv[5]), 0x19);
    ASSERT_EQ(int(adv[7]), 6);                   // key[6]
    ASSERT_EQ(int(adv[28]), 27);                 // key[27]
    ASSERT_EQ(int(adv[29]), 2);                  // key[0] >> 6

    uint8_t mac[6];
    ninebot::nrf51::haystack::macFromKey(key, mac);
    ASSERT_EQ(int(mac[0]), 0xC5);                // 0x85 | 0xC0
}

TEST(Haystack, KeySchedule) {
    using ninebot::nrf51::haystack::keyIndex;
    ASSERT_EQ(keyIndex(1000, 1000, 96), uint32_t(0));
    ASSERT_EQ(keyIndex(1000 + 1801, 1000, 96), uint32_t(2)); // 1801/900 = 2
    ASSERT_EQ(keyIndex(500, 1000, 96), uint32_t(0));         // now < base
}

/* ================================================================ ModeCtrl */

TEST(ModeCtrl, HaystackSwitch) {
    ninebot::nrf51::ModeCtrl m;
    ASSERT_TRUE(m.isNormal());
    ASSERT_TRUE(m.onUartByte(0xAA));             // changed
    ASSERT_TRUE(m.isHaystack());
    ASSERT_FALSE(m.onUartByte(0x12));            // bridge traffic — no change
    ASSERT_TRUE(m.isHaystack());
    ASSERT_TRUE(m.onUartByte(0xAB));             // back to normal
    ASSERT_TRUE(m.isNormal());
}

/* ================================================================ Watchdog */

TEST(Watchdog, IwdgParamsFor5000ms) {
    // 5000 ms at LSI 40 kHz → smallest prescaler that fits is /64 (pr=4),
    // reload 3124, realising exactly 5000 ms (64*3125/40000 = 5.000 s).
    ninebot::IwdgParams p = ninebot::iwdg_params(ninebot::IWDG_TIMEOUT_MS);
    ASSERT_EQ(int(p.pr), 4);
    ASSERT_EQ(int(p.rlr), 3124);
    ASSERT_EQ(p.actual_ms, uint32_t(5000));
}

TEST(Watchdog, FeedsOnlyWhenRequiredSubsystemsFresh) {
    using WS = ninebot::WatchdogSupervisor;
    WS sup(/*staleMs*/2000);
    sup.require(WS::LOOP);
    sup.require(WS::UART_VESC);

    // Nothing kicked yet → must NOT feed (required subsystems missing).
    ASSERT_FALSE(sup.shouldFeed(0));

    sup.kick(WS::LOOP, 100);
    sup.kick(WS::UART_VESC, 100);
    ASSERT_TRUE(sup.shouldFeed(100));
    ASSERT_TRUE(sup.shouldFeed(2000));           // 1900 ms old ≤ 2000 → still fresh

    // VESC link goes missing: its heartbeat is now stale → stop feeding → reset.
    ASSERT_FALSE(sup.shouldFeed(2200));          // UART_VESC 2100 ms old > 2000

    // VESC comes back → feed resumes (only if LOOP is also fresh).
    sup.kick(WS::UART_VESC, 2200);
    sup.kick(WS::LOOP, 2200);
    ASSERT_TRUE(sup.shouldFeed(2200));
}

TEST(Watchdog, NonRequiredSubsystemDoesNotBlockFeed) {
    using WS = ninebot::WatchdogSupervisor;
    WS sup(2000);
    sup.require(WS::LOOP);                        // only LOOP required
    sup.kick(WS::LOOP, 50);
    // Daly never seen, but it is not required (e.g. stock-BMS build) → still feed.
    ASSERT_TRUE(sup.shouldFeed(50));
}

/* ============================================================== Odometer */

/** Host model of the 4 KB user-data flash region (1->0 program semantics). */
struct SimUserFlash {
    uint8_t mem[4096];
    SimUserFlash() { for (auto& b : mem) b = 0xFF; }
    static void rd(void* c, uint32_t off, void* buf, uint32_t len) {
        std::memcpy(buf, ((SimUserFlash*)c)->mem + off, len);
    }
    static void pg(void* c, uint32_t off, const void* data, uint32_t len) {
        uint8_t* m = ((SimUserFlash*)c)->mem + off;     // flash AND: can only clear bits
        const uint8_t* d = (const uint8_t*)data;
        for (uint32_t i = 0; i < len; ++i) m[i] &= d[i];
    }
    static void er(void* c, uint32_t pageIdx) {
        std::memset(((SimUserFlash*)c)->mem + pageIdx * 1024, 0xFF, 1024);
    }
    ninebot::ble::OdoFlash io() { return {this, rd, pg, er}; }
};

TEST(Odometer, FreshDeviceReadsZero) {
    SimUserFlash fl;
    ninebot::ble::OdometerStore odo(fl.io());
    auto v = odo.load();
    ASSERT_EQ(int(v.seconds), 0);
    ASSERT_EQ(int(v.meters), 0);
}

TEST(Odometer, SaveThenLoadRoundTrip) {
    SimUserFlash fl;
    ninebot::ble::OdometerStore odo(fl.io());
    odo.save(7200, 12345);                       // 2 h, 12.345 km
    auto v = odo.load();
    ASSERT_EQ(int(v.seconds), 7200);
    ASSERT_EQ(int(v.meters), 12345);
    ASSERT_EQ(int(v.hours()), 2);
}

TEST(Odometer, LatestWinsAcrossPageBoundary) {
    SimUserFlash fl;
    ninebot::ble::OdometerStore odo(fl.io());
    // 70 power-offs → crosses the 64-record page boundary (forces a page erase).
    for (int i = 1; i <= 70; ++i) odo.save(uint32_t(i) * 3600u, uint32_t(i) * 1000u);
    auto v = odo.load();
    ASSERT_EQ(int(v.seconds), 70 * 3600);
    ASSERT_EQ(int(v.meters), 70 * 1000);
    ASSERT_EQ(int(v.hours()), 70);
}

TEST(Odometer, PowerLossPartialWriteIgnored) {
    SimUserFlash fl;
    ninebot::ble::OdometerStore odo(fl.io());
    odo.save(3600, 5000);                        // good record in slot 0
    // Simulate a power-loss mid-write: a record with a corrupt CRC in slot 1.
    ninebot::ble::OdoRecord bad{2, 9999, 9999, 0xDEADBEEF};
    SimUserFlash::pg(&fl, sizeof(ninebot::ble::OdoRecord), &bad, sizeof(bad));
    auto v = odo.load();                          // must ignore the bad record
    ASSERT_EQ(int(v.seconds), 3600);
    ASSERT_EQ(int(v.meters), 5000);
}
