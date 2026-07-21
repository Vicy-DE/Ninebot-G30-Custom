#!/usr/bin/env python3
"""Build and run the nRF51822 Bluetooth-firmware BLE session simulator.

Runs the real `Nrf51Firmware` on the host `SimNrf51Hardware` through a full BLE
session — advertising, phone connect, MiIO pairing, an end-to-end register-read
round-trip (phone -> nRF51 -> STM32 -> nRF51 -> phone notification) — plus the
new custom modules (mode_ctrl -> Haystack, FindMy advertisement, VESC tunnel).

A real BLE radio / Nordic SoftDevice cannot run in a chip emulator (Renode has
no nRF51 + the SoftDevice is proprietary), so this is a functional simulation at
the firmware/HAL boundary, with the SoftDevice modelled by SimSoftDevice.

    python tools/ble_sim.py
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


def _run(cmd, cwd):
    print(">", " ".join(cmd), f"(cwd={os.path.relpath(cwd, REPO)})")
    return subprocess.run(cmd, cwd=cwd).returncode


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default=os.path.join(DECOMP, "build_ble"))
    args = ap.parse_args(argv)

    if not shutil.which("cmake"):
        print("ERROR: cmake not found.", file=sys.stderr)
        return 2
    gen = ["-G", "Ninja"] if shutil.which("ninja") else []
    if _run(["cmake", "-B", args.build_dir, "-S", DECOMP] + gen, DECOMP):
        return 1
    if _run(["cmake", "--build", args.build_dir, "--target", "ble_sim"], DECOMP):
        return 1
    exe = os.path.join(args.build_dir, "ble_sim.exe")
    if not os.path.exists(exe):
        exe = os.path.join(args.build_dir, "ble_sim")
    print()
    return _run([exe], REPO)


if __name__ == "__main__":
    raise SystemExit(main())
