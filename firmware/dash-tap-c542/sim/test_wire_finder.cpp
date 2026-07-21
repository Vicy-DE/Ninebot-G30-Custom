/**
 * @file test_wire_finder.cpp
 * @brief Host test for wire_finder.c — proves the C542RC firmware can identify,
 *        from passive sniffing alone, which tapped wire (A0/PA0 vs A2/PA4) is the
 *        BLE/nRF side, the dashboard/app side, idle, or noise.
 */
extern "C" {
#include "wire_finder.h"
}
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char *m) {
    printf("    [%s] %s\n", ok ? " ok " : "FAIL", m);
    if (ok) ++g_pass; else ++g_fail;
}

/* Build a Ninebot frame (verified LEN=payload, ~sum checksum). */
static std::vector<uint8_t> frame(uint8_t src, uint8_t dst, uint8_t cmd,
                                  uint8_t arg, std::vector<uint8_t> pl,
                                  bool corrupt = false) {
    std::vector<uint8_t> body = { (uint8_t)pl.size(), src, dst, cmd, arg };
    body.insert(body.end(), pl.begin(), pl.end());
    uint16_t s = 0; for (uint8_t b : body) s = (uint16_t)(s + b);
    uint16_t ck = (uint16_t)(~s); if (corrupt) ck ^= 0xFF;
    std::vector<uint8_t> f = { WF_HDR1, WF_HDR2 };
    f.insert(f.end(), body.begin(), body.end());
    f.push_back(ck & 0xFF); f.push_back(ck >> 8);
    return f;
}

static void feed(wire_finder_t &wf, int line, const std::vector<uint8_t> &v) {
    for (uint8_t b : v) wf_feed(&wf, line, b);
}

int main() {
    printf("=========================================================\n");
    printf(" wire_finder — identify the two tapped dashboard wires\n");
    printf("=========================================================\n");

    wire_finder_t wf;
    wf_reset(&wf);

    // Line 0 (A0/PA0): BLE/nRF chatter — SRC 0x21 (BLE board) talking to App 0x3E.
    for (int i = 0; i < 5; i++)
        feed(wf, 0, frame(0x21, 0x3E, 0x64, 0x00, {(uint8_t)(10 + i), 0x00}));
    // Line 1 (A2/PA4): random non-framed noise (no 5A A5 frame).
    feed(wf, 1, {0x00, 0xFF, 0x13, 0x37, 0x42, 0xAA, 0x01, 0x02});

    wf_line_t st[WF_LINES];
    wf_classify(&wf, st);

    printf("  Scenario: A0=BLE/nRF traffic, A2=noise\n");
    check(st[0].kind == WF_NINEBOT, "A0 classified as Ninebot");
    check(st[0].frames_ok == 5, "A0 decoded 5 valid frames");
    check((st[0].addr_mask & (1u << 1)) != 0, "A0 saw BLE address 0x21");
    check(std::string(wf_role(&st[0])).find("BT") != std::string::npos,
          "A0 role names the BT/nRF side");
    check(st[1].kind == WF_NOISE, "A2 classified as noise (bytes, no frames)");

    // Fresh window: A0 idle, A2 carries dashboard/app-addressed frames; plus one
    // corrupted frame must count as bad, not valid.
    wf_reset(&wf);
    for (int i = 0; i < 3; i++)
        feed(wf, 1, frame(0x3E, 0x21, 0x65, 0x00, {0x01}));
    feed(wf, 1, frame(0x3E, 0x21, 0x65, 0x00, {0x01}, /*corrupt*/true));

    wf_classify(&wf, st);
    printf("  Scenario: A0 idle, A2=app-addressed frames (+1 corrupt)\n");
    check(st[0].kind == WF_IDLE, "A0 classified as idle (no bytes)");
    check(st[1].kind == WF_NINEBOT && st[1].frames_ok == 3, "A2 decoded 3 valid frames");
    check(st[1].frames_bad == 1, "A2 counted the corrupted frame as bad");
    check(std::string(wf_role(&st[1])).find("App") != std::string::npos,
          "A2 role names the App-side transmitter (SRC 0x3E)");

    printf("---------------------------------------------------------\n");
    printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
