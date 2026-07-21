#!/usr/bin/env python3
"""Build and run the dashboard chip simulator (no-brick verification).

The simulator (`firmware/dashboard/sim`) runs the *exact* `DashApp` firmware
logic against a functional STM32F103 model (SimChip) and checks that the firmware
cannot brick the hardware: the 5000 ms watchdog fires + recovers, no boot loop,
and no writes to the bootloader/option-byte flash. Run this **before flashing**.

It also builds the target `.bin` first (if the Arm toolchain is present) so the
simulator can validate the real image's vector table.

Examples
--------
    python tools/dashboard_sim.py            # build + run, print the verdict
    python tools/dashboard_sim.py --no-target  # skip the arm-gcc target build
"""
from __future__ import annotations
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DECOMP = os.path.join(REPO, "firmware", "decompiled")
FW = os.path.join(REPO, "firmware", "dashboard")
BIN = os.path.join(FW, "build", "dashboard_app.bin")


def _run(cmd, cwd):
    print(">", " ".join(str(c) for c in cmd), f"(cwd={os.path.relpath(cwd, REPO)})")
    return subprocess.run(cmd, cwd=cwd).returncode


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-target", action="store_true",
                    help="don't build the arm-none-eabi .bin (skip the vector check)")
    ap.add_argument("--build-dir", default=os.path.join(DECOMP, "build_sim"))
    args = ap.parse_args(argv)

    # 1) Build the target .bin so the sim can validate its vector table.
    if not args.no_target and shutil.which("arm-none-eabi-g++"):
        make = next((m for m in ("make", "mingw32-make", "gmake") if shutil.which(m)), None)
        if make and _run([make], FW):
            print("WARNING: target build failed; running sim without the .bin check.",
                  file=sys.stderr)

    # 2) Configure + build the simulator (host toolchain).
    if not shutil.which("cmake"):
        print("ERROR: cmake not found.", file=sys.stderr)
        return 2
    if _run(["cmake", "-B", args.build_dir, "-S", DECOMP, "-G", "Ninja"], DECOMP):
        # Fall back to the default generator if Ninja is unavailable.
        if _run(["cmake", "-B", args.build_dir, "-S", DECOMP], DECOMP):
            return 1
    if _run(["cmake", "--build", args.build_dir, "--target", "dashboard_sim"], DECOMP):
        return 1

    exe = os.path.join(args.build_dir, "dashboard_sim.exe")
    if not os.path.exists(exe):
        exe = os.path.join(args.build_dir, "dashboard_sim")
    if not os.path.exists(exe):
        print("ERROR: dashboard_sim was not built.", file=sys.stderr)
        return 1

    # 3) Run the simulator with the real .bin path for the vector check.
    print()
    return _run([exe, BIN], REPO)


if __name__ == "__main__":
    raise SystemExit(main())
