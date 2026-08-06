#!/usr/bin/env python3
"""
nrf51_dump_receive.py — receive a flash dump from the on-chip nRF51 dumper.

PC side of `firmware/nrf51-dumper` (the port of the deleted STM32 bootloader-dumper). The dumper
runs in the dashboard's application slot and streams the whole flash out over the Ninebot bus in
`5A A5` frames; this reassembles it.

    python nrf51_dump_receive.py --port COM9 --out dump.bin
    python nrf51_dump_receive.py --selftest        # no hardware needed

Frames (SRC 0x21 dashboard -> DST 0x3F PC):
    CMD 0x70  info   arg = 1 when region 0 is readback protected
                     payload = deviceid0, deviceid1, flash_size, clenr0, bootaddr, rbpconf (LE32)
    CMD 0x71  data   arg = 1 when this chunk was unreadable (protected)
                     payload = addr (LE32) + 64 bytes

> Prefer SWD (`nrf51_swd.py dump`) when you can: it needs no app slot, so it does not overwrite
> the stock application, and it can read region 0 whenever the debugger is allowed to.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time

CHUNK = 64
ADDR_DASH = 0x21
ADDR_PC = 0x3F
CMD_INFO = 0x70
CMD_DATA = 0x71


def checksum(body: bytes) -> int:
    """(~sum(LEN..payload)) & 0xFFFF — the firmware-confirmed Ninebot checksum."""
    return (~sum(body)) & 0xFFFF


class Parser:
    """Incremental `5A A5` frame parser; resynchronises on bad preamble/checksum."""

    def __init__(self) -> None:
        self.buf = bytearray()
        self.bad = 0

    def feed(self, data: bytes):
        """Yield (cmd, arg, payload) for each valid frame in the stream."""
        self.buf.extend(data)
        while True:
            # resync to the preamble
            while len(self.buf) >= 2 and not (self.buf[0] == 0x5A and self.buf[1] == 0xA5):
                del self.buf[0]
            if len(self.buf) < 3:
                return
            ln = self.buf[2]
            total = 3 + 4 + ln + 2
            if len(self.buf) < total:
                return
            frame = bytes(self.buf[:total])
            body = frame[2:2 + 5 + ln]            # LEN SRC DST CMD ARG payload
            got = frame[-2] | (frame[-1] << 8)
            if checksum(body) != got:
                self.bad += 1
                del self.buf[0]
                continue
            src, dst, cmd, arg = frame[3], frame[4], frame[5], frame[6]
            payload = frame[7:7 + ln]
            del self.buf[:total]
            if src == ADDR_DASH and dst == ADDR_PC:
                yield cmd, arg, payload


def build_frame(cmd: int, arg: int, payload: bytes) -> bytes:
    """Build a dashboard->PC frame (used by the self-test to fake the device)."""
    body = bytes([len(payload), ADDR_DASH, ADDR_PC, cmd, arg]) + payload
    ck = checksum(body)
    return b"\x5A\xA5" + body + bytes([ck & 0xFF, ck >> 8])


def selftest() -> int:
    """Round-trip the framing + reassembly without hardware."""
    fails = 0

    def check(ok: bool, what: str):
        nonlocal fails
        print(f"  {' ok ' if ok else 'FAIL'} {what}")
        if not ok:
            fails += 1

    # info frame
    info = struct.pack("<6I", 0x12345678, 0x9ABCDEF0, 0x40000, 0x18000, 0x3C000, 0xFFFFFFFF)
    p = Parser()
    out = list(p.feed(build_frame(CMD_INFO, 0, info)))
    check(len(out) == 1 and out[0][0] == CMD_INFO, "info frame parses")
    check(struct.unpack("<6I", out[0][2])[2] == 0x40000, "  flash size decoded (256 KB)")

    # data frames reassemble into an image
    p = Parser()
    image = bytes((i * 7) & 0xFF for i in range(CHUNK * 4))
    stream = b""
    for i in range(4):
        addr = i * CHUNK
        stream += build_frame(CMD_DATA, 0,
                              struct.pack("<I", addr) + image[addr:addr + CHUNK])
    got = bytearray(len(image))
    seen = set()
    for cmd, arg, payload in p.feed(stream):
        if cmd == CMD_DATA:
            (a,) = struct.unpack_from("<I", payload, 0)
            got[a:a + CHUNK] = payload[4:]
            seen.add(a)
    check(len(seen) == 4, "four data frames parsed")
    check(bytes(got) == image, "reassembled image matches byte-for-byte")

    # protected chunk is flagged, not silently wrong
    p = Parser()
    out = list(p.feed(build_frame(CMD_DATA, 1, struct.pack("<I", 0) + b"\xFF" * CHUNK)))
    check(len(out) == 1 and out[0][1] == 1, "protected chunk carries arg=1")

    # corruption is rejected and the stream resynchronises
    p = Parser()
    good = build_frame(CMD_DATA, 0, struct.pack("<I", 0) + b"\x00" * CHUNK)
    bad = bytearray(good)
    bad[-1] ^= 0xFF
    out = list(p.feed(bytes(bad) + good))
    check(len(out) == 1 and p.bad == 1, "corrupt frame rejected, next frame recovered")

    # leading garbage
    p = Parser()
    out = list(p.feed(b"\x00\xFF\x5A\x5A" + good))
    check(len(out) == 1, "recovers after leading garbage")

    print(f"\n{'PASSED' if not fails else 'FAILED'} ({fails} failure(s))")
    return 1 if fails else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port on the Ninebot bus (e.g. COM9)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="nrf51_dump.bin")
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0x40000)
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.port:
        ap.error("--port is required (or use --selftest)")

    try:
        import serial       # pyserial
    except ImportError:
        sys.exit("pyserial not installed: pip install pyserial")

    image = bytearray(b"\xFF" * args.size)
    have = bytearray(args.size // CHUNK)
    protected = set()
    parser = Parser()
    info_shown = False

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        print(f"[*] listening on {args.port} @{args.baud} ...")
        deadline = time.time() + args.timeout
        while time.time() < deadline:
            data = ser.read(4096)
            if not data:
                continue
            for cmd, arg, payload in parser.feed(data):
                if cmd == CMD_INFO and not info_shown and len(payload) >= 24:
                    d0, d1, size, cl, bl, rbp = struct.unpack("<6I", payload[:24])
                    print(f"[+] device id : {d1:08X}{d0:08X}")
                    print(f"[+] flash     : {size // 1024} KB")
                    print(f"[+] CLENR0    : 0x{cl:08X}  (SoftDevice region)")
                    print(f"[+] BOOTADDR  : 0x{bl:08X}")
                    print(f"[+] RBPCONF   : 0x{rbp:08X}"
                          + ("   region 0 PROTECTED" if arg else "   region 0 readable"))
                    info_shown = True
                elif cmd == CMD_DATA and len(payload) >= 4 + CHUNK:
                    (a,) = struct.unpack_from("<I", payload, 0)
                    idx = a // CHUNK
                    if idx < len(have):
                        image[a:a + CHUNK] = payload[4:4 + CHUNK]
                        have[idx] = 1
                        if arg:
                            protected.add(a)
                        done = sum(have)
                        if done % 256 == 0:
                            print(f"    {done * CHUNK // 1024} KB / {args.size // 1024} KB")
            if all(have):
                break

    missing = len(have) - sum(have)
    with open(args.out, "wb") as f:
        f.write(image)
    print(f"[+] wrote {args.out} ({len(image)} bytes)")
    if protected:
        lo, hi = min(protected), max(protected) + CHUNK
        print(f"[!] 0x{lo:05X}..0x{hi:05X} was readback protected — those bytes are 0xFF filler,")
        print("    NOT the real SoftDevice. Do not restore this image over a working stack.")
    if missing:
        print(f"[!] {missing} chunk(s) never arrived — dump is INCOMPLETE, do not treat as a backup")
        return 1
    if parser.bad:
        print(f"[i] {parser.bad} corrupt frame(s) were discarded and retried")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
