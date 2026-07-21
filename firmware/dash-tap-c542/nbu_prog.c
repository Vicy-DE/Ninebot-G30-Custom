/**
 * @file nbu_prog.c
 * @brief On-chip NBU programmer/sender (see nbu_prog.h).
 */
#include "nbu_prog.h"

#define MAX_RETRIES   8
#define ACK_TIMEOUT   1000u      /* ms to wait for one ACK */

/* Build + send one 5A A5 frame (LEN=payload, CK=~sum, little-endian). */
static void send_frame(const nbu_prog_io_t *io, uint8_t cmd, uint8_t arg,
                       const uint8_t *pl, uint8_t pl_len, uint8_t addr)
{
    uint8_t hdr[5] = { pl_len, NBU_ADDR_HOST, addr, cmd, arg };
    uint16_t s = 0;
    for (int i = 0; i < 5; i++) s = (uint16_t)(s + hdr[i]);
    for (uint8_t i = 0; i < pl_len; i++) s = (uint16_t)(s + pl[i]);
    uint16_t ck = (uint16_t)(~s);

    io->send_byte(NBU_HDR1, io->ctx);
    io->send_byte(NBU_HDR2, io->ctx);
    for (int i = 0; i < 5; i++) io->send_byte(hdr[i], io->ctx);
    for (uint8_t i = 0; i < pl_len; i++) io->send_byte(pl[i], io->ctx);
    io->send_byte((uint8_t)(ck & 0xFF), io->ctx);
    io->send_byte((uint8_t)(ck >> 8), io->ctx);
}

/*
 * Wait for one ACK frame addressed to the host. On success fills *status (ARG),
 * *acked_cmd and *info (payload[1..2]). Returns 0 ok, -1 on timeout.
 */
static int wait_ack(const nbu_prog_io_t *io, uint8_t *status,
                    uint8_t *acked_cmd, uint16_t *info)
{
    uint8_t buf[262];
    uint8_t st = 0; uint16_t idx = 0, expected = 0;
    uint32_t start = io->tick_ms(io->ctx);

    for (;;) {
        if ((io->tick_ms(io->ctx) - start) > ACK_TIMEOUT) return -1;
        uint8_t b;
        if (io->recv_byte(&b, ACK_TIMEOUT, io->ctx) != 0) {
            if ((io->tick_ms(io->ctx) - start) > ACK_TIMEOUT) return -1;
            continue;
        }
        if (st == 0) {
            if (b == NBU_HDR1) st = 1;
            continue;
        }
        if (st == 1) {
            if (b == NBU_HDR2) { st = 2; idx = 0; }
            else if (b != NBU_HDR1) st = 0;
            continue;
        }
        buf[idx] = b;
        if (idx == 0) {
            expected = (uint16_t)(b + 7);
            if (expected > sizeof(buf)) { st = 0; continue; }
        }
        if (++idx < expected) continue;

        st = 0;
        uint8_t len = buf[0];
        uint16_t calc = nbu_checksum(buf, (uint16_t)(len + 5));
        uint16_t recv = (uint16_t)(buf[len + 5] | (buf[len + 6] << 8));
        if (calc != recv) continue;                     /* bad ACK -> keep waiting */
        if (buf[2] != NBU_ADDR_HOST || buf[3] != NBU_CMD_ACK) continue;
        *status    = buf[4];
        *acked_cmd = (len >= 1) ? buf[5] : 0;
        *info      = (len >= 3) ? (uint16_t)(buf[6] | (buf[7] << 8)) : 0;
        return 0;
    }
}

/* Send one command and wait for a matching, OK ACK, with retransmit. */
static nbu_prog_result_t cmd_acked(const nbu_prog_io_t *io, uint8_t addr,
                                   uint8_t cmd, uint8_t arg,
                                   const uint8_t *pl, uint8_t pl_len,
                                   uint16_t *out_info)
{
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        send_frame(io, cmd, arg, pl, pl_len, addr);
        uint8_t status, acked; uint16_t info;
        if (wait_ack(io, &status, &acked, &info) != 0) continue;   /* timeout */
        if (acked != cmd) continue;                                /* stale */
        if (status == 0x00) { if (out_info) *out_info = info; return NBU_PROG_OK; }
        /* status 0x02 = out-of-order NACK: info is the seq the receiver expects.
         * Caller (DATA loop) resyncs on the returned info. */
        if (out_info) *out_info = info;
        return NBU_PROG_ERR_NAK;
    }
    return NBU_PROG_ERR_TIMEOUT;
}

nbu_prog_result_t nbu_prog_send(const nbu_prog_io_t *io, uint8_t addr,
                                const uint8_t *image, uint32_t len)
{
    uint8_t pl[2 + NBU_PROG_CHUNK];
    uint16_t info = 0;

    pl[0] = (uint8_t)(len & 0xFF);  pl[1] = (uint8_t)((len >> 8) & 0xFF);
    pl[2] = (uint8_t)((len >> 16) & 0xFF); pl[3] = (uint8_t)((len >> 24) & 0xFF);
    nbu_prog_result_t r = cmd_acked(io, addr, NBU_CMD_BEGIN, 0, pl, 4, &info);
    if (r != NBU_PROG_OK) return r;

    uint16_t seq = 0;
    uint32_t off = 0;
    while (off < len) {
        uint32_t n = len - off; if (n > NBU_PROG_CHUNK) n = NBU_PROG_CHUNK;
        pl[0] = (uint8_t)(seq & 0xFF); pl[1] = (uint8_t)(seq >> 8);
        for (uint32_t i = 0; i < n; i++) pl[2 + i] = image[off + i];
        r = cmd_acked(io, addr, NBU_CMD_DATA, 0, pl, (uint8_t)(2 + n), &info);
        if (r == NBU_PROG_ERR_NAK) {                  /* receiver wants seq=info */
            if (info != seq) { seq = info; off = (uint32_t)seq * NBU_PROG_CHUNK; continue; }
            return NBU_PROG_ERR_NAK;
        }
        if (r != NBU_PROG_OK) return r;
        off += n; seq++;
    }

    return cmd_acked(io, addr, NBU_CMD_END, 0, 0, 0, &info);
}
