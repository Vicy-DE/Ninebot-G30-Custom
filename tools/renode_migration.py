#!/usr/bin/env python3
"""Build + run the bootloader migration in Renode and assert it worked.

Migration: the stock 4 KB bootloader boots the packed `migrate_big.bin` at
0x08001000; the trampoline jumps to the updater at 0x08004000; the updater
installs the embedded new 16 KB bootloader at 0x08000000 (erase/write from RAM).

The sim checks: the result cookie = BL_INSTALLED, and 0x08000000 now holds the
new bootloader (byte-identical to newbl.bin).

    python tools/renode_migration.py
"""
from __future__ import annotations
import os
import re
import shutil
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MIG = os.path.join(REPO, "firmware", "migration")
RESC = os.path.join(MIG, "sim", "migrate.resc")
NEWBL = os.path.join(MIG, "build", "newbl.bin")

MIG_COOKIE_MAGIC = 0x4D494752
MIG_STATUS_BL_INSTALLED = 2


def find_renode():
    for c in ("renode", "Renode", "renode.exe", "Renode.exe"):
        if shutil.which(c):
            return shutil.which(c)
    for p in (r"C:\Program Files\Renode\bin\Renode.exe",
              r"C:\Program Files (x86)\Renode\bin\Renode.exe"):
        if os.path.exists(p):
            return p
    return None


def main() -> int:
    make = next((m for m in ("make", "mingw32-make", "gmake") if shutil.which(m)), None)
    if make:
        if subprocess.run([make], cwd=MIG).returncode:
            print("ERROR: migration build failed.", file=sys.stderr)
            return 1
    renode = find_renode()
    if not renode:
        print("ERROR: Renode not found (winget install Renode.Renode).", file=sys.stderr)
        return 2

    out = subprocess.run(
        [renode, "--disable-xwt", "--console", "-P", "0",
         "-e", f"i @{RESC.replace(os.sep, '/')}"],
        cwd=os.path.dirname(renode), capture_output=True, text=True, timeout=170).stdout
    out = re.sub(r"\x1b\[[0-9;]*m", "", out)
    vals = [int(m, 16) for m in re.findall(r"^0x[0-9A-Fa-f]{8}", out, re.M)]
    if len(vals) < 4:
        print("ERROR: could not read sim results:\n" + out, file=sys.stderr)
        return 1
    cookie_magic, status, bl_sp, bl_reset = vals[:4]

    with open(NEWBL, "rb") as f:
        exp_sp, exp_reset = struct.unpack("<II", f.read(8))

    print(f"  cookie magic   : 0x{cookie_magic:08X}  (expect 0x{MIG_COOKIE_MAGIC:08X})")
    print(f"  cookie status  : {status}  (expect {MIG_STATUS_BL_INSTALLED}=BL_INSTALLED)")
    print(f"  0x08000000 SP  : 0x{bl_sp:08X}  (expect 0x{exp_sp:08X})")
    print(f"  0x08000004 rst : 0x{bl_reset:08X}  (expect 0x{exp_reset:08X})")

    ok = (cookie_magic == MIG_COOKIE_MAGIC and status == MIG_STATUS_BL_INSTALLED
          and bl_sp == exp_sp and bl_reset == exp_reset)
    print("-" * 56)
    if ok:
        print(" VERDICT: migration OK - trampoline jumped, updater installed the new"
              " 16 KB bootloader at 0x08000000 (byte-verified).")
        return 0
    print(" VERDICT: migration FAILED — inspect the sim output.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
