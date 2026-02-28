/**
 * @file crc32.h
 * @brief CRC-32 computation (ISO 3309 / ITU-T V.42 polynomial).
 *
 * Table-less implementation to minimize flash usage.
 * Used for quick firmware integrity checks before full SHA-256 + ECDSA.
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Standard CRC-32 polynomial (reflected). */
#define CRC32_POLYNOMIAL  0xEDB88320U

/**
 * Compute CRC-32 over a data buffer.
 *
 * @param data  Input data
 * @param len   Data length in bytes
 * @return CRC-32 value
 */
uint32_t crc32_compute(const uint8_t *data, size_t len);

/**
 * Incremental CRC-32: initialize.
 * @return Initial CRC state
 */
uint32_t crc32_init(void);

/**
 * Incremental CRC-32: process a chunk of data.
 * @param crc   Current CRC state (from crc32_init or previous crc32_update)
 * @param data  Input data chunk
 * @param len   Chunk length in bytes
 * @return Updated CRC state
 */
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);

/**
 * Incremental CRC-32: finalize.
 * @param crc  Current CRC state
 * @return Final CRC-32 value
 */
uint32_t crc32_final(uint32_t crc);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */
