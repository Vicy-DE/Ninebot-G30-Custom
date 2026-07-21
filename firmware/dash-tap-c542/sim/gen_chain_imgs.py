#!/usr/bin/env python3
"""Generate the signed images the IAP-chain sim (test_iap_chain.cpp) flashes:

  _app.sfw     a genuinely ECDSA-signed app (target 0x01 = ble-stm32) — the BL@16
               must accept this and write it to the app slot at 0x08008000 (32 offset)
  _pubkey.bin  64-byte raw public key (X||Y) the BL verifies against
  _bl.bin      a raw bootloader image the installer@32 writes back to 0x08004000 (16)

Reuses the real tools/signing/sign_firmware.py — so the chain sim exercises the same
signer the hardware flow uses. Outputs are gitignored.
"""
import os
import sys

from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(REPO, "tools", "signing"))
import sign_firmware  # noqa: E402


def raw_pubkey(priv) -> bytes:
    n = priv.public_key().public_numbers()
    return n.x.to_bytes(32, "big") + n.y.to_bytes(32, "big")


def main() -> int:
    # App firmware to flash to the 32 offset (3000 B, not a 128-chunk multiple).
    fw = bytes(((i * 37 + 11) & 0xFF) for i in range(3000))
    key_path = os.path.join(HERE, "_chain_key.pem")
    priv = ec.generate_private_key(ec.SECP256R1())
    with open(key_path, "wb") as f:
        f.write(priv.private_bytes(serialization.Encoding.PEM,
                                   serialization.PrivateFormat.PKCS8,
                                   serialization.NoEncryption()))

    sfw = sign_firmware.sign_firmware(fw, key_path, 0x01, (1, 0, 0))  # ble-stm32
    with open(os.path.join(HERE, "_app.sfw"), "wb") as f:
        f.write(sfw)
    with open(os.path.join(HERE, "_pubkey.bin"), "wb") as f:
        f.write(raw_pubkey(priv))

    # Raw bootloader image the installer@32 writes back to the 16 offset (6 KB).
    bl = bytes(((i * 91 + 7) & 0xFF) for i in range(6144))
    with open(os.path.join(HERE, "_bl.bin"), "wb") as f:
        f.write(bl)

    print(f"OK -> _app.sfw ({len(sfw)} B, fw {len(fw)} B), _pubkey.bin, _bl.bin ({len(bl)} B)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
