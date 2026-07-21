/**
 * @file nbu.h
 * @brief NBU — Ninebot-framed firmware update over **half-duplex single-wire UART**.
 *
 * Replaces XMODEM. The dashboard cable's data line is a single half-duplex wire
 * (the Ninebot bus), where XMODEM's byte-stream + echo/turnaround is fragile.
 * NBU is **framed request→reply turn-taking** — the host sends one command frame
 * and waits for the bootloader's single reply before the next — which is exactly
 * what a one-wire half-duplex bus supports, and it reuses the bus's own framing.
 *
 * Wire format = the firmware-verified Ninebot frame (see
 * `firmware/decompiled/common/include/ninebot_protocol_verified.hpp`):
 *   `5A A5 | LEN | SRC | DST | CMD | ARG | payload[LEN] | CK_lo CK_hi`
 *   LEN = payload byte count;  CK = (sum(LEN..payload)) ^ 0xFFFF, little-endian.
 *
 * Commands (host 0x3F → bootloader `my_addr`), aligned with the stock IAP opcodes:
 *   0x07 BEGIN  payload: u32 total .sfw size (LE)   → reply ACK
 *   0x08 DATA   payload: u16 seq (LE) + data bytes  → reply ACK(seq) / NACK(expected)
 *   0x09 END    payload: (optional) u32 crc32       → reply ACK; receive done
 *   0x0A RESET                                       → reply ACK; reset into app
 * Reply (bootloader → host): `CMD 0x06 (ACK)`, `ARG = status` (0 = OK), payload =
 *   [acked_cmd, info_lo, info_hi] (info = seq or running offset).
 */
#ifndef NBU_H
#define NBU_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBU_HDR1        0x5A
#define NBU_HDR2        0xA5
#define NBU_CMD_BEGIN   0x07
#define NBU_CMD_DATA    0x08
#define NBU_CMD_END     0x09
#define NBU_CMD_RESET   0x0A
#define NBU_CMD_ACK     0x06
#define NBU_ADDR_HOST   0x3F
#define NBU_DATA_MAX    192     /**< max data bytes per DATA frame */

typedef enum {
    NBU_OK          = 0,    /**< END received; *total = bytes written */
    NBU_ERR_TIMEOUT = -1,   /**< host went silent */
    NBU_ERR_WRITE   = -5,   /**< write_cb aborted (flash error) */
    NBU_GOT_RESET   = 1,    /**< host sent RESET (caller should reboot) */
} nbu_result_t;

/** Half-duplex UART I/O (plain byte send/receive + millisecond tick). */
typedef struct {
    void     (*uart_send_byte)(uint8_t b);
    int      (*uart_recv_byte)(uint8_t *b, uint32_t timeout_ms);  /* 0 ok, -1 timeout */
    uint32_t (*get_tick_ms)(void);
} nbu_io_t;

/** Called for each accepted in-order data chunk. Return 0 ok, -1 to abort. */
typedef int (*nbu_write_cb)(const uint8_t *data, uint32_t offset,
                            uint32_t length, void *ctx);

/**
 * Run a framed half-duplex update session until END (or RESET / timeout / error).
 * @param io        UART callbacks
 * @param my_addr   this board's bus address (e.g. 0x21 = BLE dashboard)
 * @param write_cb  receives accepted data chunks in order
 * @param ctx       opaque, passed to write_cb
 * @param total     out: bytes received
 */
nbu_result_t nbu_receive(const nbu_io_t *io, uint8_t my_addr,
                         nbu_write_cb write_cb, void *ctx, uint32_t *total);

/** Ninebot checksum: (sum(data[0..len)) ^ 0xFFFF). Exposed for tests/senders. */
uint16_t nbu_checksum(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* NBU_H */
