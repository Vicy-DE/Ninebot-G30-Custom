#!/usr/bin/env python3
"""Receive a stock-bootloader dump from the bootloader-dumper firmware.

Flash `firmware/bootloader-dumper` via the stock IAP (no soldering — see
docs/DASHBOARD_NO_SOLDER_FLASH.md). It emits the 4 KB bootloader over the cable
UART (USART2, 115200) as marker-framed hex + CRC32:

    ==NBDUMP== base=08000000 len=4096 crc32=XXXXXXXX
    <8192 hex chars>
    ==NBDUMPEND==

This tool reads one complete frame (from a serial port, or from a captured text
file for sim verification), checks the CRC32, and writes the raw `.bin`.

    python tools/dump_bootloader.py --port COM5
    python tools/dump_bootloader.py --from-file capture.log --out dump.bin
"""
from __future__ import annotations
import argparse
import binascii
import os
import re
import sys
import time

HDR = re.compile(r"==NBDUMP== base=([0-9A-Fa-f]+) len=(\d+) crc32=([0-9A-Fa-f]+)")
END = "==NBDUMPEND=="
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(REPO, "boards", "ble-dashboard", "firmware",
                           "BLE_bootloader_dump.bin")


def parse_frame(text: str):
    """Extract (base, length, reported_crc, raw_bytes) from one complete frame."""
    m = HDR.search(text)
    if not m:
        return None
    base = int(m.group(1), 16)
    length = int(m.group(2))
    crc_rep = int(m.group(3), 16)
    after = text[m.end():]
    end_i = after.find(END)
    if end_i < 0:
        return None                                   # frame not complete yet
    hexbody = re.sub(r"[^0-9A-Fa-f]", "", after[:end_i])
    if len(hexbody) < length * 2:
        return None
    raw = binascii.unhexlify(hexbody[:length * 2])
    return base, length, crc_rep, raw


def read_from_serial(port: str, baud: int, timeout_s: float) -> str:
    try:
        import serial
    except ImportError:
        print("ERROR: pyserial not installed (pip install pyserial).", file=sys.stderr)
        sys.exit(2)
    ser = serial.Serial(port, baud, timeout=0.5)
    print(f"Listening on {port} @ {baud} for a bootloader dump (Ctrl-C to stop)...")
    buf = ""
    t_end = time.monotonic() + timeout_s
    while time.monotonic() < t_end:
        chunk = ser.read(512).decode("ascii", "ignore")
        if chunk:
            buf += chunk
            # keep only from the latest header so we always grab a fresh frame
            if "==NBDUMP==" in buf and END in buf.split("==NBDUMP==", 1)[1]:
                ser.close()
                return "==NBDUMP==" + buf.split("==NBDUMP==", 1)[1]
    ser.close()
    return buf


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port (e.g. COM5, /dev/ttyUSB0)")
    src.add_argument("--from-file", help="read a captured text log instead of a port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=20.0, help="serial read timeout (s)")
    ap.add_argument("--out", default=DEFAULT_OUT, help="output .bin path")
    args = ap.parse_args(argv)

    if args.from_file:
        with open(args.from_file, "r", encoding="ascii", errors="ignore") as f:
            text = f.read()
    else:
        text = read_from_serial(args.port, args.baud, args.timeout)

    frame = parse_frame(text)
    if not frame:
        print("ERROR: no complete ==NBDUMP==..==NBDUMPEND== frame found.", file=sys.stderr)
        return 1
    base, length, crc_rep, raw = frame
    crc_calc = binascii.crc32(raw) & 0xFFFFFFFF
    print(f"  base = 0x{base:08X}  len = {length}  bytes received = {len(raw)}")
    print(f"  CRC32 device = 0x{crc_rep:08X}  host = 0x{crc_calc:08X}  -> "
          f"{'MATCH' if crc_calc == crc_rep else 'MISMATCH'}")
    if crc_calc != crc_rep or len(raw) != length:
        print("ERROR: dump is corrupt (CRC/length mismatch).", file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(raw)
    print(f"  OK -> wrote {len(raw)} bytes to {os.path.relpath(args.out, REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
