/**
 * @file sha256.h
 * @brief Minimal SHA-256 implementation for bootloader use.
 *
 * Optimized for code size over speed — suitable for Cortex-M0/M3
 * bootloaders with limited flash. No dynamic allocation.
 */

#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** SHA-256 digest size in bytes. */
#define SHA256_DIGEST_SIZE  32

/** SHA-256 block size in bytes. */
#define SHA256_BLOCK_SIZE   64

/**
 * SHA-256 context (internal state).
 */
typedef struct {
    uint32_t state[8];           /**< Hash state (A-H) */
    uint64_t count;              /**< Total bytes processed */
    uint8_t  buffer[SHA256_BLOCK_SIZE]; /**< Partial block buffer */
} sha256_ctx_t;

/**
 * Initialize SHA-256 context.
 * @param ctx  Context to initialize
 */
void sha256_init(sha256_ctx_t *ctx);

/**
 * Feed data into the hash computation.
 * Can be called multiple times for streaming.
 *
 * @param ctx   Initialized context
 * @param data  Input data buffer
 * @param len   Number of bytes to process
 */
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * Finalize and output the 32-byte digest.
 * Context is invalid after this call.
 *
 * @param ctx     Context to finalize
 * @param digest  Output buffer (32 bytes)
 */
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/**
 * One-shot SHA-256: hash a contiguous buffer.
 *
 * @param data    Input data
 * @param len     Input length in bytes
 * @param digest  Output buffer (32 bytes)
 */
void sha256_hash(const uint8_t *data, size_t len,
                 uint8_t digest[SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
