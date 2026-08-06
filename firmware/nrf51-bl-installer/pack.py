#!/usr/bin/env python3
"""
pack.py — bundle the nRF51 bootloader installer with the bootloader image it should install.

Port of the deleted STM32 `firmware/migration/pack.py`. Produces ONE blob for the application
slot at 0x18000:

    [ installer code ][ pad to 4 ][ bootloader image ][ trailer(16B) ]

Trailer (little-endian, word aligned), searched for backwards by the installer:

    magic       "NBBL" (0x4C42424E)
    length      bootloader image length in bytes (multiple of 4)
    crc32       CRC-32 of the bootloader image
    ~length     one's complement of length (guards against a half-written trailer)

    python pack.py --installer build/nrf51_bl_installer.bin \
                   --bootloader ../../bootloader/nrf51/build/nrf51_bootloader.bin \
                   --out build/packed.bin
    python pack.py --selftest
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib

MAGIC = 0x4C42424E          # "NBBL"
BL_START = 0x0003C000
BL_SIZE = 0x4000            # 16 KB bootloader slot
APP_START = 0x00018000
APP_SLOT = BL_START - APP_START   # 144 KB usable
TRAILER_LEN = 16


def build(installer: bytes, bootloader: bytes) -> bytes:
    """Concatenate installer + bootloader + trailer, with alignment and range checks."""
    if len(bootloader) == 0:
        raise ValueError("bootloader image is empty")
    if len(bootloader) > BL_SIZE:
        raise ValueError(f"bootloader image is {len(bootloader)} B, slot is {BL_SIZE} B")

    bl = bootloader
    if len(bl) % 4:
        bl = bl + b"\xFF" * (4 - len(bl) % 4)     # NVMC writes whole words only

    blob = bytearray(installer)
    while len(blob) % 4:
        blob.append(0xFF)

    trailer = struct.pack("<4I", MAGIC, len(bl), zlib.crc32(bl) & 0xFFFFFFFF,
                          (~len(bl)) & 0xFFFFFFFF)
    out = bytes(blob) + bl + trailer

    if len(out) > APP_SLOT:
        raise ValueError(f"packed image {len(out)} B exceeds the {APP_SLOT} B app slot")
    return out


def verify(packed: bytes) -> tuple[int, int]:
    """Re-implement the installer's backward trailer search. Returns (offset, length)."""
    p = len(packed) - TRAILER_LEN
    while p > 0:
        magic, length, crc, notlen = struct.unpack_from("<4I", packed, p)
        if (magic == MAGIC and length and length % 4 == 0 and length <= BL_SIZE
                and ((length ^ 0xFFFFFFFF) & 0xFFFFFFFF) == notlen and p - length >= 0):
            body = packed[p - length:p]
            if (zlib.crc32(body) & 0xFFFFFFFF) == crc:
                return p - length, length
        p -= 4
    raise ValueError("no valid trailer found")


def selftest() -> int:
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(f"  {' ok ' if ok else 'FAIL'} {what}")
        if not ok:
            fails += 1

    installer = bytes(range(256)) * 3                       # 768 B, not word-multiple issues
    # A plausible bootloader image: SP + thumb reset vector into the BL slot.
    bl = struct.pack("<II", 0x20003FF8, BL_START + 0x101) + bytes(range(256)) * 8
    packed = build(installer, bl)

    off, length = verify(packed)
    check(length == len(bl), "trailer records the exact bootloader length")
    check(packed[off:off + length] == bl, "bootloader image round-trips byte-for-byte")
    check(len(packed) % 4 == 0, "packed blob is word aligned")
    check(len(packed) == len(installer) + len(bl) + TRAILER_LEN, "no unexpected padding")

    # The vector table the installer sanity-checks must survive packing.
    sp, rv = struct.unpack_from("<II", packed, off)
    check(0x20000000 <= sp <= 0x20004000, "staged SP is in SRAM")
    check((rv & 1) and BL_START <= (rv & ~1) < BL_START + BL_SIZE,
          "staged reset vector is thumb and inside the BL slot")

    # Corruption must be detected.
    bad = bytearray(packed)
    bad[off + 32] ^= 0xFF
    try:
        verify(bytes(bad))
        check(False, "corrupted payload is rejected")
    except ValueError:
        check(True, "corrupted payload is rejected (CRC)")

    # A mangled length/complement pair must be rejected.
    bad2 = bytearray(packed)
    struct.pack_into("<I", bad2, len(packed) - TRAILER_LEN + 4, 0x1234)
    try:
        verify(bytes(bad2))
        check(False, "inconsistent length/complement is rejected")
    except ValueError:
        check(True, "inconsistent length/complement is rejected")

    # Oversized bootloader must be refused up front.
    try:
        build(installer, b"\x00" * (BL_SIZE + 4))
        check(False, "oversized bootloader refused")
    except ValueError:
        check(True, "oversized bootloader refused")

    # Odd-length bootloader is padded to a word.
    p2 = build(installer, b"\xAA" * 17)
    o2, l2 = verify(p2)
    check(l2 == 20 and p2[o2:o2 + 17] == b"\xAA" * 17,
          "odd-length image padded to a word boundary")

    print(f"\n{'PASSED' if not fails else 'FAILED'} ({fails} failure(s))")
    return 1 if fails else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--installer")
    ap.add_argument("--bootloader")
    ap.add_argument("--out")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not (args.installer and args.bootloader and args.out):
        ap.error("--installer, --bootloader and --out are required (or use --selftest)")

    with open(args.installer, "rb") as f:
        installer = f.read()
    with open(args.bootloader, "rb") as f:
        bootloader = f.read()

    packed = build(installer, bootloader)
    off, length = verify(packed)

    sp, rv = struct.unpack_from("<II", packed, off)
    if not (0x20000000 <= sp <= 0x20004000):
        sys.exit(f"bootloader image has an implausible initial SP 0x{sp:08X} — wrong file?")
    if not ((rv & 1) and BL_START <= (rv & ~1) < BL_START + BL_SIZE):
        sys.exit(f"bootloader reset vector 0x{rv:08X} is not inside the 0x{BL_START:X} slot — "
                 "is this image linked for the bootloader?")

    with open(args.out, "wb") as f:
        f.write(packed)

    print(f"[+] installer   : {len(installer)} B")
    print(f"[+] bootloader  : {length} B  (SP 0x{sp:08X}, reset 0x{rv:08X})")
    print(f"[+] packed      : {len(packed)} B -> {args.out}")
    print(f"[+] app slot use : {len(packed) * 100 // APP_SLOT}% of {APP_SLOT // 1024} KB")
    print(f"[i] flash to 0x{APP_START:08X}, then it installs the BL to 0x{BL_START:08X} and resets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
