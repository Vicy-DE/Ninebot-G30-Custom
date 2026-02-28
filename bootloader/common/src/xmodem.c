/**
 * @file xmodem.c
 * @brief XMODEM-CRC receiver implementation.
 *
 * Implements the receiver side of the XMODEM-CRC protocol:
 *   1. Sends 'C' to request CRC mode from sender
 *   2. Receives 128-byte data blocks with CRC-16 verification
 *   3. ACKs valid blocks, NAKs invalid ones
 *   4. Handles EOT (end of transmission) and CAN (cancel)
 *
 * Designed for bootloader firmware update over UART.
 */

#include "xmodem.h"
#include <string.h>

/* ── CRC-16/XMODEM (polynomial 0x1021) ─────────────────────────────────── */

uint16_t xmodem_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0x0000;
    size_t i;
    int bit;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* ── Receive helpers ───────────────────────────────────────────────────── */

/**
 * Receive exactly @p len bytes from UART with timeout.
 * @return 0 on success, -1 on timeout.
 */
static int recv_bytes(const xmodem_io_t *io, uint8_t *buf,
                      size_t len, uint32_t timeout_ms)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (io->uart_recv_byte(&buf[i], timeout_ms) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * Flush any pending bytes in the UART receive buffer.
 */
static void flush_rx(const xmodem_io_t *io)
{
    uint8_t dummy;
    while (io->uart_recv_byte(&dummy, 50) == 0) {
        /* Discard */
    }
}

/* ── Main receive function ─────────────────────────────────────────────── */

xmodem_result_t xmodem_receive(const xmodem_io_t *io,
                                xmodem_write_cb write_cb,
                                void *user_ctx,
                                uint32_t *total_size)
{
    uint8_t block_buf[XMODEM_BLOCK_SIZE];
    uint8_t packet_hdr[3];  /* SOH + BLK + ~BLK */
    uint8_t crc_buf[2];
    uint8_t expected_blk = 1;
    uint32_t offset = 0;
    int retries;
    int crc_request_retries;
    uint8_t byte;
    uint16_t crc_recv, crc_calc;

    if (total_size) {
        *total_size = 0;
    }

    /* ── Phase 1: Send 'C' to request CRC mode ─────────────────────── */
    crc_request_retries = 0;
    while (crc_request_retries < XMODEM_MAX_RETRIES) {
        flush_rx(io);
        io->uart_send_byte(XMODEM_CRC_CHAR);

        /* Wait for sender to respond with SOH or EOT */
        if (io->uart_recv_byte(&byte, XMODEM_CRC_POLL_MS) == 0) {
            if (byte == XMODEM_SOH) {
                /* Got first block header, proceed */
                goto receive_first_block;
            }
            if (byte == XMODEM_EOT) {
                /* Empty transfer */
                io->uart_send_byte(XMODEM_ACK);
                return XMODEM_OK;
            }
            if (byte == XMODEM_CAN) {
                /* Sender cancelled */
                return XMODEM_ERR_CANCEL;
            }
        }
        crc_request_retries++;
    }
    return XMODEM_ERR_TIMEOUT;

    /* ── Phase 2: Receive data blocks ──────────────────────────────── */
receive_first_block:
    /* The SOH byte has already been received */
    retries = 0;

    for (;;) {
        /* Read block number and complement */
        if (recv_bytes(io, packet_hdr, 2, XMODEM_TIMEOUT_MS) != 0) {
            goto retry_block;
        }

        uint8_t blk_num  = packet_hdr[0];
        uint8_t blk_comp = packet_hdr[1];

        /* Validate block number */
        if ((blk_num + blk_comp) != 0xFF) {
            goto retry_block;
        }

        /* Read 128 bytes of data */
        if (recv_bytes(io, block_buf, XMODEM_BLOCK_SIZE, XMODEM_TIMEOUT_MS) != 0) {
            goto retry_block;
        }

        /* Read CRC-16 */
        if (recv_bytes(io, crc_buf, 2, XMODEM_TIMEOUT_MS) != 0) {
            goto retry_block;
        }

        crc_recv = ((uint16_t)crc_buf[0] << 8) | crc_buf[1];
        crc_calc = xmodem_crc16(block_buf, XMODEM_BLOCK_SIZE);

        if (crc_recv != crc_calc) {
            goto retry_block;
        }

        /* Check block sequence */
        if (blk_num == expected_blk) {
            /* New block — deliver to callback */
            if (write_cb(block_buf, offset, XMODEM_BLOCK_SIZE, user_ctx) != 0) {
                /* Write failed — cancel transfer */
                io->uart_send_byte(XMODEM_CAN);
                io->uart_send_byte(XMODEM_CAN);
                return XMODEM_ERR_WRITE;
            }
            offset += XMODEM_BLOCK_SIZE;
            expected_blk++;  /* Wraps at 256 (0x00 → 0x01 for block 256) */
            retries = 0;
        } else if (blk_num == (uint8_t)(expected_blk - 1)) {
            /* Duplicate block (sender didn't get our ACK) — just ACK again */
        } else {
            /* Out of sequence */
            io->uart_send_byte(XMODEM_CAN);
            io->uart_send_byte(XMODEM_CAN);
            return XMODEM_ERR_SEQUENCE;
        }

        /* ACK the block */
        io->uart_send_byte(XMODEM_ACK);

        /* Wait for next block header */
        if (io->uart_recv_byte(&byte, XMODEM_TIMEOUT_MS) != 0) {
            return XMODEM_ERR_TIMEOUT;
        }

        if (byte == XMODEM_SOH) {
            /* Next data block, continue loop */
            continue;
        }
        if (byte == XMODEM_EOT) {
            /* Transfer complete */
            io->uart_send_byte(XMODEM_ACK);
            if (total_size) {
                *total_size = offset;
            }
            return XMODEM_OK;
        }
        if (byte == XMODEM_CAN) {
            return XMODEM_ERR_CANCEL;
        }

        /* Unexpected byte — NAK and retry */
        goto retry_block;

    retry_block:
        retries++;
        if (retries >= XMODEM_MAX_RETRIES) {
            io->uart_send_byte(XMODEM_CAN);
            io->uart_send_byte(XMODEM_CAN);
            return XMODEM_ERR_RETRIES;
        }
        flush_rx(io);
        io->uart_send_byte(XMODEM_NAK);

        /* Wait for retransmission */
        if (io->uart_recv_byte(&byte, XMODEM_TIMEOUT_MS) != 0) {
            continue;  /* Will retry */
        }
        if (byte == XMODEM_SOH) {
            continue;  /* Retry the block */
        }
        if (byte == XMODEM_CAN) {
            return XMODEM_ERR_CANCEL;
        }
        if (byte == XMODEM_EOT) {
            io->uart_send_byte(XMODEM_ACK);
            if (total_size) {
                *total_size = offset;
            }
            return XMODEM_OK;
        }
    }
}
