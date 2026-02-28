/**
 * @file fw_header.c
 * @brief .sfw header parsing, validation, and signature verification.
 */

#include "fw_header.h"
#include "crc32.h"
#include "sha256.h"
#include "ecdsa.h"
#include <string.h>

sfw_result_t sfw_validate_header(const sfw_header_t *header,
                                  uint8_t target_id,
                                  uint32_t max_size)
{
    /* Check magic */
    if (header->magic != SFW_MAGIC) {
        return SFW_ERR_MAGIC;
    }

    /* Check header version */
    if (header->header_version != SFW_HEADER_VERSION) {
        return SFW_ERR_VERSION;
    }

    /* Check target */
    if (header->target_id != target_id) {
        return SFW_ERR_TARGET;
    }

    /* Check crypto type */
    if (header->crypto_type != SFW_CRYPTO_ECDSA_P256_SHA256) {
        return SFW_ERR_CRYPTO;
    }

    /* Check firmware size */
    if (header->fw_size == 0 || header->fw_size > max_size) {
        return SFW_ERR_SIZE;
    }

    /* Check reserved field */
    if (header->reserved != 0) {
        return SFW_ERR_MAGIC;  /* Strict: reserved must be zero */
    }

    /* Verify header CRC (covers first 24 bytes: offset 0x00–0x17) */
    uint32_t hdr_crc = crc32_compute((const uint8_t *)header, 24);
    if (hdr_crc != header->header_crc32) {
        return SFW_ERR_HEADER_CRC;
    }

    return SFW_OK;
}

sfw_result_t sfw_check_crc(const uint8_t *fw_data,
                            uint32_t fw_size,
                            uint32_t expected)
{
    uint32_t computed = crc32_compute(fw_data, fw_size);
    if (computed != expected) {
        return SFW_ERR_FW_CRC;
    }
    return SFW_OK;
}

sfw_result_t sfw_verify_signature(const sfw_header_t *header,
                                   const uint8_t *fw_data,
                                   uint32_t fw_size,
                                   const uint8_t *pubkey)
{
    uint8_t computed_hash[SHA256_DIGEST_SIZE];

    /* Compute SHA-256 of the firmware binary */
    sha256_hash(fw_data, fw_size, computed_hash);

    /* Verify the hash matches the one stored in the header */
    if (memcmp(computed_hash, header->fw_sha256, SHA256_DIGEST_SIZE) != 0) {
        return SFW_ERR_SHA256;
    }

    /* Verify ECDSA-P256 signature over the hash */
    if (!ecdsa_p256_verify(pubkey, computed_hash, header->signature)) {
        return SFW_ERR_SIGNATURE;
    }

    return SFW_OK;
}
