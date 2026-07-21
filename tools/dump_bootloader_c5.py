#!/usr/bin/env python3
"""Dump the dashboard bootloader through the NUCLEO-C542RC plug-tap bridge.

The C542RC firmware (`firmware/dash-tap-c542/`) taps the two wires of the
dashboard's internal plug on Arduino pins A0=PA0 and A2=PA4, prints a `[FIND]`
report saying which wire carries the Ninebot traffic, then transparently bridges
that wire to the ST-LINK USB Virtual COM Port. This tool:

  1. reads + shows the `[FIND]` report (which wire = BT/nRF vs dashboard),
  2. then captures the `==NBDUMP==` bootloader frame emitted by the on-target
     dumper app and writes the raw `.bin` (CRC-checked) — reusing
     `tools/dump_bootloader.py`'s frame parser.

    python tools/dump_bootloader_c5.py --port COM7          # live, via the C542 VCP
    python tools/dump_bootloader_c5.py --selftest           # no hardware: verify parsing

Live capture still needs the on-target dumper app flashed (see dump_bootloader.py /
docs/DASHBOARD_DUMP_C542.md). The C542 is the transport; it does not by itself read
the target's flash.
"""
from __future__ import annotations
import argparse
import binascii
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dump_bootloader as dbl                      # reuse parse_frame + DEFAULT_OUT

FIND_LINE = re.compile(
    r"\[FIND\]\s+(A\d/PA\d):\s+(\d+)\s+bytes,\s+(\d+)\s+frames\s+\((\d+)\s+bad\)\s+[-—]\s+(.*)")
BRIDGE_LINE = re.compile(r"\[FIND\]\s+bridging\s+(A\d/PA\d)")


def parse_find_report(text: str):
    """Return (lines, bridged) where lines = [(pin, bytes, frames, bad, role)]."""
    lines = [(m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)),
              m.group(5).strip()) for m in FIND_LINE.finditer(text)]
    bm = BRIDGE_LINE.search(text)
    return lines, (bm.group(1) if bm else None)


def show_find(text: str) -> None:
    lines, bridged = parse_find_report(text)
    if not lines:
        print("  (no [FIND] report seen yet)")
        return
    print("  Wire-finder report:")
    for pin, nbytes, frames, bad, role in lines:
        print(f"    {pin}: {nbytes} B, {frames} frame(s), {bad} bad — {role}")
    print(f"  -> bridging {bridged or '(none: no Ninebot traffic found)'}")


def _selftest() -> int:
    fails = 0

    def check(ok, msg):
        nonlocal fails
        print(f"  [{' ok ' if ok else 'FAIL'}] {msg}")
        if not ok:
            fails += 1

    # Synthetic C542 output: a FIND report then a small ==NBDUMP== frame.
    fw = bytes(range(8))
    crc = binascii.crc32(fw) & 0xFFFFFFFF
    stream = (
        "[C542-TAP] dashboard-plug tap ready (A0=PA0, A2=PA4 @115200).\r\n"
        "[FIND] A0/PA0: 320 bytes, 5 frames (0 bad) - transmits SRC 0x21 -> BLE/nRF (BT) side\r\n"
        "[FIND] A2/PA4: 8 bytes, 0 frames (0 bad) - active but no Ninebot frames\r\n"
        "[FIND] bridging A0/PA0 (most Ninebot frames).\r\n"
        f"==NBDUMP== base=08000000 len=8 crc32={crc:08X}\r\n"
        + fw.hex() + "\r\n==NBDUMPEND==\r\n"
    )

    lines, bridged = parse_find_report(stream)
    check(len(lines) == 2, "parsed both [FIND] line reports")
    check(bridged == "A0/PA0", "identified A0/PA0 as the bridged wire")
    check(any("BT" in role for *_, role in lines), "named the BT/nRF side from SRC 0x21")

    frame = dbl.parse_frame(stream)
    check(frame is not None, "extracted the ==NBDUMP== frame through the bridge text")
    if frame:
        base, length, crc_rep, raw = frame
        check(raw == fw and length == 8, "dump bytes reconstructed (8 B)")
        check((binascii.crc32(raw) & 0xFFFFFFFF) == crc_rep, "dump CRC32 matches")

    print(f"\n  selftest: {'PASS' if fails == 0 else 'FAIL'} ({fails} failures)")
    return 1 if fails else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="C542RC ST-LINK VCP serial port (e.g. COM7)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=25.0, help="capture window (s)")
    ap.add_argument("--out", default=dbl.DEFAULT_OUT, help="output .bin path")
    ap.add_argument("--selftest", action="store_true",
                    help="verify report+dump parsing without hardware")
    args = ap.parse_args(argv)

    if args.selftest:
        return _selftest()
    if not args.port:
        ap.error("--port is required (or use --selftest)")

    try:
        import serial
    except ImportError:
        print("ERROR: pyserial not installed (pip install pyserial).", file=sys.stderr)
        return 2

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    print(f"Listening on {args.port} @ {args.baud} via the C542 tap "
          f"(Ctrl-C to stop)...")
    buf = ""
    shown = False
    t_end = time.monotonic() + args.timeout
    while time.monotonic() < t_end:
        chunk = ser.read(512).decode("ascii", "ignore")
        if not chunk:
            continue
        buf += chunk
        if not shown and "[FIND] bridging" in buf:
            show_find(buf)
            shown = True
        if "==NBDUMP==" in buf and dbl.END in buf.split("==NBDUMP==", 1)[1]:
            break
    ser.close()

    frame = dbl.parse_frame(buf)
    if not frame:
        if not shown:
            show_find(buf)
        print("ERROR: no complete ==NBDUMP==..==NBDUMPEND== frame found. "
              "Is the on-target dumper app flashed and the right wire bridged?",
              file=sys.stderr)
        return 1
    base, length, crc_rep, raw = frame
    crc_calc = binascii.crc32(raw) & 0xFFFFFFFF
    print(f"  base = 0x{base:08X}  len = {length}  received = {len(raw)}")
    print(f"  CRC32 device = 0x{crc_rep:08X}  host = 0x{crc_calc:08X}  -> "
          f"{'MATCH' if crc_calc == crc_rep else 'MISMATCH'}")
    if crc_calc != crc_rep or len(raw) != length:
        print("ERROR: dump is corrupt (CRC/length mismatch).", file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(raw)
    print(f"  OK -> wrote {len(raw)} bytes to {os.path.relpath(args.out, dbl.REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
