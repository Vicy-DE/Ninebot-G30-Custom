/**
 * @file nbu_prog.h
 * @brief On-chip NBU programmer (sender) — the C port of tools/flasher/nbu_send.py.
 *
 * The NUCLEO-C542RC runs this to flash a signed .sfw image to a board's bootloader
 * over the (software) UART, using the firmware-verified Ninebot framing and the NBU
 * BEGIN/DATA/END handshake with per-block ACK + retransmit. Reuses nbu.h constants /
 * checksum so it is wire-compatible with the bootloader's nbu.c receiver.
 */
#ifndef NBU_PROG_H
#define NBU_PROG_H

#include <stdint.h>
#include "nbu.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Byte I/O the programmer drives (over the software UART on the C542). */
typedef struct {
    void     (*send_byte)(uint8_t b, void *ctx);
    int      (*recv_byte)(uint8_t *b, uint32_t timeout_ms, void *ctx); /* 0 ok, -1 t/o */
    uint32_t (*tick_ms)(void *ctx);
    void      *ctx;
} nbu_prog_io_t;

typedef enum {
    NBU_PROG_OK          = 0,
    NBU_PROG_ERR_NAK     = -1,   /* receiver kept NACKing past retries */
    NBU_PROG_ERR_TIMEOUT = -2,   /* no ACK */
} nbu_prog_result_t;

#define NBU_PROG_CHUNK   128     /* data bytes per DATA frame (<= NBU_DATA_MAX) */

/** Flash `image` (len bytes) to bus address `addr` via NBU. */
nbu_prog_result_t nbu_prog_send(const nbu_prog_io_t *io, uint8_t addr,
                                const uint8_t *image, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* NBU_PROG_H */
