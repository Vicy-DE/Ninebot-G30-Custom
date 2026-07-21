#!/usr/bin/env python3
"""Feed a signed .sfw to the NUCLEO-C542RC software-UART programmer.

The C542 programmer firmware (firmware/dash-tap-c542/main_programmer.c) expects, on
its ST-LINK VCP: a 4-byte little-endian length, then that many image bytes. It then
flashes the image to the target bootloader via NBU over the bit-banged UART and prints
a `[C542-PROG]` result. This tool sends that and echoes the result.

    python tools/flasher/c5_feed.py --port COM7 --file build/ble_app.sfw
    python tools/flasher/c5_feed.py --selftest        # no hardware: verify the framing
"""
from __future__ import annotations
import argparse
import os
import struct
import sys
import time


def frame(image: bytes) -> bytes:
    """The exact bytes the C542 programmer expects: u32 LE length + image."""
    return struct.pack("<I", len(image)) + image


def _selftest() -> int:
    img = bytes(range(10))
    f = frame(img)
    ok = (len(f) == 4 + len(img)
          and struct.unpack("<I", f[:4])[0] == len(img)
          and f[4:] == img)
    print(f"  [{' ok ' if ok else 'FAIL'}] length-prefixed frame is u32 LE len + image")
    print(f"\n  selftest: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="C542 ST-LINK VCP serial port (e.g. COM7)")
    ap.add_argument("--file", help="signed firmware image (.sfw)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return _selftest()
    if not args.port or not args.file:
        ap.error("--port and --file are required (or use --selftest)")
    if not os.path.isfile(args.file):
        print(f"ERROR: not found: {args.file}", file=sys.stderr); return 1

    try:
        import serial
    except ImportError:
        print("ERROR: pyserial not installed (pip install pyserial).", file=sys.stderr)
        return 2

    image = open(args.file, "rb").read()
    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    time.sleep(0.2)
    ser.reset_input_buffer()
    print(f"Sending {len(image)} bytes to the C542 programmer on {args.port}...")
    ser.write(frame(image)); ser.flush()

    deadline = time.time() + 30
    while time.time() < deadline:
        line = ser.readline().decode("ascii", "ignore").strip()
        if line:
            print("  " + line)
            if "OK:" in line or "FAIL" in line:
                ser.close()
                return 0 if "OK:" in line else 1
    ser.close()
    print("ERROR: no [C542-PROG] result within timeout.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
