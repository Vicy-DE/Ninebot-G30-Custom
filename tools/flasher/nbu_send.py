#!/usr/bin/env python3
"""
nbu_send.py - NBU framed half-duplex sender for bootloader firmware updates.

Sends a signed firmware image (.sfw) to the custom secure bootloader over the
Ninebot UART bus using the **NBU** protocol (see bootloader/common/include/nbu.h).

Why not XMODEM? The dashboard cable's data line is a **single half-duplex wire**
(the Ninebot bus, 115200 8N1). On a one-wire bus the host sees its own bytes
echoed back, and XMODEM's free-running byte stream has no clean turnaround. NBU
instead is strict request->reply turn-taking over the bus's own 5A A5 framing:
the host sends exactly one command frame, then waits for the bootloader's single
ACK before sending the next. That is half-duplex-native and lets us drop/retry
per block.

Wire format (firmware-verified Ninebot frame):
    5A A5 | LEN | SRC | DST | CMD | ARG | payload[LEN] | CK_lo CK_hi
    LEN = payload byte count;  CK = (sum(LEN..payload)) ^ 0xFFFF, little-endian.

Update conversation (host 0x3F -> board, aligned with the stock IAP opcodes):
    0x07 BEGIN  payload: u32 total .sfw size (LE)   -> ACK
    0x08 DATA   payload: u16 seq (LE) + data bytes  -> ACK(seq) / NACK(expected)
    0x09 END                                         -> ACK; receiver validates+boots
    0x0A RESET                                        -> ACK; reboot into app
Reply: CMD 0x06 (ACK), ARG = status (0 = OK), payload = [acked_cmd, info_lo, info_hi].

Usage:
    python nbu_send.py --port COM3 --file firmware.sfw --target ble-stm32 [--trigger]
    python nbu_send.py --selftest          # no hardware: verify framing/parser
"""

import argparse
import os
import struct
import sys
import time

HDR1, HDR2 = 0x5A, 0xA5
CMD_BEGIN, CMD_DATA, CMD_END, CMD_RESET, CMD_ACK = 0x07, 0x08, 0x09, 0x0A, 0x06
ADDR_HOST = 0x3F                      # PC
TARGET_ADDRS = {                      # board bus addresses (see CLAUDE.md)
    "ble-stm32": 0x21,
    "bms-stm32": 0x22,
    "nrf51822": 0x21,                 # reached via the BLE STM32 relay
}

DATA_CHUNK = 128                      # data bytes per DATA frame (<= NBU_DATA_MAX)
ACK_TIMEOUT = 2.0                     # seconds to wait for an ACK
MAX_RETRIES = 8


def nbu_checksum(data: bytes) -> int:
    """Ninebot checksum: (sum(data) ^ 0xFFFF), 16-bit."""
    return (sum(data) ^ 0xFFFF) & 0xFFFF


def build_frame(src: int, dst: int, cmd: int, arg: int, payload: bytes = b"") -> bytes:
    """Build one 5A A5 frame with the verified LEN=payload convention."""
    body = bytes([len(payload), src, dst, cmd, arg]) + payload
    ck = (sum(body)) ^ 0xFFFF
    return bytes([HDR1, HDR2]) + body + struct.pack("<H", ck & 0xFFFF)


def parse_frame(buf: bytes):
    """
    Parse the first complete 5A A5 frame in buf.

    Returns (frame_dict, consumed_bytes) or (None, n) where n is the number of
    leading bytes that can be safely discarded (no frame yet).
    """
    i = buf.find(bytes([HDR1, HDR2]))
    if i < 0:
        return None, max(0, len(buf) - 1)   # keep last byte (could be a lone 5A)
    if len(buf) < i + 3:
        return None, i
    ln = buf[i + 2]
    total = 2 + 5 + ln + 2                   # hdr + (LEN SRC DST CMD ARG) + payload + CK
    if len(buf) < i + total:
        return None, i                       # wait for more bytes
    frame = buf[i:i + total]
    body = frame[2:2 + 5 + ln]
    ck = frame[2 + 5 + ln] | (frame[3 + 5 + ln] << 8)
    ok = ((sum(body)) ^ 0xFFFF) & 0xFFFF == ck
    fd = {
        "ok": ok, "len": ln, "src": body[1], "dst": body[2],
        "cmd": body[3], "arg": body[4], "payload": bytes(body[5:]),
    }
    return fd, i + total


class FrameReader:
    """Accumulates serial bytes and yields decoded ACK frames addressed to us."""

    def __init__(self, ser):
        self.ser = ser
        self.buf = bytearray()

    def wait_ack(self, board_addr: int, timeout: float):
        """Return the next ACK frame from board_addr to the host, or None on timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.ser.read(64)
            if chunk:
                self.buf += chunk
            while True:
                fd, consumed = parse_frame(bytes(self.buf))
                if fd is None:
                    if consumed:
                        del self.buf[:consumed]
                    break
                del self.buf[:consumed]
                # Ignore our own echoed TX (src == host) and anything not an ACK to us.
                if fd["ok"] and fd["cmd"] == CMD_ACK and fd["dst"] == ADDR_HOST \
                        and fd["src"] == board_addr:
                    return fd
            if not chunk:
                time.sleep(0.005)
        return None


def _send_cmd(reader, ser, board_addr, cmd, arg, payload, expect_info=None):
    """Send a frame and wait for a matching ACK, retransmitting on timeout/NACK."""
    frame = build_frame(ADDR_HOST, board_addr, cmd, arg, payload)
    for attempt in range(MAX_RETRIES):
        ser.reset_input_buffer()
        ser.write(frame)
        ser.flush()
        ack = reader.wait_ack(board_addr, ACK_TIMEOUT)
        if ack is None:
            continue                                   # timeout -> resend
        status = ack["arg"]
        acked = ack["payload"][0] if ack["payload"] else None
        info = (ack["payload"][1] | (ack["payload"][2] << 8)) \
            if len(ack["payload"]) >= 3 else None
        if acked != cmd:
            continue                                   # stale ACK -> resend
        if status == 0x00:
            return True, info
        # status 0x02 = out-of-order NACK: info is the seq the receiver expects.
        if status == 0x02 and expect_info is not None and info is not None:
            return False, info                         # caller resyncs
    return None, None


def send_trigger(ser, board_addr: int):
    """
    Ask the running application to enter update mode (set flag + reset into BL).

    The device must be running the bootloader to accept NBU; if it is in the app,
    this nudges it to reboot into the bootloader first. Uses a write to the custom
    "update trigger" register (CMD 0x02, ARG 0xF0, data 0x01).
    """
    frame = build_frame(ADDR_HOST, board_addr, 0x02, 0xF0, bytes([0x01]))
    print(f"Trigger -> 0x{board_addr:02X}: {frame.hex()}")
    ser.write(frame)
    ser.flush()
    time.sleep(2.0)                                    # let it reboot into the BL
    ser.reset_input_buffer()


def nbu_send(port, filepath, baudrate=115200, target="ble-stm32", trigger=False):
    import serial                                      # local: lets --selftest run w/o pyserial

    with open(filepath, "rb") as f:
        data = f.read()
    board_addr = TARGET_ADDRS.get(target, 0x21)
    nblocks = (len(data) + DATA_CHUNK - 1) // DATA_CHUNK

    print(f"File:   {filepath}")
    print(f"Size:   {len(data)} bytes ({nblocks} NBU blocks of {DATA_CHUNK} B)")
    print(f"Port:   {port} @ {baudrate} 8N1   target 0x{board_addr:02X} ({target})")
    print()

    ser = serial.Serial(port=port, baudrate=baudrate, bytesize=serial.EIGHTBITS,
                        parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                        timeout=0.1)
    reader = FrameReader(ser)
    try:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        if trigger:
            send_trigger(ser, board_addr)

        ok, _ = _send_cmd(reader, ser, board_addr, CMD_BEGIN, 0,
                          struct.pack("<I", len(data)))
        if not ok:
            print("ERROR: BEGIN not acknowledged. Is the device in update mode?")
            return False
        print("BEGIN acknowledged - streaming firmware...")

        start = time.time()
        seq = 0
        offset = 0
        while offset < len(data):
            chunk = data[offset:offset + DATA_CHUNK]
            payload = struct.pack("<H", seq & 0xFFFF) + chunk
            ok, info = _send_cmd(reader, ser, board_addr, CMD_DATA, 0, payload,
                                 expect_info=seq)
            if ok is None:
                print(f"\nERROR: block seq {seq} not acknowledged after retries")
                return False
            if ok is False:
                # Receiver expects a different seq (info): resync to it.
                if info is not None and info != seq:
                    seq = info
                    offset = seq * DATA_CHUNK
                    continue
            offset += len(chunk)
            seq += 1
            pct = offset / len(data) * 100
            spd = offset / (time.time() - start) if time.time() > start else 0
            sys.stdout.write(f"\r  [{seq}/{nblocks}] {pct:5.1f}%  ({spd:.0f} B/s)")
            sys.stdout.flush()
        print()

        ok, total = _send_cmd(reader, ser, board_addr, CMD_END, 0, b"")
        if not ok:
            print("ERROR: END not acknowledged")
            return False

        dt = time.time() - start
        print()
        print(f"Transfer complete: {len(data)} bytes in {dt:.1f}s "
              f"({len(data) / dt:.0f} B/s); receiver counted {total} bytes.")
        print("The bootloader now validates the .sfw header, CRC-32 and ECDSA")
        print("signature; on success it boots the new firmware, else it stays in BL.")
        return True
    finally:
        ser.close()


def _selftest() -> int:
    """Verify framing + parser round-trip without hardware (mirrors nbu.c)."""
    fails = 0

    def check(cond, msg):
        nonlocal fails
        print(f"  [{' ok ' if cond else 'FAIL'}] {msg}")
        if not cond:
            fails += 1

    # Known vector: BEGIN with size=200.
    f = build_frame(ADDR_HOST, 0x21, CMD_BEGIN, 0, struct.pack("<I", 200))
    check(f[:2] == bytes([0x5A, 0xA5]), "frame starts 5A A5")
    check(f[2] == 4 and f[3] == ADDR_HOST and f[4] == 0x21, "LEN=4, SRC=3F, DST=21")
    fd, consumed = parse_frame(f)
    check(fd is not None and fd["ok"], "self-built frame parses with valid checksum")
    check(consumed == len(f), "parser consumes exactly the frame")
    check(fd["cmd"] == CMD_BEGIN and fd["payload"] == struct.pack("<I", 200),
          "round-trip cmd + payload preserved")

    # Echo filtering: a host-origin frame in the buffer must be skipped, the
    # board's ACK after it returned.
    ack = build_frame(0x21, ADDR_HOST, CMD_ACK, 0x00, bytes([CMD_BEGIN, 0, 0]))
    stream = f + ack                       # our echo, then the real reply
    pos = 0
    found = None
    while pos < len(stream):
        fd, c = parse_frame(stream[pos:])
        if fd is None:
            break
        pos += c
        if fd["cmd"] == CMD_ACK and fd["dst"] == ADDR_HOST and fd["src"] == 0x21:
            found = fd
            break
    check(found is not None, "ACK located past our own echoed TX frame")

    # Corrupted checksum must be flagged.
    bad = bytearray(f)
    bad[-1] ^= 0xFF
    fd, _ = parse_frame(bytes(bad))
    check(fd is not None and not fd["ok"], "corrupted checksum -> ok=False")

    print(f"\n  selftest: {'PASS' if fails == 0 else 'FAIL'} ({fails} failures)")
    return 1 if fails else 0


def main():
    p = argparse.ArgumentParser(
        description="Send signed firmware (.sfw) to the bootloader via NBU "
                    "(framed half-duplex, one-wire Ninebot bus).")
    p.add_argument("--port", "-p", help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    p.add_argument("--file", "-f", help="Signed firmware file (.sfw)")
    p.add_argument("--baud", "-b", type=int, default=115200, help="Baud (default 115200)")
    p.add_argument("--target", "-t", default="ble-stm32",
                   choices=list(TARGET_ADDRS), help="Target board")
    p.add_argument("--trigger", action="store_true",
                   help="Send a Ninebot trigger so the app reboots into the bootloader")
    p.add_argument("--selftest", action="store_true",
                   help="Verify framing/parser without hardware, then exit")
    args = p.parse_args()

    if args.selftest:
        sys.exit(_selftest())

    if not args.port or not args.file:
        p.error("--port and --file are required (or use --selftest)")
    if not os.path.isfile(args.file):
        print(f"ERROR: File not found: {args.file}")
        sys.exit(1)

    ok = nbu_send(args.port, args.file, args.baud, args.target, args.trigger)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
