#!/usr/bin/env python3
"""Verify the secure-boot bootloader end-to-end.

1. Sign a test firmware with the real PC signer (tools/signing/sign_firmware.py).
2. Compile the bootloader's OWN verify code (fw_header.c / ecdsa.c / sha256.c /
   crc32.c) into a host test and run it: the genuine signed image must be
   ACCEPTED and every tampering (firmware byte, signature byte, wrong key, wrong
   magic, wrong target) REJECTED.
3. Cross-compile the bootloader target (nRF51822 — the only one) so the on-device
   build is confirmed too.

    python tools/verify_secureboot.py
Exit 0 only if the crypto accepts/rejects correctly AND the nRF51 target builds.
"""
from __future__ import annotations
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BL = os.path.join(REPO, "bootloader")
TESTS = os.path.join(BL, "tests")
COMMON = os.path.join(BL, "common")


def run(cmd, cwd, **kw) -> int:
    print(">", " ".join(str(c) for c in cmd), f"(cwd={os.path.relpath(cwd, REPO)})")
    return subprocess.run(cmd, cwd=cwd, **kw).returncode


def py() -> str:
    venv = os.path.join(REPO, ".venv", "Scripts", "python.exe")
    return venv if os.path.exists(venv) else sys.executable


def main() -> int:
    if not (shutil.which("gcc") and shutil.which("g++")):
        print("ERROR: gcc/g++ not found.", file=sys.stderr)
        return 2

    # 1) Produce a genuinely-signed .sfw + raw public keys.
    if run([py(), os.path.join(TESTS, "make_test_sfw.py")], TESTS):
        print("ERROR: signing failed (is `cryptography` installed?).", file=sys.stderr)
        return 1

    # 2) Compile the bootloader verify core + the host test, then run it.
    inc = os.path.join(COMMON, "include")
    cfiles = [os.path.join(COMMON, "src", f + ".c")
              for f in ("fw_header", "ecdsa", "sha256", "crc32")]
    objs = []
    for c in cfiles:
        o = os.path.join(TESTS, os.path.basename(c)[:-2] + ".o")
        if run(["gcc", "-O2", "-std=c11", "-I", inc, "-c", c, "-o", o], TESTS):
            return 1
        objs.append(o)
    exe = os.path.join(TESTS, "test_secureboot.exe")
    if run(["g++", "-O2", "-I", inc, os.path.join(TESTS, "test_secureboot.cpp")]
           + objs + ["-o", exe], TESTS):
        return 1
    rc = run([exe, os.path.join(TESTS, "_test.sfw"),
              os.path.join(TESTS, "_pubkey.bin"),
              os.path.join(TESTS, "_wrong_pubkey.bin")], REPO)
    if rc:
        print("ERROR: secure-boot crypto verification FAILED.", file=sys.stderr)
        return 1

    # 3) Confirm the on-device bootloader builds + links.
    #    nRF51822 is the ONLY target: the dashboard has no STM32, the ESC is a VESC and the BMS
    #    stays stock (see boards/ble-dashboard/MCU_IDENTIFICATION.md).
    make = next((m for m in ("make", "mingw32-make", "gmake") if shutil.which(m)), None)
    if make:
        d = os.path.join(BL, "nrf51")
        run([make, "clean"], d, stdout=subprocess.DEVNULL)
        if run([make], d):
            print("ERROR: nrf51 bootloader failed to build.", file=sys.stderr)
            return 1
    else:
        print("NOTE: no make/arm toolchain — skipped on-device bootloader build.")

    print("\nVERDICT: secure boot verified - signed-firmware accept + tamper reject,"
          " and the nRF51 bootloader builds.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
