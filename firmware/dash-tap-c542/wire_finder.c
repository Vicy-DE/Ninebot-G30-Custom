/**
 * @file wire_finder.c
 * @brief Passive two-line Ninebot sniffer/classifier (see wire_finder.h).
 */
#include "wire_finder.h"

uint16_t wf_checksum(const uint8_t *d, uint16_t len)
{
    uint16_t s = 0;
    for (uint16_t i = 0; i < len; i++) s = (uint16_t)(s + d[i]);
    return (uint16_t)(~s);
}

/* Map a Ninebot address to a stable bit so addr_mask records who was seen.
 * 0x20 ESC, 0x21 BLE/nRF, 0x22 BMS, 0x3E App, 0x3F PC -> bits 0..4; else bit 15. */
static uint16_t addr_bit(uint8_t a)
{
    switch (a) {
    case 0x20: return 1u << 0;
    case 0x21: return 1u << 1;
    case 0x22: return 1u << 2;
    case 0x3E: return 1u << 3;
    case 0x3F: return 1u << 4;
    default:   return 1u << 15;
    }
}

void wf_reset(wire_finder_t *wf)
{
    for (int i = 0; i < WF_LINES; i++) {
        wf_lstate_t *L = &wf->line[i];
        L->state = 0; L->idx = 0; L->expected = 0;
        L->stat.kind = WF_IDLE;
        L->stat.bytes = L->stat.frames_ok = L->stat.frames_bad = 0;
        L->stat.addr_mask = 0;
        L->stat.last_src = L->stat.last_dst = 0;
    }
}

void wf_feed(wire_finder_t *wf, int line, uint8_t b)
{
    if (line < 0 || line >= WF_LINES) return;
    wf_lstate_t *L = &wf->line[line];
    L->stat.bytes++;

    if (L->state == 0) { if (b == WF_HDR1) L->state = 1; return; }
    if (L->state == 1) {
        if (b == WF_HDR2) { L->state = 2; L->idx = 0; }
        else if (b != WF_HDR1) L->state = 0;          /* re-sync */
        return;
    }

    /* state 2: body = [LEN, SRC, DST, CMD, ARG, payload..., CK_lo, CK_hi] */
    L->buf[L->idx] = b;
    if (L->idx == 0) {
        L->expected = (uint16_t)(b + 7);              /* body = LEN + 7 */
        if (L->expected > sizeof(L->buf)) { L->state = 0; return; }
    }
    L->idx++;
    if (L->idx < L->expected) return;

    L->state = 0;                                     /* frame complete */
    uint8_t len = L->buf[0];
    uint16_t calc = wf_checksum(L->buf, (uint16_t)(len + 5));
    uint16_t recv = (uint16_t)(L->buf[len + 5] | (L->buf[len + 6] << 8));
    if (calc != recv) { L->stat.frames_bad++; return; }

    L->stat.frames_ok++;
    L->stat.last_src = L->buf[1];
    L->stat.last_dst = L->buf[2];
    L->stat.addr_mask |= addr_bit(L->buf[1]);   /* SRC = the transmitter on this wire */
}

void wf_classify(const wire_finder_t *wf, wf_line_t out[WF_LINES])
{
    for (int i = 0; i < WF_LINES; i++) {
        out[i] = wf->line[i].stat;
        if (out[i].frames_ok > 0)      out[i].kind = WF_NINEBOT;
        else if (out[i].bytes > 0)     out[i].kind = WF_NOISE;
        else                           out[i].kind = WF_IDLE;
    }
}

const char *wf_role(const wf_line_t *st)
{
    if (st->kind == WF_IDLE)  return "idle (no traffic)";
    if (st->kind == WF_NOISE) return "active but no Ninebot frames (noise / wrong baud)";
    /* NINEBOT: the transmitter on this wire is the frame SRC. Name it by address;
     * the operator maps that to "BT" (nRF) vs "dashboard" (STM32). */
    switch (st->last_src) {
    case 0x21: return "transmits SRC 0x21 -> BLE/nRF (BT) side";
    case 0x3E: return "transmits SRC 0x3E -> App/phone side";
    case 0x3F: return "transmits SRC 0x3F -> PC side";
    case 0x20: return "transmits SRC 0x20 -> ESC/VESC side";
    case 0x22: return "transmits SRC 0x22 -> BMS side";
    default:   return "Ninebot frames (unrecognized SRC address)";
    }
}
