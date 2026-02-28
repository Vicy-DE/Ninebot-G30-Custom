#!/usr/bin/env python3
"""
verify_firmware.py — Verify a signed firmware image (.sfw) without flashing.

Checks header integrity, CRC-32, SHA-256, and ECDSA-P256 signature.

Usage:
    python verify_firmware.py --file firmware.sfw --key public_key.pem
    python verify_firmware.py --file firmware.sfw --key private_key.pem
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib

from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.serialization import (
    load_pem_private_key,
    load_pem_public_key,
)


SFW_MAGIC = b"SFW1"
SFW_HEADER_SIZE = 256

TARGET_NAMES = {
    0x01: "BLE STM32F103",
    0x02: "BMS STM32F103",
    0x03: "nRF51822",
}

CRYPTO_NAMES = {
    0: "None",
    1: "ECDSA-P256",
}

HASH_NAMES = {
    0: "None",
    1: "SHA-256",
}


def load_public_key(key_path: str) -> ec.EllipticCurvePublicKey:
    """Load a public key from PEM file (accepts both public and private key files)."""
    with open(key_path, "rb") as f:
        pem_data = f.read()

    # Try loading as public key first
    try:
        key = load_pem_public_key(pem_data)
        if isinstance(key, ec.EllipticCurvePublicKey):
            return key
    except Exception:
        pass

    # Try loading as private key and extract public key
    try:
        key = load_pem_private_key(pem_data, password=None)
        if isinstance(key, ec.EllipticCurvePrivateKey):
            return key.public_key()
    except Exception:
        pass

    raise ValueError(f"Could not load EC key from {key_path}")


def verify_sfw(filepath: str, key_path: str) -> bool:
    """Verify a .sfw file. Returns True if all checks pass."""

    with open(filepath, "rb") as f:
        data = f.read()

    print(f"File: {filepath}")
    print(f"Total size: {len(data)} bytes")
    print()

    # ── Header checks ──────────────────────────────────────────────────
    if len(data) < SFW_HEADER_SIZE:
        print("FAIL: File too small for SFW header")
        return False

    header = data[:SFW_HEADER_SIZE]
    firmware = data[SFW_HEADER_SIZE:]

    # Magic
    magic = header[0:4]
    if magic != SFW_MAGIC:
        print(f"FAIL: Bad magic: {magic} (expected {SFW_MAGIC})")
        return False
    print(f"  Magic:          {magic.decode('ascii')}  OK")

    # Metadata
    header_ver = header[4]
    crypto_type = header[5]
    hash_type = header[6]
    target_id = header[7]
    ver_major = header[8]
    ver_minor = header[9]
    ver_patch = header[10]
    hw_rev_min = header[11]

    print(f"  Header version: {header_ver}")
    print(f"  Crypto:         {CRYPTO_NAMES.get(crypto_type, f'Unknown ({crypto_type})')}")
    print(f"  Hash:           {HASH_NAMES.get(hash_type, f'Unknown ({hash_type})')}")
    print(f"  Target:         {TARGET_NAMES.get(target_id, f'Unknown (0x{target_id:02X})')}")
    print(f"  FW Version:     {ver_major}.{ver_minor}.{ver_patch}")
    print(f"  Min HW Rev:     {hw_rev_min}")

    # Size
    fw_size = struct.unpack_from("<I", header, 0x0C)[0]
    print(f"  FW Size:        {fw_size} bytes")

    if fw_size != len(firmware):
        print(f"FAIL: Size mismatch — header says {fw_size}, actual {len(firmware)}")
        return False
    print(f"                  OK (matches actual)")

    # ── CRC-32 ──────────────────────────────────────────────────────────
    fw_crc32 = struct.unpack_from("<I", header, 0x10)[0]
    actual_crc = zlib.crc32(firmware) & 0xFFFFFFFF

    print(f"  CRC-32:         0x{fw_crc32:08X}", end="")
    if fw_crc32 != actual_crc:
        print(f"  FAIL (actual: 0x{actual_crc:08X})")
        return False
    print("  OK")

    # ── SHA-256 ─────────────────────────────────────────────────────────
    fw_hash = header[0x20:0x40]
    actual_hash = hashlib.sha256(firmware).digest()

    print(f"  SHA-256:        {fw_hash.hex()[:32]}...")
    if fw_hash != actual_hash:
        print(f"                  FAIL (actual: {actual_hash.hex()[:32]}...)")
        return False
    print(f"                  OK")

    # ── ECDSA Signature ─────────────────────────────────────────────────
    sig_r = int.from_bytes(header[0x40:0x60], byteorder="big")
    sig_s = int.from_bytes(header[0x60:0x80], byteorder="big")

    print(f"  Signature R:    {header[0x40:0x60].hex()[:32]}...")
    print(f"  Signature S:    {header[0x60:0x80].hex()[:32]}...")

    public_key = load_public_key(key_path)
    der_sig = utils.encode_dss_signature(sig_r, sig_s)

    try:
        public_key.verify(
            der_sig, actual_hash, ec.ECDSA(utils.Prehashed(hashes.SHA256()))
        )
        print(f"  Signature:      VALID")
    except Exception as e:
        print(f"  Signature:      INVALID — {e}")
        return False

    print()
    print("All checks passed.")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Verify a signed firmware image (.sfw)."
    )
    parser.add_argument(
        "--file", "-f", required=True, help="Signed firmware file (.sfw)"
    )
    parser.add_argument(
        "--key", "-k", required=True, help="Public key (.pem) or private key (.pem)"
    )
    args = parser.parse_args()

    if not os.path.isfile(args.file):
        print(f"ERROR: File not found: {args.file}")
        sys.exit(1)

    success = verify_sfw(args.file, args.key)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
