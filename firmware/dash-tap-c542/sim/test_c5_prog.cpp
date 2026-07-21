/**
 * @file test_c5_prog.cpp
 * @brief Closed-loop proof of the C542 software-UART NBU/IAP programmer.
 *
 *   nbu_prog_send (C542 programmer)  ──bits──►  soft_uart  ──►  nbu_receive (BL)
 *           ▲                                                          │
 *           └──────────────  soft_uart  ◄──bits──  ACKs  ◄────────────┘
 *
 * Every byte in BOTH directions is encoded by su_tx_byte into 10 line levels and
 * decoded by su_rx_bit — so this exercises the real bit-bang UART, the real NBU
 * sender (nbu_prog.c) and the real bootloader receiver (nbu.c) together. If the
 * receiver reconstructs the image byte-for-byte, the software-UART IAP path works.
 */
extern "C" {
#include "soft_uart.h"
#include "nbu_prog.h"
#include "nbu.h"
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

/* ── Part 1: software-UART loopback fidelity (no threads) ──────────────────── */
static std::vector<int> g_bits;
static void emit(int lvl, void *) { g_bits.push_back(lvl); }

static void loopback_tests() {
    printf("  Software-UART loopback:\n");
    std::vector<uint8_t> msg;
    for (int i = 0; i < 256; i++) msg.push_back((uint8_t)(i * 73 + 5));
    // also push a real NBU frame's bytes (5A A5 ...) to cover framing bytes
    uint8_t fr[] = {0x5A,0xA5,0x04,0x3F,0x21,0x07,0x00,0xC8,0,0,0};
    for (uint8_t b : fr) msg.push_back(b);

    g_bits.clear();
    for (uint8_t b : msg) su_tx_byte(b, emit, nullptr);

    su_rx_t rx; su_rx_reset(&rx);
    std::vector<uint8_t> out;
    for (int lvl : g_bits) { uint8_t b; if (su_rx_bit(&rx, lvl, &b)) out.push_back(b); }

    check(out == msg, "every byte (incl. 5A A5 framing) survives TX->bits->RX");
    check(g_bits.size() == msg.size() * 10, "10 line levels per byte (8N1)");
}

/* ── A bit-level wire with a decoding reader (one UART direction) ──────────── */
struct BitLink {
    std::deque<int> bits;
    std::mutex m;
    std::condition_variable cv;
    bool closed = false;
    su_rx_t rx;
    BitLink() { su_rx_reset(&rx); }
    void push_level(int lvl) {
        { std::lock_guard<std::mutex> l(m); bits.push_back(lvl); }
        cv.notify_one();
    }
    int get_byte(uint8_t *out, uint32_t timeout_ms) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        std::unique_lock<std::mutex> l(m);
        for (;;) {
            while (!bits.empty()) {
                int lvl = bits.front(); bits.pop_front();
                uint8_t b;
                if (su_rx_bit(&rx, lvl, &b)) { *out = b; return 0; }
            }
            if (closed) return -1;
            if (cv.wait_until(l, deadline) == std::cv_status::timeout && bits.empty())
                return -1;
        }
    }
    void close() { { std::lock_guard<std::mutex> l(m); closed = true; } cv.notify_all(); }
};

static BitLink g_fwd;   // programmer -> bootloader
static BitLink g_back;  // bootloader  -> programmer
static void push_byte(BitLink &lk, uint8_t b) {
    su_tx_byte(b, [](int lvl, void *ctx) { ((BitLink *)ctx)->push_level(lvl); }, &lk);
}

/* Programmer (C542) side I/O. */
static void prog_send(uint8_t b, void *) { push_byte(g_fwd, b); }
static int  prog_recv(uint8_t *b, uint32_t t, void *) { return g_back.get_byte(b, t); }
static uint32_t prog_tick(void *) { return now_ms(); }

/* Bootloader (nbu.c) side I/O. */
static void bl_send(uint8_t b) { push_byte(g_back, b); }
static int  bl_recv(uint8_t *b, uint32_t t) { return g_fwd.get_byte(b, t); }
static uint32_t bl_tick() { return now_ms(); }

static std::vector<uint8_t> g_written;
static int bl_write(const uint8_t *d, uint32_t, uint32_t len, void *) {
    for (uint32_t i = 0; i < len; i++) g_written.push_back(d[i]);
    return 0;
}

int main() {
    printf("=========================================================\n");
    printf(" C542 software-UART NBU/IAP programmer — closed loop\n");
    printf("=========================================================\n");
    loopback_tests();

    // 777-byte image (not a chunk multiple) to flash via the software UART.
    std::vector<uint8_t> img(777);
    for (size_t i = 0; i < img.size(); i++) img[i] = (uint8_t)(i * 37 + 11);

    const uint8_t ADDR = 0x21;   // BLE/dashboard bus address
    nbu_result_t rx_result = (nbu_result_t)0x5;
    uint32_t total = 0;

    // Bootloader receiver thread (the relocated BL would run exactly this nbu_receive).
    std::thread bl([&]() {
        nbu_io_t io = { bl_send, bl_recv, bl_tick };
        rx_result = nbu_receive(&io, ADDR, bl_write, nullptr, &total);
    });

    // Programmer thread (the C542 firmware runs exactly this nbu_prog_send).
    nbu_prog_result_t tx_result = NBU_PROG_ERR_TIMEOUT;
    std::thread prog([&]() {
        nbu_prog_io_t io = { prog_send, prog_recv, prog_tick, nullptr };
        tx_result = nbu_prog_send(&io, ADDR, img.data(), (uint32_t)img.size());
    });

    prog.join();
    g_fwd.close();           // let the receiver's recv unblock if needed
    bl.join();

    printf("  Programmer -> bootloader over software UART:\n");
    check(tx_result == NBU_PROG_OK, "programmer reports OK (BEGIN/DATA/END all ACKed)");
    check(rx_result == NBU_OK, "bootloader receiver ends on END");
    check(total == img.size(), "received byte count = 777");
    check(g_written == img, "firmware reconstructed byte-for-byte through the soft UART");

    printf("---------------------------------------------------------\n");
    printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
