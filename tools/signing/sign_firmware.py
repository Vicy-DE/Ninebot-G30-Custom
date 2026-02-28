#!/usr/bin/env python3
"""
sign_firmware.py — Create a signed firmware image (.sfw) for the bootloader.

Takes a raw firmware binary (.bin) and produces a signed firmware file (.sfw)
with a 256-byte header containing:
  - Magic number: "SFW1"
  - Target board ID
  - Version number
  - Firmware size & CRC-32
  - SHA-256 hash of the firmware
  - ECDSA-P256 signature over the hash

Usage:
    python sign_firmware.py \\
        --input firmware.bin \\
        --output firmware.sfw \\
        --key private_key.pem \\
        --target ble-stm32 \\
        --version 1.2.3

Target IDs:
    ble-stm32  (0x01) — BLE Dashboard STM32F103
    bms-stm32  (0x02) — BMS STM32F103
    nrf51822   (0x03) — BLE Dashboard nRF51822
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.serialization import load_pem_private_key


# ── Constants matching fw_header.h ──────────────────────────────────────────

SFW_MAGIC = b"SFW1"
SFW_HEADER_SIZE = 256
SFW_CRYPTO_ECDSA_P256 = 1
SFW_HASH_SHA256 = 1

TARGET_IDS = {
    "ble-stm32": 0x01,
    "bms-stm32": 0x02,
    "nrf51822": 0x03,
}

# Maximum firmware sizes per target (safety check)
MAX_FW_SIZE = {
    0x01: 46 * 1024,   # BLE STM32: 46 KB
    0x02: 46 * 1024,   # BMS STM32: 46 KB
    0x03: 80 * 1024,   # nRF51822: 80 KB
}


def parse_version(version_str: str) -> tuple[int, int, int]:
    """Parse version string 'major.minor.patch' into tuple."""
    parts = version_str.split(".")
    if len(parts) != 3:
        raise ValueError(f"Version must be 'major.minor.patch', got '{version_str}'")
    return tuple(int(p) for p in parts)


def compute_crc32(data: bytes) -> int:
    """Compute CRC-32 (same polynomial as bootloader's table-less implementation)."""
    return zlib.crc32(data) & 0xFFFFFFFF


def sign_firmware(
    firmware_data: bytes,
    private_key_path: str,
    target_id: int,
    version: tuple[int, int, int],
    hw_rev_min: int = 0,
) -> bytes:
    """
    Create a signed .sfw image.

    Returns the complete image (256-byte header + firmware data).
    """

    fw_size = len(firmware_data)
    max_size = MAX_FW_SIZE.get(target_id, 128 * 1024)

    if fw_size > max_size:
        raise ValueError(
            f"Firmware too large: {fw_size} bytes "
            f"(max {max_size} bytes for target 0x{target_id:02X})"
        )

    if fw_size == 0:
        raise ValueError("Firmware binary is empty")

    # ── Load private key ────────────────────────────────────────────────
    with open(private_key_path, "rb") as f:
        pem_data = f.read()

    private_key = load_pem_private_key(pem_data, password=None)
    if not isinstance(private_key, ec.EllipticCurvePrivateKey):
        raise TypeError("Key is not an EC private key")
    if not isinstance(private_key.curve, ec.SECP256R1):
        raise TypeError(f"Key curve is {private_key.curve.name}, expected secp256r1")

    # ── Compute firmware hash & CRC ─────────────────────────────────────
    fw_crc32 = compute_crc32(firmware_data)
    fw_sha256 = hashlib.sha256(firmware_data).digest()

    print(f"  Firmware size: {fw_size} bytes")
    print(f"  CRC-32:        0x{fw_crc32:08X}")
    print(f"  SHA-256:       {fw_sha256.hex()}")

    # ── ECDSA sign the SHA-256 hash ─────────────────────────────────────
    # Sign with deterministic RFC 6979 nonce (default in cryptography lib)
    der_signature = private_key.sign(fw_sha256, ec.ECDSA(utils.Prehashed(hashes.SHA256())))

    # Decode DER to raw (r, s) — each 32 bytes, big-endian
    r, s = utils.decode_dss_signature(der_signature)
    sig_r = r.to_bytes(32, byteorder="big")
    sig_s = s.to_bytes(32, byteorder="big")

    print(f"  Signature R:   {sig_r.hex()}")
    print(f"  Signature S:   {sig_s.hex()}")

    # ── Build 256-byte header ───────────────────────────────────────────
    # struct sfw_header layout (see fw_header.h):
    #   0x00: magic[4]         "SFW1"
    #   0x04: header_version   uint8
    #   0x05: crypto_type      uint8
    #   0x06: hash_type        uint8
    #   0x07: target_id        uint8
    #   0x08: fw_version_major uint8
    #   0x09: fw_version_minor uint8
    #   0x0A: fw_version_patch uint8
    #   0x0B: hw_rev_min       uint8
    #   0x0C: fw_size          uint32 LE
    #   0x10: fw_crc32         uint32 LE
    #   0x14: reserved[12]     zeros
    #   0x20: fw_hash[32]      SHA-256
    #   0x40: signature[64]    ECDSA (r[32] + s[32])
    #   0x80: padding[128]     zeros (to reach 256 bytes)

    header = bytearray(SFW_HEADER_SIZE)

    # Magic
    header[0:4] = SFW_MAGIC

    # Metadata
    header[4] = 1  # header_version
    header[5] = SFW_CRYPTO_ECDSA_P256
    header[6] = SFW_HASH_SHA256
    header[7] = target_id
    header[8] = version[0]  # major
    header[9] = version[1]  # minor
    header[10] = version[2]  # patch
    header[11] = hw_rev_min

    # Firmware size (little-endian uint32)
    struct.pack_into("<I", header, 0x0C, fw_size)

    # CRC-32 (little-endian uint32)
    struct.pack_into("<I", header, 0x10, fw_crc32)

    # Reserved (already zero)

    # SHA-256 hash
    header[0x20:0x40] = fw_sha256

    # Signature: r || s
    header[0x40:0x60] = sig_r
    header[0x60:0x80] = sig_s

    # Padding (already zero)

    return bytes(header) + firmware_data


def main():
    parser = argparse.ArgumentParser(
        description="Create a signed firmware image (.sfw) for the secure bootloader."
    )
    parser.add_argument(
        "--input", "-i", required=True, help="Input firmware binary (.bin)"
    )
    parser.add_argument(
        "--output", "-o", help="Output signed firmware (.sfw). Default: input with .sfw extension"
    )
    parser.add_argument(
        "--key", "-k", required=True, help="ECDSA-P256 private key file (.pem)"
    )
    parser.add_argument(
        "--target",
        "-t",
        required=True,
        choices=TARGET_IDS.keys(),
        help="Target board identifier",
    )
    parser.add_argument(
        "--version",
        "-v",
        required=True,
        help="Firmware version (major.minor.patch, e.g., 1.2.3)",
    )
    parser.add_argument(
        "--hw-rev-min",
        type=int,
        default=0,
        help="Minimum hardware revision required (default: 0 = any)",
    )
    args = parser.parse_args()

    # Default output name
    if args.output is None:
        base, _ = os.path.splitext(args.input)
        args.output = base + ".sfw"

    # Read firmware binary
    print(f"Reading firmware: {args.input}")
    with open(args.input, "rb") as f:
        firmware_data = f.read()

    target_id = TARGET_IDS[args.target]
    version = parse_version(args.version)

    print(f"Target: {args.target} (0x{target_id:02X})")
    print(f"Version: {version[0]}.{version[1]}.{version[2]}")
    print()

    # Sign
    print("Signing firmware...")
    sfw_image = sign_firmware(
        firmware_data=firmware_data,
        private_key_path=args.key,
        target_id=target_id,
        version=version,
        hw_rev_min=args.hw_rev_min,
    )

    # Write output
    with open(args.output, "wb") as f:
        f.write(sfw_image)

    print()
    print(f"Signed firmware written: {args.output}")
    print(f"  Total size: {len(sfw_image)} bytes (header: {SFW_HEADER_SIZE}, firmware: {len(firmware_data)})")
    print()

    # Verify the signature we just made (sanity check)
    print("Verifying signature (sanity check)...")
    verify_sfw(args.output, args.key)
    print("  OK — signature verified successfully")


def verify_sfw(sfw_path: str, private_key_path: str):
    """Quick verification of a .sfw file using the signing key (for testing)."""
    with open(sfw_path, "rb") as f:
        data = f.read()

    if len(data) < SFW_HEADER_SIZE:
        raise ValueError("File too small for SFW header")

    header = data[:SFW_HEADER_SIZE]
    firmware = data[SFW_HEADER_SIZE:]

    if header[0:4] != SFW_MAGIC:
        raise ValueError(f"Bad magic: {header[0:4]}")

    fw_size = struct.unpack_from("<I", header, 0x0C)[0]
    if fw_size != len(firmware):
        raise ValueError(f"Size mismatch: header says {fw_size}, got {len(firmware)}")

    fw_crc32 = struct.unpack_from("<I", header, 0x10)[0]
    actual_crc = compute_crc32(firmware)
    if fw_crc32 != actual_crc:
        raise ValueError(f"CRC mismatch: header 0x{fw_crc32:08X}, actual 0x{actual_crc:08X}")

    fw_hash = header[0x20:0x40]
    actual_hash = hashlib.sha256(firmware).digest()
    if fw_hash != actual_hash:
        raise ValueError("SHA-256 hash mismatch")

    sig_r = int.from_bytes(header[0x40:0x60], byteorder="big")
    sig_s = int.from_bytes(header[0x60:0x80], byteorder="big")

    # Re-encode as DER for verification
    der_sig = utils.encode_dss_signature(sig_r, sig_s)

    with open(private_key_path, "rb") as f:
        pem_data = f.read()
    private_key = load_pem_private_key(pem_data, password=None)
    public_key = private_key.public_key()

    public_key.verify(der_sig, actual_hash, ec.ECDSA(utils.Prehashed(hashes.SHA256())))


if __name__ == "__main__":
    main()
