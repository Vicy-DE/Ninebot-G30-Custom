#!/usr/bin/env python3
"""
nbu_xcheck.py - cross-language check: the Python sender's framing must be
byte-decodable by the C receiver (nbu.c). Generates a real BEGIN/DATA/END byte
stream with tools/flasher/nbu_send.build_frame and writes it next to the
expected firmware; nbu_xcheck.cpp replays it through nbu_receive and we diff.

Run via nbu_xcheck.cpp's harness (this only emits the inputs).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "flasher"))
from nbu_send import build_frame, CMD_BEGIN, CMD_DATA, CMD_END, ADDR_HOST  # noqa: E402

BOARD = 0x21
CHUNK = 128


def main():
    # Deterministic pseudo-firmware, deliberately not a chunk multiple (777 B).
    fw = bytes((i * 31 + 7) & 0xFF for i in range(777))

    stream = bytearray()
    stream += build_frame(ADDR_HOST, BOARD, CMD_BEGIN, 0, struct.pack("<I", len(fw)))
    seq = 0
    for off in range(0, len(fw), CHUNK):
        chunk = fw[off:off + CHUNK]
        stream += build_frame(ADDR_HOST, BOARD, CMD_DATA, 0,
                              struct.pack("<H", seq) + chunk)
        seq += 1
    stream += build_frame(ADDR_HOST, BOARD, CMD_END, 0, b"")

    with open(os.path.join(HERE, "_xcheck_stream.bin"), "wb") as f:
        f.write(stream)
    with open(os.path.join(HERE, "_xcheck_expected.bin"), "wb") as f:
        f.write(fw)
    print(f"wrote stream ({len(stream)} B) + expected firmware ({len(fw)} B)")


if __name__ == "__main__":
    main()
