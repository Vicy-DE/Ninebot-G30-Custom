/**
 * @file ble_sim.cpp
 * @brief End-to-end BLE session simulator for the nRF51822 Bluetooth firmware.
 *
 * Runs the real `Nrf51Firmware` (the reconstructed BLE bridge) on the host
 * `SimNrf51Hardware` and drives a full session — advertising, a phone connecting,
 * the MiIO pairing handshake, and an **end-to-end Ninebot register read**
 * (phone → nRF51 → STM32 → nRF51 → phone notification). It then exercises the
 * new custom modules that the app-compatible firmware adds: `mode_ctrl`
 * (0xAA/0xAB → Haystack), the FindMy/`haystack` advertisement, and the
 * `vesc_tunnel` framing for VESC Tool over a 2nd NUS.
 *
 * Prints an annotated transcript and a PASS/FAIL verdict. Exit 0 on all-pass.
 *
 * Note: a real BLE radio / Nordic SoftDevice cannot run in any chip emulator,
 * so this is a functional simulation at the firmware/HAL boundary (the
 * SoftDevice is modeled by `SimSoftDevice`). Unit coverage of the modules lives
 * in test_nrf51822.cpp / test_new_modules.cpp; this is the integrated scenario.
 */
#include "sim_nrf51.h"
#include "nrf51_firmware.h"
#include "ninebot_protocol_verified.hpp"
#include "haystack.h"
#include "mode_ctrl.h"
#include "vesc_tunnel.h"

#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

namespace nv = ninebot_verified;
using namespace ninebot;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* msg) {
    printf("    [%s] %s\n", ok ? " ok " : "FAIL", msg);
    if (ok) ++g_pass; else ++g_fail;
}
static void hex(const char* label, const uint8_t* d, size_t n) {
    printf("      %s", label);
    for (size_t i = 0; i < n; ++i) printf(" %02X", d[i]);
    printf("\n");
}

int main() {
    printf("=========================================================\n");
    printf(" nRF51822 Bluetooth firmware — BLE session simulator\n");
    printf("=========================================================\n");

    sim::SimNrf51Hardware hw;
    nrf51::Nrf51Hal hal = hw.toHal();
    nrf51::Nrf51Firmware fw(hal);
    fw.init();

    /* 1 — Advertising the app scans for ------------------------------------ */
    printf("  1) Advertising:\n");
    const auto& adv = hw.softdevice.advData();
    std::string name(adv.begin() + 2, adv.begin() + 2 + (adv.size() > 2 ? adv[0] - 1 : 0));
    printf("      adv name = \"%s\"\n", name.c_str());
    check(hw.softdevice.isAdvertising(), "SoftDevice is advertising");
    check(name == "NBScooter0001", "advertises the name the phone app scans for");

    /* 2 — Phone connects --------------------------------------------------- */
    printf("  2) Phone connects over BLE:\n");
    hw.softdevice.injectConnect(0x0001);
    fw.mainLoopIteration();
    check(fw.isBleConnected(), "BLE connected");
    check(!hw.softdevice.isAdvertising(), "advertising stopped on connect");
    check(fw.miioState() == nrf51::MiioState::WAIT_AUTH, "MiIO auth handshake started");

    /* 3 — MiIO pairing handshake (so the original app bonds) --------------- */
    printf("  3) MiIO pairing handshake:\n");
    uint8_t authd[4] = {0x01, 0x02, 0x03, 0x04};
    fw.onGattsWrite(0xFFFF, authd, 4);                 // non-NUS handle -> MiIO path
    uint8_t token[16];
    for (int i = 0; i < 16; ++i) token[i] = uint8_t(0x10 + i);
    fw.onGattsWrite(0xFFFF, token, 16);
    uint8_t sn[] = "N2GWX0012345678";
    fw.onGattsWrite(0xFFFF, sn, 15);
    check(fw.miioState() == nrf51::MiioState::FLASH_REGISTERED, "MiIO reached FLASH_REGISTERED");
    check(fw.isMiioFlashRegistered() && hw.psm.hasBlock(0x0001), "session token persisted to PSM");

    /* 4 — End-to-end register read (BLE <-> UART bridge) ------------------- */
    printf("  4) Phone reads battery (reg 0x22) end-to-end:\n");
    uint8_t cccd[2] = {0x01, 0x00};
    fw.onGattsWrite(0x12, cccd, 2);                    // enable notifications (NUS CCCD)

    uint8_t req[16];
    uint8_t cnt = 2;
    size_t reqLen = nv::buildPacket(nv::APP, nv::ESC, nv::CMD_READ, 0x22, &cnt, 1, req);
    hex("phone -> NUS  :", req, reqLen);
    fw.onGattsWrite(0x11, req, uint16_t(reqLen));      // phone writes the NUS TX char
    fw.drainUartTx();
    const auto& toStm = hw.uart.txData();
    hex("nRF  -> STM32 :", toStm.data(), toStm.size());
    bool relayed_ok = toStm.size() == reqLen &&
                      std::equal(toStm.begin(), toStm.end(), req);
    check(relayed_ok, "phone read relayed verbatim to the STM32 over UART");

    // The STM32/ESC answers with battery = 80 (0x50); feed it back over UART.
    uint8_t val[2] = {0x50, 0x00};
    uint8_t resp[16];
    size_t respLen = nv::buildPacket(nv::ESC, nv::APP, nv::CMD_READ_RESP, 0x22, val, 2, resp);
    for (size_t i = 0; i < respLen; ++i) fw.uart0RxIsr(resp[i]);
    bool queued = fw.bleTxQueue().size() == 1 &&
                  fw.bleTxQueue()[0].size() == respLen &&
                  std::equal(fw.bleTxQueue()[0].begin(), fw.bleTxQueue()[0].end(), resp);
    check(queued, "STM32 response queued for BLE notification");

    fw.drainBleTx();
    const auto& note = hw.softdevice.lastNotification();
    hex("nRF  -> phone :", note.data(), note.size());
    bool notified = hw.softdevice.notificationsSent() >= 1 &&
                    note.size() == respLen &&
                    std::equal(note.begin(), note.end(), resp);
    check(notified, "phone notified with the response (battery byte = 0x50 = 80)");

    /* 5 — Sleep: mode switch to Apple FindMy / Haystack -------------------- */
    printf("  5) Sleep -> Haystack (FindMy) mode:\n");
    nrf51::ModeCtrl mode;
    bool toHay = mode.onUartByte(nrf51::CMD_ENTER_HAYSTACK);   // STM32 sends 0xAA
    check(toHay && mode.isHaystack(), "0xAA from STM32 -> HAYSTACK mode");

    uint8_t key[28];
    for (int i = 0; i < 28; ++i) key[i] = uint8_t(i + 1);
    key[0] = 0x85;
    uint8_t findmy[31];
    nrf51::haystack::buildAdv(key, findmy);
    hex("FindMy adv    :", findmy, 8);
    check(findmy[0] == 0x1E && findmy[1] == 0xFF && findmy[2] == 0x4C && findmy[4] == 0x12,
          "FindMy advertisement is well-formed (Apple 0x004C, type 0x12)");
    bool back = mode.onUartByte(nrf51::CMD_ENTER_NORMAL);      // STM32 wakes (0xAB)
    check(back && mode.isNormal(), "0xAB from STM32 -> back to NORMAL");

    /* 6 — VESC Tool over the 2nd NUS (tunnel framing) --------------------- */
    printf("  6) VESC Tool packet over the 2nd NUS tunnel:\n");
    uint8_t vescpl[1] = {0x00};                                // COMM_FW_VERSION request
    uint8_t framed[16];
    size_t fn = nrf51::vesc::frame(vescpl, 1, framed);
    hex("VESC framed   :", framed, fn);
    const uint8_t* outpl; size_t outlen;
    bool tunnel = nrf51::vesc::unframe(framed, fn, &outpl, &outlen) &&
                  outlen == 1 && outpl[0] == 0x00;
    check(tunnel, "VESC packet frames + unframes (CRC ok) for VESC Tool");

    /* Verdict -------------------------------------------------------------- */
    printf("---------------------------------------------------------\n");
    printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0)
        printf(" VERDICT: the BLE firmware works in the simulator — app pairs,\n"
               "          reads telemetry end-to-end, and the custom BLE features run.\n");
    else
        printf(" VERDICT: FAILURES present.\n");
    printf("=========================================================\n");
    return g_fail ? 1 : 0;
}
