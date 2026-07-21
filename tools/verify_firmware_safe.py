#!/usr/bin/env python3
"""Firmware-change safety gate: is it SAFE, is the UPDATE path preserved, and is
SECURE BOOT still sound? Run after **every firmware change** (see CLAUDE.md).

It chains the existing checks and maps each to a guarantee:

  SAFE (no brick)            dashboard chip sim: firmware never writes the
                             bootloader / option-byte flash, keeps a valid vector
                             table, and the 5000 ms watchdog resets+recovers.
  UPDATE PATH PRESERVED      the app image is bootloader-loadable (valid vector at
                             0x08001000) and never touches the bootloader region,
                             so the stock IAP / bootloader can always reflash it;
                             the secure bootloader still builds + verifies.
  REGRESSION                 full host test suite (protocol, bridge, BLE, …) green.
  SECURE BOOT                signed-firmware accepted, all tampering rejected,
                             both bootloader targets build.

    python tools/verify_firmware_safe.py        # full gate
    python tools/verify_firmware_safe.py --fast # skip the on-device bootloader builds
"""
from __future__ import annotations
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def py() -> str:
    venv = os.path.join(REPO, ".venv", "Scripts", "python.exe")
    return venv if os.path.exists(venv) else sys.executable


def step(title: str, cmd: list[str]) -> bool:
    print(f"\n=== {title} ===")
    rc = subprocess.run(cmd, cwd=REPO).returncode
    print(f"--- {title}: {'PASS' if rc == 0 else 'FAIL'} ---")
    return rc == 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fast", action="store_true",
                    help="skip the arm bootloader builds (crypto verify still runs)")
    args = ap.parse_args(argv)

    results = {}
    # Builds the firmware, validates its vector table (bootloader-loadable), and
    # runs the whole ctest suite incl. the no-brick dashboard_sim + ble_sim.
    results["SAFE + UPDATE-PATH + REGRESSION"] = step(
        "Build + no-brick sim + update-path + full test suite",
        [py(), os.path.join(HERE, "build_dashboard.py"), "--host-tests"])

    sb_cmd = [py(), os.path.join(HERE, "verify_secureboot.py")]
    results["SECURE BOOT"] = step("Secure-boot bootloader verification", sb_cmd)

    print("\n=========================================================")
    print(" FIRMWARE SAFETY GATE")
    print("=========================================================")
    ok = True
    for name, passed in results.items():
        print(f"  [{'PASS' if passed else 'FAIL'}] {name}")
        ok = ok and passed
    if ok:
        print(" VERDICT: SAFE to flash, UPDATE path preserved, SECURE BOOT sound.")
    else:
        print(" VERDICT: DO NOT FLASH — a guarantee failed above.")
    print("=========================================================")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
