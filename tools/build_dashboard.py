#!/usr/bin/env python3
"""Build (and sanity-check) the Ninebot G30 dashboard firmware.

Wraps the `firmware/dashboard` Makefile (arm-none-eabi toolchain) and, optionally,
the host test suite in `firmware/decompiled`. After a target build it validates
the produced `.bin` the same way the bootloader does (`platform_is_app_valid`):
the initial stack pointer must point into SRAM and the reset vector into the app
flash region — a cheap guard against a mis-linked image before you flash it.

Examples
--------
    python tools/build_dashboard.py                 # build the firmware
    python tools/build_dashboard.py --debug         # DASH_DEBUG=1 (boot banner)
    python tools/build_dashboard.py --host-tests     # also run the host suite
    python tools/build_dashboard.py --clean
"""
from __future__ import annotations
import argparse
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
FW_DIR = os.path.join(REPO, "firmware", "dashboard")
DECOMP_DIR = os.path.join(REPO, "firmware", "decompiled")

APP_BASE = 0x08001000
APP_END = 0x08001000 + 60 * 1024      # 60 KB app region (Phase 1)
SRAM_LO, SRAM_HI = 0x20000000, 0x20005000


def _find_make() -> str | None:
    for cand in ("make", "mingw32-make", "gmake"):
        if shutil.which(cand):
            return cand
    return None


def _run(cmd: list[str], cwd: str) -> int:
    print(">", " ".join(cmd), f"(cwd={os.path.relpath(cwd, REPO)})")
    return subprocess.run(cmd, cwd=cwd).returncode


def validate_bin(path: str) -> bool:
    """Check the reset vector of the linked image (SP in SRAM, reset in app)."""
    with open(path, "rb") as f:
        head = f.read(8)
    if len(head) < 8:
        print("ERROR: .bin too small", file=sys.stderr)
        return False
    sp, reset = struct.unpack("<II", head)
    ok = (SRAM_LO <= sp <= SRAM_HI) and (APP_BASE <= (reset & ~1) < APP_END)
    print(f"  vector: SP=0x{sp:08X}  reset=0x{reset:08X}  -> {'OK' if ok else 'BAD'}")
    if not ok:
        print("ERROR: vector table looks wrong — do NOT flash this image.",
              file=sys.stderr)
    return ok


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--debug", action="store_true", help="build with DASH_DEBUG=1")
    ap.add_argument("--clean", action="store_true", help="make clean first")
    ap.add_argument("--host-tests", action="store_true",
                    help="also configure+build+run the host test suite")
    args = ap.parse_args(argv)

    if not shutil.which("arm-none-eabi-g++"):
        print("ERROR: arm-none-eabi-g++ not on PATH (install the Arm GNU toolchain).",
              file=sys.stderr)
        return 2
    make = _find_make()
    if not make:
        print("ERROR: no 'make' found (need make / mingw32-make).", file=sys.stderr)
        return 2

    if args.clean and _run([make, "clean"], FW_DIR):
        return 1

    make_cmd = [make]
    if args.debug:
        make_cmd.append("DASH_DEBUG=1")
    if _run(make_cmd, FW_DIR):
        print("ERROR: firmware build failed.", file=sys.stderr)
        return 1

    binpath = os.path.join(FW_DIR, "build", "dashboard_app.bin")
    print(f"\nbuilt: {os.path.relpath(binpath, REPO)} ({os.path.getsize(binpath)} bytes)")
    if not validate_bin(binpath):
        return 1
    print("  load address: 0x%08X (Phase 1 - behind the stock 4 KB bootloader)" % APP_BASE)

    if args.host_tests:
        bdir = os.path.join(DECOMP_DIR, "build_host")
        if _run(["cmake", "-B", bdir, "-S", DECOMP_DIR, "-G", "Ninja"], DECOMP_DIR) \
           or _run(["cmake", "--build", bdir], DECOMP_DIR):
            return 1
        exe = os.path.join(bdir, "firmware_tests.exe")
        if not os.path.exists(exe):
            exe = os.path.join(bdir, "firmware_tests")
        rc = _run([exe], DECOMP_DIR)
        if rc:
            print("ERROR: host tests failed.", file=sys.stderr)
            return 1

    print("\nOK. Flash with the no-solder serial-IAP path "
          "(docs/DASHBOARD_NO_SOLDER_FLASH.md):")
    print("  python tools/flasher/ninebot_flasher.py --port COMx --target BLE "
          "--addr 0x21 --image firmware/dashboard/build/dashboard_app.bin --base 0x08001000")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
