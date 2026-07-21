#!/usr/bin/env python3
"""Generate the deterministic 4 KB stand-in 'stock bootloader' used by
dump_verify.resc (pattern.bin is gitignored as *.bin, so regenerate it here).
    python firmware/bootloader-dumper/sim/make_pattern.py
"""
import binascii
import os

HERE = os.path.dirname(os.path.abspath(__file__))
data = bytearray(((i * 31 + 7) & 0xFF) for i in range(4096))
data[0:16] = b"STOCKBOOT_v1.337"           # recognizable signature
out = os.path.join(HERE, "pattern.bin")
with open(out, "wb") as f:
    f.write(data)
print(f"wrote {out} ({len(data)} bytes) crc32=0x{binascii.crc32(data) & 0xFFFFFFFF:08X}")
