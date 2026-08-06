/**
 * @file fw_header.h
 * @brief Signed Firmware (.sfw) image header format.
 *
 * All multi-byte fields are little-endian.
 * Total header size: 256 bytes (0x100).
 *
 * Layout:
 *   [0x000] magic            "SFW0" (0x53465730)
 *   [0x004] header_version   Format version (currently 1)
 *   [0x008] fw_version       Firmware version (packed)
 *   [0x00C] target_id        Target board identifier
 *   [0x00D] crypto_type      Signature algorithm identifier
 *   [0x00E] flags            Update behavior flags
 *   [0x010] fw_size          Firmware binary size in bytes
 *   [0x014] fw_crc32         CRC-32 of firmware binary
 *   [0x018] header_crc32     CRC-32 of header bytes 0x00–0x17
 *   [0x01C] reserved         Must be zero
 *   [0x020] fw_sha256[32]    SHA-256 digest of firmware binary
 *   [0x040] signature[64]    ECDSA-P256 signature (r‖s, 32 bytes each)
 *   [0x080] padding[128]     Reserved (0xFF)
 *   [0x100] firmware data...
 */

#ifndef FW_HEADER_H
#define FW_HEADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Magic number ──────────────────────────────────────────────────────── */

/** "SFW0" in little-endian. */
#define SFW_MAGIC           0x53465730U

/** Current header format version. */
#define SFW_HEADER_VERSION  1U

/** Fixed header size in bytes. */
#define SFW_HEADER_SIZE     256U

/* ── Target board identifiers ──────────────────────────────────────────── */

/* 0x01 (BLE STM32) and 0x02 (BMS STM32) are RETIRED — the dashboard has no STM32 and the BMS is
 * out of scope. The ids stay reserved so old signed images are rejected rather than misinterpreted. */
#define SFW_TARGET_RETIRED_1   0x01   /**< retired: was "BLE dashboard STM32F103C8T6" */
#define SFW_TARGET_RETIRED_2   0x02   /**< retired: was "BMS battery STM32F103C8T6" */
#define SFW_TARGET_NRF51822    0x03   /**< dashboard nRF51822 — application image */
#define SFW_TARGET_NRF51822_BL 0x04   /**< dashboard nRF51822 — BOOTLOADER image (self-update) */

/* ── Crypto type identifiers ───────────────────────────────────────────── */

#define SFW_CRYPTO_ECDSA_P256_SHA256  0x01  /**< ECDSA secp256r1 + SHA-256 */

/* ── Flags ─────────────────────────────────────────────────────────────── */

#define SFW_FLAG_FORCE_UPDATE      (1U << 0)  /**< Skip version check */
#define SFW_FLAG_PRESERVE_CONFIG   (1U << 1)  /**< Don't erase config pages */

/* ── Maximum firmware sizes ────────────────────────────────────────────── */

/** nRF51822: 256KB total, 96KB SoftDevice, 16KB bootloader+settings → 96KB app */
#define SFW_MAX_FW_SIZE_NRF51   (96U * 1024U)

/* ── Header structure ──────────────────────────────────────────────────── */

/**
 * Signed Firmware image header (256 bytes, packed).
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;              /**< 0x000: Must be SFW_MAGIC */
    uint32_t header_version;     /**< 0x004: Header format version */
    uint32_t fw_version;         /**< 0x008: Firmware version (major.minor.patch packed) */
    uint8_t  target_id;          /**< 0x00C: Target board (SFW_TARGET_*) */
    uint8_t  crypto_type;        /**< 0x00D: Signature algorithm (SFW_CRYPTO_*) */
    uint16_t flags;              /**< 0x00E: Behavior flags (SFW_FLAG_*) */
    uint32_t fw_size;            /**< 0x010: Firmware binary size in bytes */
    uint32_t fw_crc32;           /**< 0x014: CRC-32 of firmware binary */
    uint32_t header_crc32;       /**< 0x018: CRC-32 of first 24 header bytes */
    uint32_t reserved;           /**< 0x01C: Reserved, must be 0 */
    uint8_t  fw_sha256[32];      /**< 0x020: SHA-256 digest of firmware binary */
    uint8_t  signature[64];      /**< 0x040: ECDSA signature (r[32] || s[32]) */
    uint8_t  padding[128];       /**< 0x080: Reserved padding (0xFF) */
} sfw_header_t;

_Static_assert(sizeof(sfw_header_t) == SFW_HEADER_SIZE,
               "sfw_header_t must be exactly 256 bytes");

/* ── Validation result codes ───────────────────────────────────────────── */

typedef enum {
    SFW_OK                  = 0,   /**< Header and signature valid */
    SFW_ERR_MAGIC           = 1,   /**< Bad magic number */
    SFW_ERR_VERSION         = 2,   /**< Unsupported header version */
    SFW_ERR_TARGET          = 3,   /**< Wrong target board */
    SFW_ERR_CRYPTO          = 4,   /**< Unsupported crypto type */
    SFW_ERR_SIZE            = 5,   /**< Firmware size exceeds max */
    SFW_ERR_HEADER_CRC      = 6,   /**< Header CRC mismatch */
    SFW_ERR_FW_CRC          = 7,   /**< Firmware CRC mismatch */
    SFW_ERR_SHA256          = 8,   /**< SHA-256 hash mismatch */
    SFW_ERR_SIGNATURE       = 9,   /**< ECDSA signature invalid */
    SFW_ERR_DOWNGRADE       = 10,  /**< Firmware version too old */
} sfw_result_t;

/* ── API ───────────────────────────────────────────────────────────────── */

/**
 * Parse and validate the .sfw header (magic, version, CRC, target).
 * Does NOT verify the ECDSA signature — call sfw_verify_signature() after.
 *
 * @param header      Pointer to 256-byte header buffer
 * @param target_id   Expected target board ID (SFW_TARGET_*)
 * @param max_size    Maximum allowed firmware size
 * @return SFW_OK if header is structurally valid
 */
sfw_result_t sfw_validate_header(const sfw_header_t *header,
                                  uint8_t target_id,
                                  uint32_t max_size);

/**
 * Verify the ECDSA-P256 signature of the firmware.
 * Computes SHA-256 over the firmware binary and checks the signature
 * in the header against the embedded public key.
 *
 * @param header      Pointer to validated .sfw header
 * @param fw_data     Pointer to firmware binary data
 * @param fw_size     Size of firmware data in bytes
 * @param pubkey      64-byte raw public key (X[32] || Y[32])
 * @return SFW_OK if signature is valid
 */
sfw_result_t sfw_verify_signature(const sfw_header_t *header,
                                   const uint8_t *fw_data,
                                   uint32_t fw_size,
                                   const uint8_t *pubkey);

/**
 * Quick CRC-32 integrity check of firmware binary.
 * Faster than SHA-256, used as a first-pass check.
 *
 * @param fw_data     Pointer to firmware binary
 * @param fw_size     Size of firmware data
 * @param expected    Expected CRC-32 from header
 * @return SFW_OK if CRC matches
 */
sfw_result_t sfw_check_crc(const uint8_t *fw_data,
                            uint32_t fw_size,
                            uint32_t expected);

#ifdef __cplusplus
}
#endif

#endif /* FW_HEADER_H */
