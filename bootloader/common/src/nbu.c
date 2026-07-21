/**
 * @file nbu.c
 * @brief NBU framed half-duplex firmware-update receiver (see nbu.h).
 */
#include "nbu.h"

uint16_t nbu_checksum(const uint8_t *d, uint16_t len)
{
    uint16_t s = 0;
    for (uint16_t i = 0; i < len; i++) s = (uint16_t)(s + d[i]);
    return (uint16_t)(~s);
}

/* Send one 5A A5 frame (LEN = payload count, ~sum checksum, little-endian CK). */
static void send_frame(const nbu_io_t *io, uint8_t src, uint8_t dst,
                       uint8_t cmd, uint8_t arg,
                       const uint8_t *pl, uint8_t pl_len)
{
    uint8_t fields[5] = { pl_len, src, dst, cmd, arg };
    uint16_t s = 0;
    for (int i = 0; i < 5; i++) s = (uint16_t)(s + fields[i]);
    for (uint8_t i = 0; i < pl_len; i++) s = (uint16_t)(s + pl[i]);
    uint16_t ck = (uint16_t)(~s);

    io->uart_send_byte(NBU_HDR1);
    io->uart_send_byte(NBU_HDR2);
    for (int i = 0; i < 5; i++) io->uart_send_byte(fields[i]);
    for (uint8_t i = 0; i < pl_len; i++) io->uart_send_byte(pl[i]);
    io->uart_send_byte((uint8_t)(ck & 0xFF));
    io->uart_send_byte((uint8_t)(ck >> 8));
}

/* Reply ACK/NACK: ARG = status (0 = OK); payload = [acked_cmd, info_lo, info_hi]. */
static void reply(const nbu_io_t *io, uint8_t my, uint8_t acked_cmd,
                  uint8_t status, uint16_t info)
{
    uint8_t pl[3] = { acked_cmd, (uint8_t)(info & 0xFF), (uint8_t)(info >> 8) };
    send_frame(io, my, NBU_ADDR_HOST, NBU_CMD_ACK, status, pl, 3);
}

nbu_result_t nbu_receive(const nbu_io_t *io, uint8_t my,
                         nbu_write_cb cb, void *ctx, uint32_t *total)
{
    uint8_t  buf[256];
    uint8_t  state = 0;          /* 0 = idle, 1 = saw 5A, 2 = collecting body */
    uint16_t idx = 0, expected = 0;   /* wide: LEN+7 can be up to 262 (don't wrap u8) */
    uint16_t seq_expected = 0;
    uint32_t offset = 0;
    uint32_t last_rx = io->get_tick_ms();

    for (;;) {
        uint8_t b;
        if (io->uart_recv_byte(&b, 1000) != 0) {            /* 1 s read timeout */
            if ((io->get_tick_ms() - last_rx) > 20000u) {   /* 20 s idle → give up */
                if (total) *total = offset;
                return NBU_ERR_TIMEOUT;
            }
            continue;
        }
        last_rx = io->get_tick_ms();

        if (state == 0) { if (b == NBU_HDR1) state = 1; continue; }
        if (state == 1) {
            if (b == NBU_HDR2) { state = 2; idx = 0; }
            else if (b != NBU_HDR1) state = 0;              /* re-sync */
            continue;
        }

        /* state 2: body = [LEN, SRC, DST, CMD, ARG, payload..., CK_lo, CK_hi] */
        buf[idx] = b;
        if (idx == 0) {
            expected = (uint16_t)(b + 7);                   /* body = LEN + 7 */
            if (expected > sizeof(buf)) { state = 0; continue; } /* LEN>249: reject */
        }
        idx++;
        if (idx < expected) continue;

        state = 0;                                          /* frame complete */
        uint8_t len = buf[0];
        uint16_t calc = nbu_checksum(buf, (uint16_t)(len + 5));
        uint16_t recv = (uint16_t)(buf[len + 5] | (buf[len + 6] << 8));
        if (calc != recv) continue;                         /* drop → host resends */

        uint8_t dst = buf[2], cmd = buf[3];
        const uint8_t *pl = &buf[5];
        uint8_t pl_len = len;
        if (dst != my) continue;                            /* not addressed to us */

        switch (cmd) {
        case NBU_CMD_BEGIN:
            offset = 0; seq_expected = 0;
            reply(io, my, NBU_CMD_BEGIN, 0x00, 0);
            break;

        case NBU_CMD_DATA: {
            if (pl_len < 2) { reply(io, my, NBU_CMD_DATA, 0x01, seq_expected); break; }
            uint16_t seq = (uint16_t)(pl[0] | (pl[1] << 8));
            const uint8_t *data = pl + 2;
            uint8_t dlen = (uint8_t)(pl_len - 2);
            if (seq == seq_expected) {
                if (cb(data, offset, dlen, ctx) != 0) {
                    reply(io, my, NBU_CMD_DATA, 0x05, seq);
                    if (total) *total = offset;
                    return NBU_ERR_WRITE;
                }
                offset += dlen; seq_expected++;
                reply(io, my, NBU_CMD_DATA, 0x00, seq);     /* ACK */
            } else if ((uint16_t)(seq + 1) == seq_expected) {
                reply(io, my, NBU_CMD_DATA, 0x00, seq);     /* duplicate → re-ACK */
            } else {
                reply(io, my, NBU_CMD_DATA, 0x02, seq_expected); /* NACK + expected */
            }
            break;
        }

        case NBU_CMD_END:
            reply(io, my, NBU_CMD_END, 0x00, (uint16_t)offset);
            if (total) *total = offset;
            return NBU_OK;

        case NBU_CMD_RESET:
            reply(io, my, NBU_CMD_RESET, 0x00, 0);
            if (total) *total = offset;
            return NBU_GOT_RESET;

        default:
            break;
        }
    }
}
