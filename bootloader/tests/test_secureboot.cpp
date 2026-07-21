/**
 * @file test_secureboot.cpp
 * @brief Host verification of the secure-boot chain using the bootloader's OWN
 *        C code (fw_header.c / ecdsa.c / sha256.c / crc32.c).
 *
 * Proves the security guarantee end-to-end: a genuinely Python-signed `.sfw` is
 * accepted, and ANY tampering — a flipped firmware byte, a flipped signature
 * byte, the wrong public key, a wrong magic, or the wrong target — is rejected.
 *
 *   make_test_sfw.py produces _test.sfw, _pubkey.bin, _wrong_pubkey.bin.
 *   Usage: test_secureboot <sfw> <pubkey.bin> <wrong_pubkey.bin>
 */
// fw_header.h uses C11 `_Static_assert`; map it to C++ `static_assert`.
#ifdef __cplusplus
#define _Static_assert static_assert
#endif
#include "fw_header.h"
#include "ecdsa.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* msg) {
    printf("    [%s] %s\n", ok ? " ok " : "FAIL", msg);
    if (ok) ++g_pass; else ++g_fail;
}

static std::vector<uint8_t> readfile(const char* path) {
    std::vector<uint8_t> v;
    FILE* f = std::fopen(path, "rb");
    if (!f) return v;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    v.resize(n > 0 ? size_t(n) : 0);
    if (!v.empty()) { size_t got = std::fread(v.data(), 1, v.size(), f); v.resize(got); }
    std::fclose(f);
    return v;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("usage: %s <sfw> <pubkey.bin> <wrong_pubkey.bin>\n", argv[0]);
        return 2;
    }
    printf("=========================================================\n");
    printf(" Secure-boot bootloader verification (C verify code)\n");
    printf("=========================================================\n");

    std::vector<uint8_t> sfw   = readfile(argv[1]);
    std::vector<uint8_t> pub   = readfile(argv[2]);
    std::vector<uint8_t> wrong = readfile(argv[3]);
    if (sfw.size() <= SFW_HEADER_SIZE || pub.size() != 64 || wrong.size() != 64) {
        printf("ERROR: bad input files (sfw=%zu pub=%zu wrong=%zu)\n",
               sfw.size(), pub.size(), wrong.size());
        return 2;
    }

    const sfw_header_t* h = reinterpret_cast<const sfw_header_t*>(sfw.data());
    const uint8_t* fw = sfw.data() + SFW_HEADER_SIZE;
    uint32_t fwsize = uint32_t(sfw.size() - SFW_HEADER_SIZE);

    printf("  Crypto-core sanity:\n");
    check(ecdsa_p256_valid_pubkey(pub.data()) == 1,
          "genuine public key is a valid P-256 point (exercises bn_mod_mul)");

    printf("  Genuine signed image:\n");
    check(sfw_validate_header(h, SFW_TARGET_BLE_STM32, SFW_MAX_FW_SIZE_STM32) == SFW_OK,
          "header valid (magic SFW0, version, target, header CRC)");
    check(sfw_check_crc(fw, fwsize, h->fw_crc32) == SFW_OK, "firmware CRC-32 matches");
    check(sfw_verify_signature(h, fw, fwsize, pub.data()) == SFW_OK,
          "ECDSA-P256 signature ACCEPTED (PC-signed, C-verified)");

    printf("  Tampering must be rejected:\n");
    {   // flip one firmware byte
        std::vector<uint8_t> fwt(fw, fw + fwsize);
        fwt[10] ^= 0xFF;
        check(sfw_verify_signature(h, fwt.data(), fwsize, pub.data()) == SFW_ERR_SHA256,
              "flipped firmware byte -> rejected (SHA-256 mismatch)");
    }
    {   // flip one signature byte
        sfw_header_t ht = *h;
        ht.signature[0] ^= 0xFF;
        check(sfw_verify_signature(&ht, fw, fwsize, pub.data()) == SFW_ERR_SIGNATURE,
              "flipped signature byte -> rejected (bad signature)");
    }
    check(sfw_verify_signature(h, fw, fwsize, wrong.data()) == SFW_ERR_SIGNATURE,
          "wrong public key -> rejected (bad signature)");
    {   // corrupt magic
        sfw_header_t hm = *h;
        hm.magic ^= 0xFFu;
        check(sfw_validate_header(&hm, SFW_TARGET_BLE_STM32, SFW_MAX_FW_SIZE_STM32) == SFW_ERR_MAGIC,
              "corrupted magic -> rejected");
    }
    check(sfw_validate_header(h, SFW_TARGET_NRF51822, SFW_MAX_FW_SIZE_STM32) == SFW_ERR_TARGET,
          "wrong target board -> rejected");

    printf("---------------------------------------------------------\n");
    printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0)
        printf(" VERDICT: secure boot works — only correctly-signed firmware for\n"
               "          this board is accepted; all tampering is rejected.\n");
    else
        printf(" VERDICT: secure boot FAILED verification.\n");
    printf("=========================================================\n");
    return g_fail ? 1 : 0;
}
