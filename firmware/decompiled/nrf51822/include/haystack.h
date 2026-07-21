/**
 * @file haystack.h
 * @brief Apple FindMy / OpenHaystack advertisement builder + key schedule
 *        (Req 15). Header-only, host-testable.
 *
 * Builds the 31-byte non-connectable (`ADV_NONCONN_IND`) manufacturer-specific
 * payload that Apple's Find My network reports, so the scooter is trackable while
 * asleep. Public keys are pre-generated offline (OpenHaystack), 28-byte P-224
 * compressed keys stored in nRF51 flash; one key is active per 900 s window.
 *
 * Payload layout (Req 15.1; OpenHaystack `byte[]` convention):
 * @code
 *   [0]  0x1E              length of the AD structure that follows (30)
 *   [1]  0xFF              manufacturer-specific data
 *   [2]  0x4C 0x00         Apple company id (0x004C, little-endian)
 *   [4]  0x12              FindMy "offline finding" type
 *   [5]  0x19              length of remaining bytes (25)
 *   [6]  status            key-roll indicator / battery (reserved)
 *   [7..28]  key[6..27]    22 bytes of the compressed public key
 *   [29] key[0] >> 6       upper 2 bits of the first key byte
 *   [30] hint              0x00 (OpenHaystack default; Req notes last key byte)
 * @endcode
 */
#ifndef NINEBOT_HAYSTACK_H
#define NINEBOT_HAYSTACK_H

#include <cstdint>
#include <cstddef>

namespace ninebot {
namespace nrf51 {
namespace haystack {

static constexpr size_t PUBKEY_LEN = 28;  ///< compressed P-224 public key
static constexpr size_t ADV_LEN    = 31;  ///< BLE adv payload length
static constexpr uint32_t KEY_PERIOD_S = 900; ///< 15-minute rotation (Req 15.2)

/**
 * Build the 31-byte FindMy advertisement payload from a public key.
 * @param pubkey 28-byte compressed P-224 public key
 * @param out    31-byte output buffer
 * @param status status byte (default 0x00)
 * @param hint   hint byte (default 0x00 — matches deployed OpenHaystack)
 */
inline void buildAdv(const uint8_t pubkey[PUBKEY_LEN], uint8_t out[ADV_LEN],
                     uint8_t status = 0x00, uint8_t hint = 0x00) {
    out[0] = 0x1E;          // length (30 bytes follow)
    out[1] = 0xFF;          // manufacturer specific data
    out[2] = 0x4C;          // Apple company id LSB
    out[3] = 0x00;          // Apple company id MSB
    out[4] = 0x12;          // offline finding type
    out[5] = 0x19;          // length of remaining (25)
    out[6] = status;
    for (size_t i = 0; i < 22; ++i) out[7 + i] = pubkey[6 + i]; // key[6..27]
    out[29] = uint8_t(pubkey[0] >> 6);
    out[30] = hint;
}

/**
 * Index of the active rolling key for an absolute time, given a base epoch.
 * @param nowS     seconds (monotonic or wall clock)
 * @param baseS    epoch at which key index 0 became active
 * @param numKeys  number of keys stored in flash (≥96, Req 15.2)
 * @return key index in [0, numKeys)
 */
inline uint32_t keyIndex(uint32_t nowS, uint32_t baseS, uint32_t numKeys) {
    if (numKeys == 0) return 0;
    uint32_t periods = (nowS >= baseS) ? (nowS - baseS) / KEY_PERIOD_S : 0;
    return periods % numKeys;
}

/**
 * The 6-byte BLE MAC FindMy expects: top 2 bits of key[0] forced to 1, i.e.
 * `(key[0] | 0xC0), key[1..5]`. Set as a random static address before advertising.
 */
inline void macFromKey(const uint8_t pubkey[PUBKEY_LEN], uint8_t mac[6]) {
    mac[0] = uint8_t(pubkey[0] | 0xC0);
    for (size_t i = 1; i < 6; ++i) mac[i] = pubkey[i];
}

} // namespace haystack
} // namespace nrf51
} // namespace ninebot

#endif // NINEBOT_HAYSTACK_H
