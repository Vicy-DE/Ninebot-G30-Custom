/**
 * @file nbu_xcheck.cpp
 * @brief Replay the Python sender's frame stream (_xcheck_stream.bin) through the
 *        C receiver nbu_receive(), writing the reconstructed firmware to
 *        _xcheck_got.bin. nbu_xcheck.py then diffs it against _xcheck_expected.bin.
 *        Proves nbu_send.py and nbu.c agree on the wire format byte-for-byte.
 */
extern "C" {
#include "nbu.h"
}
#include <cstdio>
#include <cstdint>
#include <vector>

static std::vector<uint8_t> g_stream;
static size_t g_pos = 0;
static uint32_t g_tick = 0;
static std::vector<uint8_t> g_got;

static void io_send(uint8_t) {}                     // ignore the bootloader's ACKs
static int io_recv(uint8_t* b, uint32_t) {
    if (g_pos >= g_stream.size()) { g_tick += 1000; return -1; }
    *b = g_stream[g_pos++]; g_tick += 1; return 0;
}
static uint32_t io_tick() { return g_tick; }
static int write_cb(const uint8_t* d, uint32_t, uint32_t len, void*) {
    for (uint32_t i = 0; i < len; ++i) g_got.push_back(d[i]);
    return 0;
}

int main() {
    FILE* f = fopen("_xcheck_stream.bin", "rb");
    if (!f) { printf("FAIL: cannot open _xcheck_stream.bin\n"); return 2; }
    int c;
    while ((c = fgetc(f)) != EOF) g_stream.push_back((uint8_t)c);
    fclose(f);

    nbu_io_t io = { io_send, io_recv, io_tick };
    uint32_t total = 0;
    nbu_result_t r = nbu_receive(&io, 0x21, write_cb, nullptr, &total);

    FILE* o = fopen("_xcheck_got.bin", "wb");
    if (o) { fwrite(g_got.data(), 1, g_got.size(), o); fclose(o); }

    printf("receiver: result=%d total=%u reconstructed=%zu bytes\n",
           (int)r, total, g_got.size());
    return (r == NBU_OK) ? 0 : 1;
}
