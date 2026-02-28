/**
 * @file xmodem.h
 * @brief XMODEM-CRC receiver for firmware update over UART.
 *
 * Implements the XMODEM-CRC protocol (128-byte data blocks with
 * CRC-16 error detection). The receiver drives the protocol by
 * sending 'C' to request CRC mode, then ACK/NAK per block.
 *
 * Usage:
 *   1. Provide platform-specific UART callbacks via xmodem_callbacks_t
 *   2. Call xmodem_receive() with a write callback for received data
 *   3. The function blocks until transfer completes or times out
 *
 * Block format:
 *   [SOH] [BLK] [~BLK] [128 bytes data] [CRC-HI] [CRC-LO]
 *
 * Control bytes:
 *   SOH = 0x01  (Start of 128-byte block)
 *   EOT = 0x04  (End of transmission)
 *   ACK = 0x06  (Acknowledge)
 *   NAK = 0x15  (Negative acknowledge)
 *   CAN = 0x18  (Cancel transfer)
 *   'C' = 0x43  (CRC mode request)
 */

#ifndef XMODEM_H
#define XMODEM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Protocol constants ────────────────────────────────────────────────── */

#define XMODEM_SOH          0x01
#define XMODEM_EOT          0x04
#define XMODEM_ACK          0x06
#define XMODEM_NAK          0x15
#define XMODEM_CAN          0x18
#define XMODEM_CRC_CHAR     'C'

#define XMODEM_BLOCK_SIZE   128    /**< Data bytes per block */
#define XMODEM_PACKET_SIZE  133    /**< SOH + BLK + ~BLK + 128 data + CRC_H + CRC_L */

/** Maximum retries before aborting. */
#define XMODEM_MAX_RETRIES  10

/** Timeout waiting for sender response (milliseconds). */
#define XMODEM_TIMEOUT_MS   3000

/** Initial 'C' send interval (milliseconds). */
#define XMODEM_CRC_POLL_MS  1000

/* ── Result codes ──────────────────────────────────────────────────────── */

typedef enum {
    XMODEM_OK           = 0,   /**< Transfer completed successfully */
    XMODEM_ERR_TIMEOUT  = -1,  /**< Sender did not respond */
    XMODEM_ERR_CANCEL   = -2,  /**< Transfer cancelled by sender */
    XMODEM_ERR_RETRIES  = -3,  /**< Maximum retries exceeded */
    XMODEM_ERR_SEQUENCE = -4,  /**< Block sequence error */
    XMODEM_ERR_WRITE    = -5,  /**< Write callback failed */
    XMODEM_ERR_ABORT    = -6,  /**< Aborted by receiver */
} xmodem_result_t;

/* ── Platform callbacks ────────────────────────────────────────────────── */

/**
 * Platform-specific I/O callbacks for XMODEM.
 * The bootloader must provide these for its UART peripheral.
 */
typedef struct {
    /**
     * Send a single byte over UART.
     * @param byte  Byte to transmit
     */
    void (*uart_send_byte)(uint8_t byte);

    /**
     * Receive a single byte from UART with timeout.
     * @param byte     Pointer to store received byte
     * @param timeout  Timeout in milliseconds
     * @return 0 on success, -1 on timeout
     */
    int (*uart_recv_byte)(uint8_t *byte, uint32_t timeout);

    /**
     * Get current system time in milliseconds.
     * Used for timeout calculations.
     * @return Milliseconds since boot (wrapping is OK)
     */
    uint32_t (*get_tick_ms)(void);
} xmodem_io_t;

/**
 * Callback invoked for each received data block.
 * The bootloader typically writes this to a staging buffer or flash.
 *
 * @param data       Pointer to block data (128 bytes, last block may be padded)
 * @param offset     Byte offset from start of transfer
 * @param length     Number of valid bytes (always 128 except possibly last block)
 * @param user_ctx   User context pointer (passed through from xmodem_receive)
 * @return 0 on success, -1 to abort transfer
 */
typedef int (*xmodem_write_cb)(const uint8_t *data, uint32_t offset,
                                uint32_t length, void *user_ctx);

/* ── API ───────────────────────────────────────────────────────────────── */

/**
 * Receive a file via XMODEM-CRC protocol.
 *
 * Blocks until the transfer completes, times out, or is cancelled.
 * Calls write_cb for each successfully received 128-byte block.
 *
 * @param io          Platform I/O callbacks
 * @param write_cb    Callback for received data blocks
 * @param user_ctx    Opaque pointer passed to write_cb
 * @param total_size  Output: total bytes received (may include padding on last block)
 * @return XMODEM_OK on success, error code otherwise
 */
xmodem_result_t xmodem_receive(const xmodem_io_t *io,
                                xmodem_write_cb write_cb,
                                void *user_ctx,
                                uint32_t *total_size);

/**
 * Compute XMODEM CRC-16 (polynomial 0x1021, init 0x0000).
 * Used internally, exposed for testing.
 *
 * @param data  Input data
 * @param len   Data length
 * @return CRC-16 value
 */
uint16_t xmodem_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* XMODEM_H */
