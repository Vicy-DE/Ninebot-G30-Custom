/**
 * @file sha256.c
 * @brief Compact SHA-256 implementation for embedded bootloaders.
 *
 * Optimized for code size (~1.5 KB compiled for Cortex-M3).
 * No lookup tables — uses computed K constants and bit operations.
 * Suitable for STM32F103 and nRF51822 with limited flash.
 */

#include "sha256.h"
#include <string.h>

/* ── SHA-256 round constants (first 32 bits of fractional parts of cube roots) ── */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* ── Helper macros ─────────────────────────────────────────────────────── */

#define ROTR(x, n)   (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)        (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x)        (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x)       (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x)       (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

/* Big-endian read/write for portability on little-endian ARM. */
static inline uint32_t be32_read(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static inline void be32_write(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

/* ── Transform one 64-byte block ───────────────────────────────────────── */

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    int i;

    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        W[i] = be32_read(block + i * 4);
    }
    for (i = 16; i < 64; i++) {
        W[i] = SIG1(W[i - 2]) + W[i - 7] + SIG0(W[i - 15]) + W[i - 16];
    }

    /* Initialize working variables */
    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    /* 64 rounds */
    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + W[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Add to hash state */
    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

/* ── Public API ────────────────────────────────────────────────────────── */

void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
    memset(ctx->buffer, 0, SHA256_BLOCK_SIZE);
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t buf_used = (size_t)(ctx->count & 0x3F);  /* bytes in partial block */

    ctx->count += len;

    /* If there's a partial block, try to fill it */
    if (buf_used > 0) {
        size_t space = SHA256_BLOCK_SIZE - buf_used;
        if (len < space) {
            memcpy(ctx->buffer + buf_used, data, len);
            return;
        }
        memcpy(ctx->buffer + buf_used, data, space);
        sha256_transform(ctx, ctx->buffer);
        data += space;
        len -= space;
    }

    /* Process full blocks */
    while (len >= SHA256_BLOCK_SIZE) {
        sha256_transform(ctx, data);
        data += SHA256_BLOCK_SIZE;
        len -= SHA256_BLOCK_SIZE;
    }

    /* Buffer remaining */
    if (len > 0) {
        memcpy(ctx->buffer, data, len);
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    size_t buf_used = (size_t)(ctx->count & 0x3F);
    uint64_t bit_count = ctx->count * 8;
    int i;

    /* Append 0x80 padding byte */
    ctx->buffer[buf_used++] = 0x80;

    /* If not enough room for 8-byte length, pad and process */
    if (buf_used > 56) {
        memset(ctx->buffer + buf_used, 0, SHA256_BLOCK_SIZE - buf_used);
        sha256_transform(ctx, ctx->buffer);
        buf_used = 0;
    }

    /* Pad to 56 bytes */
    memset(ctx->buffer + buf_used, 0, 56 - buf_used);

    /* Append bit count as big-endian 64-bit */
    for (i = 7; i >= 0; i--) {
        ctx->buffer[56 + (7 - i)] = (uint8_t)(bit_count >> (i * 8));
    }

    sha256_transform(ctx, ctx->buffer);

    /* Output digest as big-endian */
    for (i = 0; i < 8; i++) {
        be32_write(digest + i * 4, ctx->state[i]);
    }

    /* Clear sensitive state */
    memset(ctx, 0, sizeof(*ctx));
}

void sha256_hash(const uint8_t *data, size_t len,
                 uint8_t digest[SHA256_DIGEST_SIZE])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}
