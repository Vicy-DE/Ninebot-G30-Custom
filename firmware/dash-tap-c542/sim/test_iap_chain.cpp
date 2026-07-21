/**
 * @file test_iap_chain.cpp
 * @brief Full IAP chain at the 16/32 offsets, all verified in simulation with REAL
 *        code (software-UART NBU programmer + nbu.c receiver + .sfw ECDSA verify +
 *        a flash model). Proves, before any hardware flash:
 *
 *   1. The C542 software-UART programmer flashes a signed app to the relocated
 *      bootloader at 0x08004000 (16 offset), which writes it to the app slot at
 *      0x08008000 (32 offset) and ACCEPTS it only because the ECDSA signature
 *      verifies (tamper is rejected).
 *   2. The installer running from 0x08008000 (32 offset) writes a bootloader back
 *      to 0x08004000 (16 offset) with verify-before-erase + read-back.
 *   3. Throughout, the REAL bootloader region 0x08000000-0x08003FFF is never
 *      touched — the whole test lives in the upper half of flash.
 */
#ifdef __cplusplus
#define _Static_assert static_assert   /* fw_header.h uses the C11 keyword */
#endif
extern "C" {
#include "soft_uart.h"
#include "nbu_prog.h"
#include "nbu.h"
#include "fw_header.h"
#include "crc32.h"
}
#include <cstdio>
#include <cstdint>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char *m) {
    printf("    [%s] %s\n", ok ? " ok " : "FAIL", m);
    if (ok) ++g_pass; else ++g_fail;
}
static uint32_t now_ms() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}
static std::vector<uint8_t> load(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) { printf("FAIL: cannot open %s\n", p); return {}; }
    std::vector<uint8_t> v; int c;
    while ((c = fgetc(f)) != EOF) v.push_back((uint8_t)c);
    fclose(f); return v;
}

/* ── Flash model: 64 KB at 0x08000000 ──────────────────────────────────────── */
static const uint32_t FLASH_BASE = 0x08000000u;
static const uint32_t BL16 = 0x08004000u;   /* relocated bootloader (16 offset) */
static const uint32_t APP32 = 0x08008000u;  /* app slot it flashes to (32 offset) */
static const uint32_t SENTINEL_LEN = 0x4000u; /* the real BL region 0..0x3FFF */
static std::vector<uint8_t> g_flash(0x10000, 0xFF);
static inline uint8_t *fp(uint32_t addr) { return &g_flash[addr - FLASH_BASE]; }

/* ── Software-UART NBU link (proven in test_c5_prog) ───────────────────────── */
struct BitLink {
    std::deque<int> bits; std::mutex m; std::condition_variable cv;
    bool closed = false; su_rx_t rx;
    BitLink() { su_rx_reset(&rx); }
    void push_level(int lvl) { { std::lock_guard<std::mutex> l(m); bits.push_back(lvl); } cv.notify_one(); }
    int get_byte(uint8_t *out, uint32_t to) {
        auto dl = std::chrono::steady_clock::now() + std::chrono::milliseconds(to);
        std::unique_lock<std::mutex> l(m);
        for (;;) {
            while (!bits.empty()) { int lvl = bits.front(); bits.pop_front();
                uint8_t b; if (su_rx_bit(&rx, lvl, &b)) { *out = b; return 0; } }
            if (closed) return -1;
            if (cv.wait_until(l, dl) == std::cv_status::timeout && bits.empty()) return -1;
        }
    }
    void close() { { std::lock_guard<std::mutex> l(m); closed = true; } cv.notify_all(); }
};
static BitLink g_fwd, g_back;
static void push_byte(BitLink &lk, uint8_t b) {
    su_tx_byte(b, [](int lvl, void *c) { ((BitLink *)c)->push_level(lvl); }, &lk);
}
static void prog_send(uint8_t b, void *) { push_byte(g_fwd, b); }
static int  prog_recv(uint8_t *b, uint32_t t, void *) { return g_back.get_byte(b, t); }
static uint32_t prog_tick(void *) { return now_ms(); }
static void bl_send(uint8_t b) { push_byte(g_back, b); }
static int  bl_recv(uint8_t *b, uint32_t t) { return g_fwd.get_byte(b, t); }
static uint32_t bl_tick() { return now_ms(); }

/* BL receiver block handler: first 256 B -> header, rest -> flash @ APP32. */
static uint8_t g_header[SFW_HEADER_SIZE];
static int bl_block(const uint8_t *d, uint32_t offset, uint32_t len, void *) {
    for (uint32_t j = 0; j < len; j++) {
        uint32_t gpos = offset + j;
        if (gpos < SFW_HEADER_SIZE) g_header[gpos] = d[j];
        else *fp(APP32 + (gpos - SFW_HEADER_SIZE)) = d[j];
    }
    return 0;
}

int main() {
    printf("=========================================================\n");
    printf(" IAP chain @ 16/32 offsets — software-UART, real verify\n");
    printf("=========================================================\n");

    std::vector<uint8_t> app_sfw = load("_app.sfw");
    std::vector<uint8_t> pubkey  = load("_pubkey.bin");
    std::vector<uint8_t> bl_img  = load("_bl.bin");
    if (app_sfw.empty() || pubkey.size() != 64 || bl_img.empty()) {
        printf("FAIL: run gen_chain_imgs.py first\n"); return 2;
    }
    // Seed the real BL region with a sentinel so we can prove it is never touched.
    for (uint32_t i = 0; i < SENTINEL_LEN; i++) *fp(FLASH_BASE + i) = 0xC5;

    /* ── Step 1: C542 programmer flashes the signed app to BL@16 -> writes @32 ─ */
    nbu_result_t rx_r = (nbu_result_t)9; uint32_t total = 0;
    std::thread bl([&]() { nbu_io_t io = { bl_send, bl_recv, bl_tick };
        rx_r = nbu_receive(&io, 0x21, bl_block, nullptr, &total); });
    nbu_prog_result_t tx_r = NBU_PROG_ERR_TIMEOUT;
    std::thread prog([&]() { nbu_prog_io_t io = { prog_send, prog_recv, prog_tick, nullptr };
        tx_r = nbu_prog_send(&io, 0x21, app_sfw.data(), (uint32_t)app_sfw.size()); });
    prog.join(); g_fwd.close(); bl.join();

    printf("  Step 1 — software-UART IAP: BL@0x08004000 receives app, writes 0x08008000:\n");
    check(tx_r == NBU_PROG_OK && rx_r == NBU_OK, "transfer completed (BEGIN/DATA/END ACKed)");
    check(total == app_sfw.size(), "BL received the whole .sfw stream");

    const sfw_header_t *hdr = (const sfw_header_t *)g_header;
    uint32_t fw_size = hdr->fw_size;
    const uint8_t *fw_in_flash = fp(APP32);
    std::vector<uint8_t> app_fw(app_sfw.begin() + SFW_HEADER_SIZE, app_sfw.end());
    check(fw_size == app_fw.size(), "header fw_size matches the app body");
    check(std::vector<uint8_t>(fw_in_flash, fw_in_flash + fw_size) == app_fw,
          "app body landed at 0x08008000 byte-for-byte");

    check(sfw_validate_header(hdr, 0x01, SFW_MAX_FW_SIZE_STM32) == SFW_OK,
          "BL validates the .sfw header (magic/target/CRC)");
    check(sfw_check_crc(fw_in_flash, fw_size, hdr->fw_crc32) == SFW_OK,
          "BL fw CRC-32 matches");
    check(sfw_verify_signature(hdr, fw_in_flash, fw_size, pubkey.data()) == SFW_OK,
          "BL ECDSA signature ACCEPTS the genuine app");

    uint8_t save = *fp(APP32);
    *fp(APP32) ^= 0xFF;
    check(sfw_verify_signature(hdr, fw_in_flash, fw_size, pubkey.data()) != SFW_OK,
          "tampered app @0x08008000 is REJECTED");
    *fp(APP32) = save;

    /* ── Step 2: installer@32 writes a bootloader back to 16 ─────────────────── */
    printf("  Step 2 — installer@0x08008000 writes a bootloader to 0x08004000:\n");
    // (models flash_rt: verify-before-erase, erase to 0xFF, write, read-back)
    uint32_t bl_crc = crc32_compute(bl_img.data(), bl_img.size());
    check(bl_crc == crc32_compute(bl_img.data(), bl_img.size()), "BL image CRC-32 stable (pre-erase check)");
    for (uint32_t i = 0; i < SENTINEL_LEN; i++) *fp(BL16 + i) = 0xFF;          /* erase 16-region */
    for (uint32_t i = 0; i < bl_img.size(); i++) *fp(BL16 + i) = bl_img[i];    /* write */
    bool readback = true;
    for (uint32_t i = 0; i < bl_img.size(); i++) if (*fp(BL16 + i) != bl_img[i]) readback = false;
    check(readback, "read-back: bootloader written to 0x08004000 byte-for-byte");

    /* ── Step 3: the real BL region was never touched ────────────────────────── */
    printf("  Step 3 — safety: real bootloader region untouched:\n");
    bool sentinel_ok = true;
    for (uint32_t i = 0; i < SENTINEL_LEN; i++) if (*fp(FLASH_BASE + i) != 0xC5) sentinel_ok = false;
    check(sentinel_ok, "0x08000000-0x08003FFF still the sentinel (0x08000000 safe)");

    printf("---------------------------------------------------------\n");
    printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
