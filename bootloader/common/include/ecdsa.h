/**
 * @file ecdsa.h
 * @brief ECDSA-secp256r1 (NIST P-256) signature verification.
 *
 * Verify-only implementation for bootloader use.
 * Based on the micro-ecc library (compact ECC for embedded).
 * No private key operations — signing is done on the PC.
 *
 * Key format:  64 bytes raw = X[32] || Y[32] (uncompressed, no 0x04 prefix)
 * Signature:   64 bytes raw = r[32] || s[32]
 * Hash input:  32 bytes SHA-256 digest
 */

#ifndef ECDSA_H
#define ECDSA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of a coordinate (X or Y) in bytes. */
#define ECDSA_P256_COORD_SIZE   32

/** Size of the raw public key (X || Y) in bytes. */
#define ECDSA_P256_PUBKEY_SIZE  64

/** Size of the raw signature (r || s) in bytes. */
#define ECDSA_P256_SIG_SIZE     64

/** Size of the hash input in bytes (SHA-256). */
#define ECDSA_P256_HASH_SIZE    32

/**
 * Verify an ECDSA-P256 signature.
 *
 * @param pubkey     64-byte raw public key (X[32] || Y[32])
 * @param hash       32-byte SHA-256 digest of the signed message
 * @param signature  64-byte raw signature (r[32] || s[32])
 * @return 1 if signature is valid, 0 if invalid
 */
int ecdsa_p256_verify(const uint8_t pubkey[ECDSA_P256_PUBKEY_SIZE],
                      const uint8_t hash[ECDSA_P256_HASH_SIZE],
                      const uint8_t signature[ECDSA_P256_SIG_SIZE]);

/**
 * Validate that a public key point lies on the secp256r1 curve.
 * Call this once during bootloader init to detect corrupted keys.
 *
 * @param pubkey  64-byte raw public key
 * @return 1 if valid point on curve, 0 if invalid
 */
int ecdsa_p256_valid_pubkey(const uint8_t pubkey[ECDSA_P256_PUBKEY_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* ECDSA_H */
