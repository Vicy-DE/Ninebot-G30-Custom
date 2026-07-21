/**
 * @file test_nbu.cpp
 * @brief Host test for the NBU framed half-duplex update receiver (nbu.c).
 *        Drives nbu_receive() with framed BEGIN/DATA/END bytes and checks it
 *        reconstructs the firmware, ACKs each frame, re-ACKs duplicates, NACKs
 *        out-of-order, and drops bad-checksum frames.
 */
extern "C" {
#include "nbu.h"
}
#include <cstdio>
#include <cstdint>
#include <vector>
#include <deque>
#include <algorithm>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* m) {
    printf("    [%s] %s\n", ok ? " ok " : "FAIL", m);
    if (ok) ++g_pass; else ++g_fail;
}

struct Sim {
    std::deque<uint8_t> in;        // host -> bootloader
    std::vector<uint8_t> out;      // bootloader -> host (replies)
    std::vector<uint8_t> written;  // accepted firmware bytes
    uint32_t tick = 0;
} g;

static void io_send(uint8_t b) { g.out.push_back(b); }
static int io_recv(uint8_t* b, uint32_t) {
    if (g.in.empty()) { g.tick += 1000; return -1; }   // timeout advances time
    *b = g.in.front(); g.in.pop_front(); g.tick += 1; return 0;
}
static uint32_t io_tick() { return g.tick; }
static int write_cb(const uint8_t* d, uint32_t, uint32_t len, void*) {
    for (uint32_t i = 0; i < len; ++i) g.written.push_back(d[i]);
    return 0;
}

static void push_frame(uint8_t src, uint8_t dst, uint8_t cmd, uint8_t arg,
                       const std::vector<uint8_t>& pl, bool corrupt = false) {
    std::vector<uint8_t> body = { (uint8_t)pl.size(), src, dst, cmd, arg };
    body.insert(body.end(), pl.begin(), pl.end());
    uint16_t s = 0; for (uint8_t b : body) s = (uint16_t)(s + b);
    uint16_t ck = (uint16_t)(~s);
    if (corrupt) ck ^= 0xFF;
    g.in.push_back(0x5A); g.in.push_back(0xA5);
    for (uint8_t b : body) g.in.push_back(b);
    g.in.push_back((uint8_t)(ck & 0xFF)); g.in.push_back((uint8_t)(ck >> 8));
}

/** count ACK frames (CMD 0x06) with the given status in g.out */
static int count_replies(uint8_t status) {
    int n = 0;
    for (size_t i = 0; i + 1 < g.out.size(); ++i) {
        if (g.out[i] == 0x5A && g.out[i + 1] == 0xA5) {
            uint8_t len = g.out[i + 2], cmd = g.out[i + 5], arg = g.out[i + 6];
            if (cmd == NBU_CMD_ACK && arg == status) ++n;
            i += 2u + len + 7u - 1u;
        }
    }
    return n;
}

static const uint8_t MY = 0x21, HOST = 0x3F;

int main() {
    printf("=========================================================\n");
    printf(" NBU framed half-duplex update — receiver test\n");
    printf("=========================================================\n");

    // 200-byte "firmware", sent as BEGIN + 64-byte DATA blocks + END.
    std::vector<uint8_t> fw(200);
    for (int i = 0; i < 200; ++i) fw[i] = uint8_t(i * 7 + 3);

    push_frame(HOST, MY, NBU_CMD_BEGIN, 0, {200, 0, 0, 0});
    uint16_t seq = 0;
    for (size_t off = 0; off < fw.size(); off += 64) {
        size_t n = std::min<size_t>(64, fw.size() - off);
        std::vector<uint8_t> pl = { uint8_t(seq & 0xFF), uint8_t(seq >> 8) };
        pl.insert(pl.end(), fw.begin() + off, fw.begin() + off + n);
        push_frame(HOST, MY, NBU_CMD_DATA, 0, pl);
        ++seq;
    }
    push_frame(HOST, MY, NBU_CMD_END, 0, {});

    nbu_io_t io = { io_send, io_recv, io_tick };
    uint32_t total = 0;
    nbu_result_t r = nbu_receive(&io, MY, write_cb, nullptr, &total);

    printf("  Happy path (200 B, 4 blocks):\n");
    check(r == NBU_OK, "session ends OK on END");
    check(total == 200, "received byte count = 200");
    check(g.written == fw, "firmware reconstructed byte-for-byte");
    check(count_replies(0x00) >= 6, "ACKed BEGIN + 4 DATA + END (>=6 OK replies)");

    // Duplicate + out-of-order + bad-checksum behaviour.
    g = Sim{};
    push_frame(HOST, MY, NBU_CMD_BEGIN, 0, {64, 0, 0, 0});
    std::vector<uint8_t> blk = { 0, 0 };               // seq 0
    for (int i = 0; i < 32; ++i) blk.push_back(uint8_t(i));
    push_frame(HOST, MY, NBU_CMD_DATA, 0, blk);        // seq 0 (accepted)
    push_frame(HOST, MY, NBU_CMD_DATA, 0, blk);        // seq 0 again (duplicate)
    std::vector<uint8_t> ooo = { 9, 0 };               // seq 9 (out of order)
    for (int i = 0; i < 4; ++i) ooo.push_back(0xEE);
    push_frame(HOST, MY, NBU_CMD_DATA, 0, ooo);        // NACK
    push_frame(HOST, MY, NBU_CMD_DATA, 0, blk, /*corrupt*/true);  // dropped, no reply
    push_frame(HOST, MY, NBU_CMD_END, 0, {});
    total = 0;
    r = nbu_receive(&io, MY, write_cb, nullptr, &total);

    printf("  Robustness:\n");
    check(g.written.size() == 32, "duplicate block written only once (32 B)");
    check(count_replies(0x02) == 1, "out-of-order DATA → exactly one NACK");

    printf("---------------------------------------------------------\n");
    printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
