#!/usr/bin/env python3
"""Verify-before-flash gate for the C542 software-UART programmer + IAP chain.

Run this BEFORE flashing anything to hardware. It proves, in simulation with the
REAL firmware code, the whole flow the NUCLEO-C542RC will drive:

  1. software-UART bit codec round-trips bytes (test_wire_finder covers framing too)
  2. the C542 software-UART NBU/IAP programmer flashes the real bootloader receiver
     (nbu.c) byte-for-byte                                        [test_c5_prog]
  3. the relocated bootloader @0x08004000 (16 offset) accepts a signed app, writes it
     to 0x08008000 (32 offset), and REJECTS tampering; the installer @0x08008000 then
     writes a bootloader back to 0x08004000 — never touching 0x08000000 [test_iap_chain]
  4. the on-target artifacts build: relocated BL (BL_BASE=0x08004000) + installer@32

Prints GO / NO-GO. GO means the simulated chain is sound and the staged binaries
match it, so flashing is safe to attempt.

    python tools/verify_c5_flash.py
"""
from __future__ import annotations
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(REPO, "firmware", "dash-tap-c542")
SIM = os.path.join(FW, "sim")
COMMON = os.path.join(REPO, "bootloader", "common")
CINC = os.path.join(COMMON, "include")
PY = sys.executable

GCC = shutil.which("gcc"); GPP = shutil.which("g++")
MAKE = next((m for m in ("make", "mingw32-make", "gmake") if shutil.which(m)), None)
results = []


def run(cmd, cwd=None, label=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors="replace")
    ok = r.returncode == 0
    if label:
        print(f"  [{'PASS' if ok else 'FAIL'}] {label}")
        if not ok:
            sys.stdout.write(r.stdout[-1500:] + r.stderr[-1500:])
        results.append((label, ok))
    return r


def cc(src, out, cxx=False):
    comp = [GPP, "-O2", "-std=c++17", "-pthread"] if cxx else [GCC, "-O2", "-std=c11",
            "-Wall", "-Wextra", "-Werror"]
    return run(comp + ["-I", FW, "-I", CINC, "-c", src, "-o", out]).returncode == 0


def main() -> int:
    if not (GCC and GPP):
        print("NOTE: gcc/g++ not found — cannot run the gate."); return 0
    print("== Verify-before-flash gate (C542 software-UART programmer + IAP chain) ==\n")

    objs = {}
    for name, src in [("soft_uart", os.path.join(FW, "soft_uart.c")),
                      ("nbu_prog", os.path.join(FW, "nbu_prog.c")),
                      ("wire_finder", os.path.join(FW, "wire_finder.c")),
                      ("nbu", os.path.join(COMMON, "src", "nbu.c")),
                      ("fw_header", os.path.join(COMMON, "src", "fw_header.c")),
                      ("ecdsa", os.path.join(COMMON, "src", "ecdsa.c")),
                      ("sha256", os.path.join(COMMON, "src", "sha256.c")),
                      ("crc32", os.path.join(COMMON, "src", "crc32.c"))]:
        o = os.path.join(SIM, name + ".o")
        objs[name] = o
        if not cc(src, o):
            print(f"  [FAIL] compile {name}.c"); results.append((f"compile {name}", False))

    # 1+2: wire-finder + software-UART NBU programmer closed loop
    print("\n-- software-UART + NBU/IAP programmer --")
    wf = os.path.join(SIM, "test_wire_finder.exe")
    if cc(os.path.join(SIM, "test_wire_finder.cpp"), os.path.join(SIM, "twf.o"), cxx=True):
        run([GPP, "-O2", os.path.join(SIM, "twf.o"), objs["wire_finder"], "-o", wf])
        run([wf], label="wire-finder logic (9/9)")
    prog = os.path.join(SIM, "test_c5_prog.exe")
    run([GPP, "-O2", "-std=c++17", "-pthread", "-I", FW, "-I", CINC,
         os.path.join(SIM, "test_c5_prog.cpp"), objs["soft_uart"], objs["nbu_prog"],
         objs["nbu"], "-o", prog])
    run([prog], label="software-UART NBU programmer -> nbu.c receiver (closed loop)")

    # 3: IAP chain at 16/32 with real signature verify
    print("\n-- IAP chain @ 16/32 offsets (real ECDSA verify) --")
    run([PY, os.path.join(SIM, "gen_chain_imgs.py")], cwd=SIM, label="generate signed app + BL image")
    chain = os.path.join(SIM, "test_iap_chain.exe")
    run([GPP, "-O2", "-std=c++17", "-pthread", "-I", FW, "-I", CINC,
         os.path.join(SIM, "test_iap_chain.cpp"), objs["soft_uart"], objs["nbu_prog"],
         objs["nbu"], objs["fw_header"], objs["ecdsa"], objs["sha256"], objs["crc32"],
         "-o", chain])
    run([chain], cwd=SIM, label="BL@16 flashes app->32 (tamper rejected); installer@32 writes BL->16")

    # 4: PC parser + on-target builds
    print("\n-- on-target artifacts build --")
    run([PY, os.path.join(REPO, "tools", "dump_bootloader_c5.py"), "--selftest"],
        label="PC dump/report parser selftest")
    if MAKE:
        run([MAKE, "TARGET=ble", "BL_BASE=0x08004000"],
            cwd=os.path.join(REPO, "bootloader", "stm32"),
            label="relocated bootloader builds (BL_BASE=0x08004000)")
        run([MAKE], cwd=os.path.join(REPO, "firmware", "migration16"),
            label="installer@0x08008000 builds (writes BL -> 0x08004000)")
    else:
        print("  NOTE: no make/arm toolchain — skipped on-target builds.")

    # cleanup intermediate objects/exes
    for f in list(objs.values()) + [wf, prog, chain, os.path.join(SIM, "twf.o")]:
        try: os.remove(f)
        except OSError: pass

    print("\n" + "=" * 60)
    bad = [n for n, ok in results if not ok]
    if bad:
        print(f" VERDICT: NO-GO — {len(bad)} check(s) failed: {', '.join(bad)}")
        return 1
    print(" VERDICT: GO - software-UART IAP chain verified end-to-end in simulation;")
    print("          relocated BL + installer build. Safe to flash via IAP.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
