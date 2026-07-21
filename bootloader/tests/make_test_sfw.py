#!/usr/bin/env python3
"""Generate a genuinely-signed .sfw + raw public keys for the secure-boot host
test (test_secureboot.cpp). Uses the real `tools/signing/sign_firmware.py`, so
this also proves the PC signer and the on-device C verifier agree.

Outputs (in this folder, all gitignored):
  _test.sfw          256-byte header + 2 KB test firmware, signed
  _pubkey.bin        64-byte raw public key (X||Y) for the genuine key
  _wrong_pubkey.bin  64-byte raw public key of a *different* key (reject case)
"""
import os
import sys
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "signing"))
import sign_firmware  # noqa: E402  (the now-fixed signer)


def raw_pubkey(priv) -> bytes:
    n = priv.public_key().public_numbers()
    return n.x.to_bytes(32, "big") + n.y.to_bytes(32, "big")


def main() -> int:
    fw = bytes(((i * 37 + 11) & 0xFF) for i in range(2048))   # 2 KB test firmware
    fw_path = os.path.join(HERE, "_test_fw.bin")
    with open(fw_path, "wb") as f:
        f.write(fw)

    priv = ec.generate_private_key(ec.SECP256R1())
    key_path = os.path.join(HERE, "_test_key.pem")
    with open(key_path, "wb") as f:
        f.write(priv.private_bytes(serialization.Encoding.PEM,
                                   serialization.PrivateFormat.PKCS8,
                                   serialization.NoEncryption()))

    sfw = sign_firmware.sign_firmware(fw, key_path, 0x01, (1, 2, 3))  # target ble-stm32
    with open(os.path.join(HERE, "_test.sfw"), "wb") as f:
        f.write(sfw)
    with open(os.path.join(HERE, "_pubkey.bin"), "wb") as f:
        f.write(raw_pubkey(priv))

    wrong = ec.generate_private_key(ec.SECP256R1())
    with open(os.path.join(HERE, "_wrong_pubkey.bin"), "wb") as f:
        f.write(raw_pubkey(wrong))

    print(f"OK -> _test.sfw ({len(sfw)} B), _pubkey.bin, _wrong_pubkey.bin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
