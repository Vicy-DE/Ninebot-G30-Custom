/**
 * @file esc_faker_main.c
 * @brief ESC emulator on the C542: answers the dashboard's polls as if it were the
 *        ESC (SRC 0x20), to clear the "ESC not responding" fault — the dashboard
 *        likely refuses to enter IAP while comms are faulted. Then (optionally) it
 *        injects the IAP-enter command into the now-healthy bus.
 *
 * Real-time half-duplex slave on PA4 @115200: receive a full Ninebot frame (sync on
 * the inter-frame idle gap, stop-bit checked per byte), and if it is addressed to the
 * ESC (DST 0x20) reply as the ESC within the turnaround gap. Counters + last frames
 * are logged to SRAM (0x20000000) for SWD readback (decode with the host).
 *
 * Build: make MAIN=esc_faker_main.c flash   (then read 0x20000000 over SWD)
 *   -DSEND_IAP=1 also injects an IAP-enter frame once comms look healthy.
 */
#include <stdint.h>
#include "stm32c5xx.h"
#include "soft_uart.h"

#ifndef SEND_IAP
#define SEND_IAP 0
#endif

typedef struct {
    uint32_t magic;        /* 0xE5C00001 written last */
    uint32_t sysclk;
    uint32_t rx_frames;    /* checksum-valid frames received */
    uint32_t rx_to_esc;    /* frames addressed to ESC (DST 0x20) */
    uint32_t tx_resp;      /* ESC responses sent */
    uint32_t bad_ck;       /* checksum failures */
    uint32_t iap_sent;     /* IAP-enter frames injected */
    uint32_t rsvd;
    uint8_t  last_rx[24];  /* last valid frame received */
    uint8_t  last_resp[24];/* last response we sent */
    /* SWD-driven injection mailbox: host writes inject[]+inject_len; we send it in a
     * gap (while still emulating the ESC) and capture the dashboard's reply in resp[]. */
    uint32_t inject_len;
    uint32_t resp_len;
    uint8_t  inject[28];
    uint8_t  resp[40];
} log_t;
volatile log_t L __attribute__((section(".result"), used));

#define DEMCR    (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYC  (*(volatile uint32_t *)0xE0001004u)
static uint32_t bitc;
static inline uint32_t cyc(void) { return DWT_CYC; }
static inline void wait(uint32_t n) { uint32_t s = cyc(); while ((cyc() - s) < n) {} }
static inline int  rdline(void) { return (GPIOA->IDR >> 4) & 1u; }
static inline void drive_low(void) { GPIOA->BSRR = (1u << (4 + 16)); }
static inline void release(void)   { GPIOA->BSRR = (1u << 4); }
static inline void set_out(void) { GPIOA->MODER = (GPIOA->MODER & ~(3u << 8)) | (1u << 8); }
static inline void set_in(void)  { GPIOA->MODER &= ~(3u << 8); }

static void emit(int level, void *ctx) { (void)ctx; if (level) release(); else drive_low(); wait(bitc); }
static void tx_frame(const uint8_t *f, int n) {
    set_out();
    for (int i = 0; i < n; i++) su_tx_byte((uint8_t)f[i], emit, 0);
    release();
    set_in();
}

/* Receive a frame by OVERSAMPLING the waveform at 4x in real time, then decoding it
 * on-chip (reliable, unlike 1x sampling which drifts). Returns decoded byte count. */
#define OS 4
static uint8_t g_samp[700];
static int rx_frame(uint8_t *buf, int max)
{
    /* 1) sync on the inter-frame idle gap (line high ~1.5 byte-times) */
    uint32_t hs = cyc(), guard = cyc();
    while ((cyc() - hs) < bitc * 15u) {
        if (!rdline()) hs = cyc();
        if ((cyc() - guard) > SystemCoreClock / 5u) return -1;   /* 200 ms: no gap */
    }
    /* 2) wait for the frame's first falling edge (start bit) */
    uint32_t t0 = cyc();
    while (rdline()) { if ((cyc() - t0) > bitc * 4000u) return -1; }
    /* 3) oversample-capture until a long idle (end of frame) or buffer full */
    uint32_t iv = bitc / OS, base = cyc(), idle = 0, ns;
    for (ns = 0; ns < sizeof(g_samp); ns++) {
        uint32_t target = base + ns * iv;
        while ((int32_t)(cyc() - target) < 0) {}
        int lv = rdline();
        g_samp[ns] = (uint8_t)lv;
        if (lv) { if (++idle > (uint32_t)(OS * 12)) { ns++; break; } } else idle = 0;
    }
    /* 4) decode UART (8N1, LSB-first); re-sync to each byte's start bit */
    int n = 0; uint32_t pos = 0;
    while (pos + (uint32_t)(10 * OS) <= ns && n < max) {
        while (pos < ns && g_samp[pos]) pos++;        /* skip idle/stop high -> start (low) */
        if (pos + (uint32_t)(10 * OS) > ns) break;
        uint8_t b = 0;
        for (int k = 0; k < 8; k++)
            if (g_samp[pos + OS + k * OS + OS / 2]) b |= (uint8_t)(1u << k);
        buf[n++] = b;
        pos += (uint32_t)(9 * OS);                    /* past start+8 data -> stop bit */
    }
    return n;
}

static uint16_t cksum(const uint8_t *body, int n)
{
    uint16_t s = 0; for (int i = 0; i < n; i++) s = (uint16_t)(s + body[i]);
    return (uint16_t)~s;
}

int main(void)
{
    SystemCoreClockUpdate();
    bitc = SystemCoreClock / 115200u;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    release();
    GPIOA->OTYPER  &= ~(1u << 4);                     /* push-pull when driving */
    GPIOA->OSPEEDR |= (3u << 8);
    GPIOA->PUPDR    = (GPIOA->PUPDR & ~(3u << 8)) | (1u << 8);  /* pull-up */
    GPIOA->MODER   &= ~(3u << 8);                     /* INPUT default (RX) */
    DEMCR |= (1u << 24); DWT_CYC = 0; DWT_CTRL |= 1u;
    L.sysclk = SystemCoreClock;
    L.rx_frames = L.rx_to_esc = L.tx_resp = L.bad_ck = L.iap_sent = 0;
    L.magic = 0;

    uint8_t buf[32];
    for (;;) {
        int n = rx_frame(buf, sizeof buf);
        /* DEBUG: log every non-empty rx_frame (valid or not) so we can see what the
         * real-time sampler decodes. rsvd = attempts, last_rx = last raw bytes. */
        if (n > 0) {
            L.rsvd++;
            for (int i = 0; i < 24; i++) L.last_rx[i] = (i < n) ? buf[i] : 0;
            L.magic = 0xE5C00001u;
        }
        if (n < 9 || buf[0] != 0x5A || buf[1] != 0xA5) continue;
        uint8_t len = buf[2];
        if (n < 2 + 5 + len + 2) continue;            /* incomplete */
        uint16_t calc = cksum(&buf[2], 5 + len);
        uint16_t got  = (uint16_t)(buf[2 + 5 + len] | (buf[3 + 5 + len] << 8));
        if (calc != got) { L.bad_ck++; continue; }

        L.rx_frames++;
        for (int i = 0; i < 24 && i < n; i++) L.last_rx[i] = buf[i];

        uint8_t src = buf[3], dst = buf[4], cmd = buf[5], arg = buf[6];
        (void)src; (void)arg;
        /* rsvd = bitmap of frame codes seen from the dashboard (bit0=0x65, bit1=0x64,
         * bit2=other) so we learn what it actually polls. */
        if (cmd == 0x65) L.rsvd |= 1u; else if (cmd == 0x64) L.rsvd |= 2u; else L.rsvd |= 4u;
        /* Any frame addressed back to App/PC = a reply to an injected read/command. */
        if (dst == 0x3E || dst == 0x3F) {
            L.resp_len = (uint32_t)n;
            for (int i = 0; i < (int)sizeof(L.resp); i++) L.resp[i] = (i < n) ? buf[i] : 0;
        }
        if (dst != 0x20) { L.magic = 0xE5C00001u; continue; }   /* not for the ESC */

        L.rx_to_esc++;
        /* ESC -> dashboard 0x64 display frame (format from vesc-lisp/g30_dash.lisp):
         * 5A A5 06 20 21 64 00 | mode batt light beep speed error | CRClo CRChi.
         * error(byte12)=0 => no fault; valid mode/battery => healthy ESC, clears the
         * dashboard's comm-fault so it should accept IAP. */
        (void)cmd;
        uint8_t r[24];
        uint8_t pll = 6;
        r[0] = 0x5A; r[1] = 0xA5; r[2] = pll; r[3] = 0x20; r[4] = 0x21; r[5] = 0x64; r[6] = 0x00;
        r[7]  = 0x02;    /* mode = eco (valid) */
        r[8]  = 0x50;    /* battery = 80 %    */
        r[9]  = 0x00;    /* light off         */
        r[10] = 0x00;    /* beep off          */
        r[11] = 0x00;    /* speed 0 (idle)    */
        r[12] = 0x00;    /* error = 0 (NO FAULT) */
        uint16_t ck = cksum(&r[2], 5 + pll);
        r[7 + pll] = (uint8_t)(ck & 0xFF); r[8 + pll] = (uint8_t)(ck >> 8);
        int rn = 9 + pll;

        /* respond promptly in the turnaround gap */
        tx_frame(r, rn);
        L.tx_resp++;
        for (int i = 0; i < 24 && i < rn; i++) L.last_resp[i] = r[i];

        /* SWD-injected frame: send it in this gap (comms stay healthy). The reply (if
         * any) is captured by the DST==0x3E/0x3F handler above into resp[]. */
        if (L.inject_len > 0 && L.inject_len <= sizeof(L.inject)) {
            uint8_t f[28];
            uint32_t m = L.inject_len; if (m > sizeof f) m = sizeof f;
            for (uint32_t i = 0; i < m; i++) f[i] = L.inject[i];
            L.inject_len = 0;                       /* consume */
            L.iap_sent++;
            L.resp_len = 0;                         /* clear; await a fresh reply */
            tx_frame(f, (int)m);
        }
        L.magic = 0xE5C00001u;
    }
}
