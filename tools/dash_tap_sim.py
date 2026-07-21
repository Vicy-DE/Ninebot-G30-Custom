#!/usr/bin/env python3
"""Verify the NUCLEO-C542RC dashboard-plug tap logic without hardware.

Builds + runs the wire-finder C logic test (firmware/dash-tap-c542) and the PC
dump-tool parser selftest, then reports a combined verdict. This is the bench-free
half of the C542 tap: the classification + report/dump parsing are proven here; the
HAL board glue (main.c) is built separately in STM32CubeIDE for the actual board.

    python tools/dash_tap_sim.py
"""
from __future__ import annotations
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(REPO, "firmware", "dash-tap-c542")
SIM = os.path.join(FW, "sim")


def run(cmd, cwd=None):
    print("  >", " ".join(cmd))
    return subprocess.run(cmd, cwd=cwd).returncode


def main() -> int:
    gcc = shutil.which("gcc")
    gpp = shutil.which("g++")
    py = sys.executable
    if not gcc or not gpp:
        print("NOTE: gcc/g++ not found — skipping the C logic test.")
        return 0

    print("== wire_finder C logic test ==")
    obj = os.path.join(SIM, "wire_finder.o")
    exe = os.path.join(SIM, "test_wire_finder.exe")
    if run([gcc, "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", FW, "-c", os.path.join(FW, "wire_finder.c"), "-o", obj]):
        print("FAIL: wire_finder.c did not compile"); return 1
    if run([gpp, "-O2", "-std=c++17", "-I", FW,
            os.path.join(SIM, "test_wire_finder.cpp"), obj, "-o", exe]):
        print("FAIL: test did not compile"); return 1
    rc_c = run([exe])

    print("\n== PC dump-tool parser selftest ==")
    rc_py = run([py, os.path.join(REPO, "tools", "dump_bootloader_c5.py"), "--selftest"])

    for f in (obj, exe):
        try:
            os.remove(f)
        except OSError:
            pass

    print("\n" + "=" * 57)
    if rc_c == 0 and rc_py == 0:
        print(" VERDICT: C542 tap logic verified - wire-finder classifies the two")
        print("          plug wires and the dump parses end-to-end through the bridge.")
        return 0
    print(" VERDICT: FAILED — see output above.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
