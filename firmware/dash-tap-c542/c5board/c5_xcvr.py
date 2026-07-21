#!/usr/bin/env python3
"""Drive the C542 SWD half-duplex transceiver (xcvr_main.c) from the PC over SWD.

Writes a frame into the SRAM mailbox, triggers the C542 to transmit it on PA4 and
capture the response, then reads back: the per-bit line read-back (echo, proving TX
drove the wire) and the dashboard's reply (rx). No UART/VCP needed.

    python c5_xcvr.py txtest        # send a benign read-query, verify TX via echo
    python c5_xcvr.py raw 5A A5 ...  # send arbitrary bytes
"""
import os
import re
import subprocess
import sys
import time

CLI = os.path.join(os.environ["LOCALAPPDATA"], "stm32cube", "bundles", "programmer",
                   "2.21.0", "bin", "STM32_Programmer_CLI.exe")
BASE = 0x20000000
OFF_TX, OFF_ECHO, OFF_RX = 0x20, 0xE0, 0x1D1     # mailbox field offsets


def _cli(*args):
    return subprocess.run([CLI, "-c", "port=SWD", "mode=HOTPLUG", *args],
                          capture_output=True, text=True, errors="replace").stdout


def w32(addr, val):
    _cli("-w32", hex(addr), hex(val & 0xFFFFFFFF))


def wbytes(addr, data):
    tmp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_tx.bin")
    open(tmp, "wb").write(data + b"\x00" * ((4 - len(data) % 4) % 4))
    _cli("-w", tmp, hex(addr))


def rbytes(addr, size):
    out = _cli("-r32", hex(addr), hex((size + 3) & ~3))
    data = bytearray()
    for ln in out.splitlines():
        m = re.match(r"0x[0-9A-Fa-f]+\s*:\s*(.*)", ln)
        if m:
            for w in m.group(1).split():
                if re.fullmatch(r"[0-9A-Fa-f]{8}", w):
                    data += int(w, 16).to_bytes(4, "little")
    return bytes(data[:size])


def r32(addr):
    return int.from_bytes(rbytes(addr, 4), "little")


def frame(src, dst, cmd, arg, payload=b""):
    body = bytes([len(payload), src, dst, cmd, arg]) + payload
    ck = (sum(body) ^ 0xFFFF) & 0xFFFF
    return bytes([0x5A, 0xA5]) + body + bytes([ck & 0xFF, ck >> 8])


def decode_uart_bits(echo: bytes, nbits: int):
    """Decode the per-transmitted-bit read-back (10 bits/byte) back into bytes."""
    s = [(echo[i >> 3] >> (i & 7)) & 1 for i in range(nbits)]
    out = []
    i = 0
    while i + 10 <= len(s):
        if s[i] != 0:            # expect start bit (0)
            i += 1
            continue
        b = 0
        for k in range(8):
            if s[i + 1 + k]:
                b |= (1 << k)
        out.append(b)
        i += 10
    return bytes(out)


def xfer(tx: bytes, rx_win_ms=80, timeout_s=8):
    wbytes(BASE + OFF_TX, tx)
    w32(BASE + 0x04, len(tx))         # tx_len
    w32(BASE + 0x08, rx_win_ms)       # rx_win_ms
    w32(BASE + 0x0C, 0)               # clear magic_out
    w32(BASE + 0x00, 0x60001234)      # magic_in -> trigger
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        if r32(BASE + 0x0C) == 0x600D0DEF:
            break
        time.sleep(0.1)
    rx_len = r32(BASE + 0x10)
    echo_bits = r32(BASE + 0x14)
    rx = rbytes(BASE + OFF_RX, min(rx_len, 320))
    echo = rbytes(BASE + OFF_ECHO, (echo_bits + 7) // 8)
    return rx, decode_uart_bits(echo, echo_bits)


def main():
    if len(sys.argv) < 2:
        print(__doc__); return 1
    if sys.argv[1] == "txtest":
        tx = frame(0x3F, 0x21, 0x01, 0x10, bytes([0x02]))   # PC->dash read reg 0x10, 2 bytes
    elif sys.argv[1] == "raw":
        tx = bytes(int(x, 16) for x in sys.argv[2:])
    else:
        print(__doc__); return 1

    print("TX  :", " ".join(f"{b:02X}" for b in tx))
    rx, echo = xfer(tx)
    print("echo:", " ".join(f"{b:02X}" for b in echo), "  (line read-back of our TX)")
    print("  TX verified on the wire:" , "YES" if echo == tx else "NO (collision/timing?)")
    print("RX  :", " ".join(f"{b:02X}" for b in rx) if rx else "(no response)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
