#!/usr/bin/env python3
"""Read the C542 logic-analyzer capture (cap_raw_main.c) over SWD and decode the
UART offline. Reports the real baud, the decoded bytes, and any Ninebot 5A A5 frames
with checksum validation.

    python decode_raw.py
"""
import os
import re
import subprocess
import sys

CLI = os.path.join(os.environ["LOCALAPPDATA"], "stm32cube", "bundles", "programmer",
                   "2.21.0", "bin", "STM32_Programmer_CLI.exe")


def read_mem(addr, size):
    out = subprocess.run([CLI, "-c", "port=SWD", "mode=HOTPLUG", "-r32",
                          hex(addr), hex(size)], capture_output=True, text=True,
                         errors="replace").stdout
    data = bytearray()
    for ln in out.splitlines():
        m = re.match(r"0x[0-9A-Fa-f]+\s*:\s*(.*)", ln)
        if not m:
            continue
        for w in m.group(1).split():
            if re.fullmatch(r"[0-9A-Fa-f]{8}", w):
                data += int(w, 16).to_bytes(4, "little")
    return bytes(data)


def u32(b, off):
    return int.from_bytes(b[off:off + 4], "little")


def decode_uart(samples, os_):
    """Resample UART (idle high, 8N1) from an oversampled bit list."""
    n = len(samples)
    out = []
    i = 0
    while i < n - 10 * os_:
        if samples[i] == 1 and samples[i + 1] == 0:        # falling edge = start
            center = (i + 1) + os_ // 2
            if samples[center] != 0:
                i += 1
                continue
            val = 0
            for k in range(8):
                c = center + (k + 1) * os_
                if samples[c]:
                    val |= (1 << k)
            out.append(val)
            i = center + 9 * os_                            # past stop bit
        else:
            i += 1
    return out


def find_frames(bs):
    """Find 5A A5 | LEN | SRC | DST | CMD | ARG | payload | CK frames, check CK."""
    frames = []
    i = 0
    while i < len(bs) - 9:
        if bs[i] == 0x5A and bs[i + 1] == 0xA5:
            ln = bs[i + 2]
            total = 2 + 5 + ln + 2
            if i + total <= len(bs):
                body = bs[i + 2:i + 2 + 5 + ln]
                ck = bs[i + 2 + 5 + ln] | (bs[i + 3 + 5 + ln] << 8)
                ok = ((sum(body) ^ 0xFFFF) & 0xFFFF) == ck
                frames.append((bs[i:i + total], ok))
                i += total
                continue
        i += 1
    return frames


def main():
    mem = read_mem(0x20000000, 0x820)
    if len(mem) < 16 or u32(mem, 0) != 0x4A700001:
        print(f"capture magic not present (got 0x{u32(mem,0):08X}) — flash cap_raw first")
        return 1
    sysclk, iv, os_ = u32(mem, 4), u32(mem, 8), u32(mem, 12)
    bits = mem[16:16 + 2048]
    samples = [(bits[i >> 3] >> (i & 7)) & 1 for i in range(len(bits) * 8)]
    print(f"capture: sysclk={sysclk} Hz, sample interval={iv} cyc, oversample={os_}x "
          f"-> {sysclk/iv:.0f} samples/s ({sysclk/(iv*os_):.0f} baud)")

    bs = decode_uart(samples, os_)
    print(f"decoded {len(bs)} UART bytes:")
    print("  " + " ".join(f"{b:02X}" for b in bs[:64]))

    frames = find_frames(bs)
    print(f"\nNinebot 5A A5 frames found: {len(frames)}")
    for f, ok in frames[:12]:
        print(f"  [{'CK ok ' if ok else 'CK BAD'}] " + " ".join(f"{b:02X}" for b in f))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
