# Secure-Boot Verification Test

Proves the bootloader's ECDSA-P256 secure boot works **using the bootloader's own
C code** (`fw_header.c` / `ecdsa.c` / `sha256.c` / `crc32.c`):

- a genuinely PC-signed `.sfw` (via `tools/signing/sign_firmware.py`) is **accepted**;
- every tampering — flipped firmware byte, flipped signature byte, wrong public key,
  wrong magic, wrong target — is **rejected**.

This is also a **cross-language** check: the Python signer and the on-device C
verifier must agree on the `.sfw` format and the curve/hash, or the device would
reject real updates.

## Run
```bash
pip install cryptography                 # one-time
python tools/verify_secureboot.py        # generates a signed image, runs the C verify, builds both targets
```
Or manually:
```bash
python bootloader/tests/make_test_sfw.py            # -> _test.sfw, _pubkey.bin, _wrong_pubkey.bin
gcc -O2 -std=c11 -I../common/include -c ../common/src/{fw_header,ecdsa,sha256,crc32}.c
g++ -O2 -I../common/include test_secureboot.cpp *.o -o test_secureboot
./test_secureboot _test.sfw _pubkey.bin _wrong_pubkey.bin
```

## Files
| File | Role |
|------|------|
| `test_secureboot.cpp` | runs `sfw_validate_header` / `sfw_check_crc` / `sfw_verify_signature` for accept + tamper-reject |
| `make_test_sfw.py` | signs a 2 KB test firmware + exports raw public keys (genuine + wrong) |
| `_*` | generated artifacts (gitignored) |

> This test caught five bugs that made the secure boot non-functional — see
> [`docs/SECURE_BOOT_PLAN.md`](../../docs/SECURE_BOOT_PLAN.md) (Verification section).
