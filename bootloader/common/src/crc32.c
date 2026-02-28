/**
 * @file crc32.c
 * @brief Table-less CRC-32 implementation for minimal flash usage.
 *
 * Uses bit-by-bit computation instead of a 1 KB lookup table.
 * Slower than table-based, but saves flash on constrained bootloaders.
 * CRC-32/ISO-HDLC (polynomial 0xEDB88320, reflected).
 */

#include "crc32.h"

/* ── Bit-by-bit CRC computation (no table) ─────────────────────────────── */

uint32_t crc32_init(void)
{
    return 0xFFFFFFFFU;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    size_t i;
    int bit;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint32_t crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFU;
}

uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    return crc32_final(crc32_update(crc32_init(), data, len));
}
