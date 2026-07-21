#!/usr/bin/env python3
"""Interactive RE over the C542 ESC-emulator: inject any Ninebot frame onto the live
scooter bus (while the emulator keeps answering its polls so the dashboard stays
healthy) and read the dashboard's reply, all via the SWD SRAM mailbox.

Mailbox @0x20000000 (esc_faker_main.c log_t): inject_len@0x50, resp_len@0x54,
inject[28]@0x58, resp[40]@0x74.

    python esc_inject.py read 0x10 14     # READ reg 0x10 (serial), 14 bytes, from dash
    python esc_inject.py raw 5A A5 ...     # inject arbitrary bytes
"""
import os, re, subprocess, sys, time

CLI = os.path.join(os.environ["LOCALAPPDATA"], "stm32cube", "bundles", "programmer",
                   "2.21.0", "bin", "STM32_Programmer_CLI.exe")
B = 0x20000000
INJ_LEN, RESP_LEN, INJ, RESP = B + 0x50, B + 0x54, B + 0x58, B + 0x74
HERE = os.path.dirname(os.path.abspath(__file__))


def cli(*a):
    return subprocess.run([CLI, "-c", "port=SWD", "mode=HOTPLUG", *a],
                          capture_output=True, text=True, errors="replace").stdout


def w32(addr, val): cli("-w32", hex(addr), hex(val & 0xFFFFFFFF))
def wbytes(addr, data):
    p = os.path.join(HERE, "_inj.bin"); open(p, "wb").write(data + b"\x00" * ((4 - len(data) % 4) % 4))
    cli("-w", p, hex(addr))
def rbytes(addr, n):
    out = cli("-r32", hex(addr), hex((n + 3) & ~3)); d = bytearray()
    for ln in out.splitlines():
        m = re.match(r"0x[0-9A-Fa-f]+\s*:\s*(.*)", ln)
        if m:
            for w in m.group(1).split():
                if re.fullmatch(r"[0-9A-Fa-f]{8}", w): d += int(w, 16).to_bytes(4, "little")
    return bytes(d[:n])
def r32(addr): return int.from_bytes(rbytes(addr, 4), "little")


def frame(src, dst, cmd, arg, payload=b""):
    body = bytes([len(payload), src, dst, cmd, arg]) + payload
    ck = (sum(body) ^ 0xFFFF) & 0xFFFF
    return bytes([0x5A, 0xA5]) + body + bytes([ck & 0xFF, ck >> 8])


def inject(fr, timeout=4.0):
    w32(RESP_LEN, 0)                       # clear any stale reply
    wbytes(INJ, fr)
    w32(INJ_LEN, len(fr))                 # trigger (written last)
    t0 = time.time()
    while time.time() - t0 < timeout:     # wait for consume + a DST=0x3E reply
        if r32(INJ_LEN) == 0 and r32(RESP_LEN) > 0:
            break
        time.sleep(0.1)
    rl = r32(RESP_LEN)
    resp = rbytes(RESP, min(rl, 40)) if rl else b""
    return resp


def parse_frame(bs):
    if len(bs) >= 9 and bs[0] == 0x5A and bs[1] == 0xA5:
        ln = bs[2]
        if len(bs) >= 2 + 5 + ln + 2:
            body = bs[2:2 + 5 + ln]; ck = bs[2 + 5 + ln] | (bs[3 + 5 + ln] << 8)
            ok = ((sum(body) ^ 0xFFFF) & 0xFFFF) == ck
            return dict(len=ln, src=bs[3], dst=bs[4], cmd=bs[5], arg=bs[6],
                        payload=bs[7:7 + ln], ok=ok)
    return None


def main():
    if len(sys.argv) < 2: print(__doc__); return 1
    if sys.argv[1] == "read":
        reg = int(sys.argv[2], 0); n = int(sys.argv[3]) if len(sys.argv) > 3 else 14
        fr = frame(0x3E, 0x21, 0x01, reg, bytes([n]))   # App->dash READ reg, n bytes
    elif sys.argv[1] == "raw":
        fr = bytes(int(x, 16) for x in sys.argv[2:])
    else:
        print(__doc__); return 1
    print("inject:", " ".join(f"{b:02X}" for b in fr))
    resp = inject(fr)
    print("reply :", " ".join(f"{b:02X}" for b in resp) if resp else "(no reply)")
    p = parse_frame(resp)
    if p:
        print(f"  decoded: SRC {p['src']:#04x} DST {p['dst']:#04x} CMD {p['cmd']:#04x} "
              f"ARG {p['arg']:#04x} CK {'ok' if p['ok'] else 'BAD'} payload="
              + " ".join(f"{b:02X}" for b in p['payload']))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
