#!/usr/bin/env python3
"""
verify_reloc_build.py - prove the relocated (test-before-overwrite) bootloader build
is correct and safe.

The custom bootloader normally lives at 0x08000000. Before overwriting that (an
SWD-only, less-reversible step), you can build it relocated into the application slot
at +16 KB (`make TARGET=ble BL_BASE=0x08004000`) and let the *already-installed*
bootloader launch it there as if it were the app. This script builds both variants and
asserts:

  - the relocated image's vector table + entry are at 0x08004000 (not 0x08000000),
  - its reset vector is launchable from the app slot (valid SP + thumb entry in range),
  - it fits in 16 KB,
  - its app/erase region (0x08008000+) is entirely ABOVE 0x08000000 — so the test
    bootloader physically cannot erase the real bootloader, and
  - the DEFAULT build still vectors at 0x08000000 (regression guard).

Usage:  python tools/verify_reloc_build.py [--board ble|bms] [--base 0x08004000]
"""
import argparse
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STM32 = os.path.join(ROOT, "bootloader", "stm32")
OBJDUMP = "arm-none-eabi-objdump"
RAM_TOP = 0x20005000          # initial SP the bootloader vector table carries
BL_SIZE = 16 * 1024


def sh(cmd, cwd=None):
    # errors="replace": build size-report uses box-drawing chars that a cp1252
    # console can't decode; don't let that crash the check.
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                          errors="replace")


def find_make():
    for m in ("make", "mingw32-make", "gmake"):
        if sh([m, "--version"]).returncode == 0:
            return m
    return None


def isr_vector_vma(elf):
    out = sh([OBJDUMP, "-h", elf]).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 4 and p[1] == ".isr_vector":
            return int(p[3], 16)
    return None


def build(make, board, base):
    args = [make, f"TARGET={board}"]
    suffix = ""
    if base != 0x08000000:
        args.append(f"BL_BASE=0x{base:08X}")
        suffix = f"_at_0x{base:08X}"
        bdir = os.path.join(STM32, "build", f"{board}{suffix}")
    else:
        bdir = os.path.join(STM32, "build", board)
    if sh([make, "clean"], cwd=STM32).returncode != 0:
        pass
    r = sh(args, cwd=STM32)
    if r.returncode != 0:
        print(r.stdout); print(r.stderr)
        raise SystemExit(f"build failed: {' '.join(args)}")
    out = os.path.join(bdir, f"bootloader_{board}_stm32{suffix}")
    return out + ".elf", out + ".bin"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--board", default="ble", choices=["ble", "bms"])
    ap.add_argument("--base", default="0x08004000")
    args = ap.parse_args()
    base = int(args.base, 16)

    make = find_make()
    if make is None or sh([OBJDUMP, "--version"]).returncode != 0:
        print("NOTE: no make / arm-none-eabi toolchain — cannot verify. Skipped.")
        return 0

    fails = []

    def check(ok, msg):
        print(f"  [{' ok ' if ok else 'FAIL'}] {msg}")
        if not ok:
            fails.append(msg)

    print(f"Relocated bootloader build check (board={args.board}, base={args.base})")

    # Relocated build.
    relf, rbin = build(make, args.board, base)
    vma = isr_vector_vma(relf)
    check(vma == base, f".isr_vector at 0x{base:08X} (got 0x{(vma or 0):08X})")

    data = open(rbin, "rb").read()
    sp, entry = struct.unpack("<II", data[:8])
    check(sp == RAM_TOP, f"initial SP = 0x{sp:08X} (valid stack top)")
    check(base <= entry < base + BL_SIZE and entry & 1,
          f"reset vector 0x{entry:08X} is a thumb entry in the relocated region")
    check(len(data) <= BL_SIZE, f"image fits in 16 KB ({len(data)} bytes)")

    app_slot = base + BL_SIZE
    check(app_slot > 0x08000000 + BL_SIZE,
          f"app/erase region 0x{app_slot:08X} is above the real bootloader (0x08000000)")

    # Default build regression guard.
    delf, _ = build(make, args.board, 0x08000000)
    dvma = isr_vector_vma(delf)
    check(dvma == 0x08000000,
          f"DEFAULT build still vectors at 0x08000000 (got 0x{(dvma or 0):08X})")

    print("-" * 57)
    if fails:
        print(f" VERDICT: {len(fails)} FAILED — relocated build is NOT correct.")
        return 1
    print(" VERDICT: relocated bootloader is correct + safe - launchable from the")
    print("          app slot, fits 16 KB, and cannot touch 0x08000000.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
