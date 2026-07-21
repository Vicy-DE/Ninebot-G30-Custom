#!/usr/bin/env python3
"""Run the dashboard firmware in **Renode** (instruction-accurate emulation).

Unlike the functional simulator (`tools/dashboard_sim.py`, which runs the app
logic through the HAL), this loads the real compiled `dashboard_app.elf` and
executes the ARM Thumb opcodes on Renode's emulated STM32F103 (Cortex-M3) with a
modeled IWDG. It proves the watchdog behaviour on the actual binary:

  * healthy  → exactly 1 `[BOOT]` banner on USART2 (the IWDG is kept fed),
  * fault    → the 5000 ms IWDG resets the MCU every ~5 s (multiple `[BOOT]`s).

Requires: Renode (winget `Renode.Renode`), the arm-none-eabi toolchain, and make.
The firmware is (re)built with DASH_DEBUG=1 so it prints the [BOOT] banner.

    python tools/renode_dashboard.py
"""
from __future__ import annotations
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
FW = os.path.join(REPO, "firmware", "dashboard")
RENODE_DIR = os.path.join(FW, "sim", "renode")
BIN = os.path.join(FW, "build", "dashboard_app.bin")


def find_renode() -> str | None:
    for cand in ("renode", "Renode", "renode.exe", "Renode.exe"):
        p = shutil.which(cand)
        if p:
            return p
    for p in (r"C:\Program Files\Renode\bin\Renode.exe",
              r"C:\Program Files (x86)\Renode\bin\Renode.exe"):
        if os.path.exists(p):
            return p
    return None


def build_debug_firmware() -> bool:
    make = next((m for m in ("make", "mingw32-make", "gmake") if shutil.which(m)), None)
    if not (make and shutil.which("arm-none-eabi-g++")):
        print("WARNING: arm toolchain/make missing — using the existing .bin if present.",
              file=sys.stderr)
        return os.path.exists(BIN)
    subprocess.run([make, "clean"], cwd=FW, stdout=subprocess.DEVNULL)
    rc = subprocess.run([make, "DASH_DEBUG=1"], cwd=FW).returncode
    return rc == 0 and os.path.exists(BIN)


def read_vector() -> tuple[int, int]:
    with open(BIN, "rb") as f:
        sp, reset = struct.unpack("<II", f.read(8))
    return sp, reset


def run_scenario(renode: str, script: str, log: str, sp: int, reset: int,
                 timeout_s: int) -> int:
    """Run one .resc headless; return the number of [BOOT] banners on USART2."""
    if os.path.exists(log):
        os.remove(log)
    cmd = [renode, "--disable-xwt", "--console", "-P", "0",
           "-e", f"$sp={sp:#010x}", "-e", f"$resetpc={reset:#010x}",
           "-e", f"i @{script.replace(os.sep, '/')}"]
    print(f"> renode {os.path.basename(script)} (SP={sp:#010x} reset={reset:#010x}) ...")
    t0 = time.time()
    try:
        subprocess.run(cmd, cwd=os.path.dirname(renode), timeout=timeout_s,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        print("  (renode hit the timeout; reading whatever was captured)")
    dt = time.time() - t0
    boots = 0
    if os.path.exists(log):
        with open(log, "rb") as f:
            boots = f.read().count(b"[BOOT]")
    print(f"  {boots} [BOOT] banner(s) captured in {dt:.0f}s real")
    return boots


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--timeout", type=int, default=180, help="per-scenario real-time cap (s)")
    ap.add_argument("--no-build", action="store_true", help="don't rebuild the firmware")
    args = ap.parse_args(argv)

    renode = find_renode()
    if not renode:
        print("ERROR: Renode not found. Install with:  winget install Renode.Renode",
              file=sys.stderr)
        return 2
    print(f"Renode: {renode}")

    if not args.no_build and not build_debug_firmware():
        print("ERROR: could not build the DASH_DEBUG firmware.", file=sys.stderr)
        return 1
    if not os.path.exists(BIN):
        print(f"ERROR: {BIN} not found (build the firmware first).", file=sys.stderr)
        return 1
    sp, reset = read_vector()

    healthy = run_scenario(renode, os.path.join(RENODE_DIR, "dashboard_healthy.resc"),
                           os.path.join(RENODE_DIR, "usart2_healthy.log"), sp, reset,
                           args.timeout)
    fault = run_scenario(renode, os.path.join(RENODE_DIR, "dashboard_watchdog.resc"),
                         os.path.join(RENODE_DIR, "usart2_watchdog.log"), sp, reset,
                         args.timeout)

    print("-" * 56)
    ok_h = healthy == 1
    ok_f = fault >= 2
    print(f"  healthy run   : {healthy} boot  -> {'PASS' if ok_h else 'FAIL'} "
          f"(expect 1 - watchdog stays fed)")
    print(f"  watchdog run  : {fault} boots -> {'PASS' if ok_f else 'FAIL'} "
          f"(expect >=2 - IWDG resets the MCU ~every 5 s)")
    if ok_h and ok_f:
        print("  VERDICT: the REAL .elf's 5000 ms IWDG works under Renode "
              "(fed when healthy, resets+recovers on a missing subsystem).")
        return 0
    print("  VERDICT: unexpected result — inspect the usart2_*.log files.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
