#!/usr/bin/env python3
"""
nrf51_swd.py — dump / inspect / flash the G30 dashboard's nRF51822 over SWD.

The dashboard's only MCU is an nRF51822 (see boards/ble-dashboard/MCU_IDENTIFICATION.md).
Unlike the STM32C5, the nRF51 IS supported by OpenOCD, so SWD gives us a complete backup and a
flash path that does NOT depend on the auth-gated Ninebot IAP/BLE handshake.

Subcommands
    info     read FICR/UICR: device id, flash/RAM size, SoftDevice region, readback protection
    dump     read the whole flash (+UICR/FICR) to files, verified by a second independent read
    flash    program an image at an address, then verify (refuses to touch UICR)
    restore  reflash a previously dumped stock image

Safety rules enforced here
  * never writes UICR  -> APPROTECT/RBPCONF can't be set by accident, SWD stays open
  * `flash` refuses to run unless a verified stock dump exists (override: --i-have-a-backup)
  * every write is read back and compared; mismatch = non-zero exit
  * `dump` reads twice and compares, so a flaky probe can't silently give you a bad backup

Wiring (SWD, 3 wires): probe SWDIO -> nRF51 SWDIO, SWCLK -> SWCLK, GND -> GND.
Power the dashboard from the scooter (or 5V on the red dash wire); do NOT let the probe
back-power a live board.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_OUT = os.path.join(REPO, "boards", "ble-dashboard", "firmware", "dumps")

# --- nRF51822 memory map ---------------------------------------------------
FLASH_BASE = 0x00000000
FLASH_SIZE_QFAA = 0x40000        # 256 KB
RAM_BASE = 0x20000000
FICR_BASE = 0x10000000
UICR_BASE = 0x10001000
PAGE_SIZE = 0x400                # 1 KB

# FICR / UICR registers we care about
FICR_CODEPAGESIZE = FICR_BASE + 0x010
FICR_CODESIZE = FICR_BASE + 0x014
FICR_DEVICEID0 = FICR_BASE + 0x060
FICR_DEVICEID1 = FICR_BASE + 0x064
FICR_DEVICEADDR0 = FICR_BASE + 0x0A4
FICR_DEVICEADDR1 = FICR_BASE + 0x0A8
UICR_CLENR0 = UICR_BASE + 0x000  # code region 0 length (= SoftDevice size)
UICR_RBPCONF = UICR_BASE + 0x004  # readback protection
UICR_XTALFREQ = UICR_BASE + 0x008
UICR_BOOTLOADERADDR = UICR_BASE + 0x014


def find_openocd() -> str:
    exe = shutil.which("openocd")
    if exe:
        return exe
    root = os.environ.get("LOCALAPPDATA", "")
    guess = os.path.join(
        root, "Microsoft", "WinGet", "Packages",
        "xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe",
        "xpack-openocd-0.12.0-7", "bin", "openocd.exe")
    if os.path.exists(guess):
        return guess
    sys.exit("openocd not found — install it or put it on PATH")


def scripts_dir(openocd: str) -> str:
    d = os.path.abspath(os.path.join(os.path.dirname(openocd), "..", "openocd", "scripts"))
    if os.path.isdir(d):
        return d
    d2 = os.path.abspath(os.path.join(os.path.dirname(openocd), "..", "share", "openocd", "scripts"))
    return d2 if os.path.isdir(d2) else d


class Ocd:
    """Runs one OpenOCD batch: init, <commands>, exit."""

    def __init__(self, iface: str = "auto", speed: int = 1000, verbose: bool = False):
        self.exe = find_openocd()
        self.scripts = scripts_dir(self.exe)
        self.iface = iface
        self.speed = speed
        self.verbose = verbose

    def _iface_args(self, iface: str) -> list[str]:
        if iface == "stlink-dap":       # ST-LINK V3 / V2-1 in DAP-direct mode (preferred)
            return ["-f", "interface/stlink-dap.cfg", "-c", "transport select dapdirect_swd"]
        if iface == "stlink":           # ST-LINK in high-level (HLA) mode
            return ["-f", "interface/stlink.cfg", "-c", "transport select hla_swd"]
        if iface == "cmsis-dap":
            return ["-f", "interface/cmsis-dap.cfg", "-c", "transport select swd"]
        if iface == "jlink":
            return ["-f", "interface/jlink.cfg", "-c", "transport select swd"]
        raise ValueError(iface)

    def run(self, commands: list[str], timeout: int = 180) -> str:
        order = [self.iface] if self.iface != "auto" else ["stlink-dap", "stlink"]
        last = ""
        for iface in order:
            cmd = [self.exe, "-s", self.scripts]
            cmd += self._iface_args(iface)
            cmd += ["-c", f"adapter speed {self.speed}"]
            cmd += ["-f", "target/nordic/nrf51.cfg"]
            cmd += ["-c", "init"]
            for c in commands:
                cmd += ["-c", c]
            cmd += ["-c", "exit"]
            if self.verbose:
                print("  $", " ".join(cmd))
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
            out = (p.stdout or "") + (p.stderr or "")
            last = out
            if p.returncode == 0 and "Error:" not in out:
                if self.iface == "auto" and self.verbose:
                    print(f"  (interface: {iface})")
                return out
            if self.verbose:
                print(f"  [{iface}] failed:\n{out[:600]}")
        raise RuntimeError(f"OpenOCD failed.\n{last[-2500:]}")

    def read32(self, addr: int) -> int:
        out = self.run(["halt", f"mdw 0x{addr:08X}"])
        for ln in out.splitlines():
            ln = ln.strip().lower()
            if ln.startswith(f"0x{addr:08x}:"):
                return int(ln.split(":")[1].strip().split()[0], 16)
        raise RuntimeError(f"could not read 0x{addr:08X}\n{out[-800:]}")

    def read_many(self, addrs: dict[str, int]) -> dict[str, int]:
        cmds = ["halt"] + [f"mdw 0x{a:08X}" for a in addrs.values()]
        out = self.run(cmds)
        got = {}
        for ln in out.splitlines():
            ln = ln.strip().lower()
            if ln.startswith("0x") and ":" in ln:
                try:
                    a = int(ln.split(":")[0], 16)
                    v = int(ln.split(":")[1].strip().split()[0], 16)
                    got[a] = v
                except (ValueError, IndexError):
                    pass
        return {k: got.get(v) for k, v in addrs.items()}

    def dump_region(self, path: str, addr: int, size: int):
        self.run(["halt", f'dump_image "{path.replace(os.sep, "/")}" 0x{addr:08X} 0x{size:X}'],
                 timeout=600)


# --- helpers ---------------------------------------------------------------

def sha(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(65536), b""):
            h.update(blk)
    return h.hexdigest()


def decode_info(v: dict) -> dict:
    page = v.get("CODEPAGESIZE") or PAGE_SIZE
    npages = v.get("CODESIZE") or (FLASH_SIZE_QFAA // PAGE_SIZE)
    rbp = v.get("RBPCONF")
    clenr0 = v.get("CLENR0")
    info = {
        "device_id": f"{v.get('DEVICEID1', 0):08X}{v.get('DEVICEID0', 0):08X}",
        "flash_page_size": page,
        "flash_pages": npages,
        "flash_size": page * npages,
        "uicr_clenr0": clenr0,
        "uicr_bootloaderaddr": v.get("BOOTLOADERADDR"),
        "uicr_rbpconf": rbp,
        "xtalfreq": v.get("XTALFREQ"),
    }
    # nRF51 RBPCONF: PALL in bits[7:0], PR0 in bits[15:8]; 0xFF = unprotected
    if rbp is not None and rbp != 0xFFFFFFFF:
        pall = rbp & 0xFF
        pr0 = (rbp >> 8) & 0xFF
        info["readback_protected_all"] = (pall != 0xFF)
        info["readback_protected_region0"] = (pr0 != 0xFF)
    else:
        info["readback_protected_all"] = False
        info["readback_protected_region0"] = False
    # BLE MAC (as advertised): DEVICEADDR with the two MSBs set for a random static address
    a0, a1 = v.get("DEVICEADDR0"), v.get("DEVICEADDR1")
    if a0 is not None and a1 is not None:
        raw = struct.pack("<IH", a0, a1 & 0xFFFF)
        mac = bytearray(raw[::-1])
        mac[0] |= 0xC0
        info["ble_mac_random_static"] = ":".join(f"{b:02X}" for b in mac)
    return info


REGS = {
    "CODEPAGESIZE": FICR_CODEPAGESIZE, "CODESIZE": FICR_CODESIZE,
    "DEVICEID0": FICR_DEVICEID0, "DEVICEID1": FICR_DEVICEID1,
    "DEVICEADDR0": FICR_DEVICEADDR0, "DEVICEADDR1": FICR_DEVICEADDR1,
    "CLENR0": UICR_CLENR0, "RBPCONF": UICR_RBPCONF,
    "XTALFREQ": UICR_XTALFREQ, "BOOTLOADERADDR": UICR_BOOTLOADERADDR,
}


def cmd_info(args) -> int:
    ocd = Ocd(args.interface, args.speed, args.verbose)
    print("[*] reading FICR/UICR over SWD ...")
    info = decode_info(ocd.read_many(REGS))
    print(f"  device id            : {info['device_id']}")
    print(f"  flash                : {info['flash_size']//1024} KB "
          f"({info['flash_pages']} pages x {info['flash_page_size']} B)")
    if info.get("ble_mac_random_static"):
        print(f"  BLE MAC (static)     : {info['ble_mac_random_static']}")
    c = info["uicr_clenr0"]
    print(f"  UICR.CLENR0          : {'0x%08X' % c if c is not None else '?'}"
          + (f"  -> SoftDevice occupies 0x0..0x{c:X}" if c not in (None, 0xFFFFFFFF) else ""))
    b = info["uicr_bootloaderaddr"]
    print(f"  UICR.BOOTLOADERADDR  : {'0x%08X' % b if b is not None else '?'}"
          + ("  -> DFU bootloader present" if b not in (None, 0xFFFFFFFF) else "  (none)"))
    r = info["uicr_rbpconf"]
    print(f"  UICR.RBPCONF         : {'0x%08X' % r if r is not None else '?'}")
    if info["readback_protected_all"]:
        print("  [!] READBACK PROTECTION IS ON — flash cannot be read out.")
        print("      A dump is impossible without a mass-erase (which destroys the stock firmware).")
        return 2
    print("  [+] readback protection: OFF — full dump is possible")
    if args.json:
        print(json.dumps(info, indent=2))
    return 0


def cmd_dump(args) -> int:
    ocd = Ocd(args.interface, args.speed, args.verbose)
    os.makedirs(args.out, exist_ok=True)
    print("[*] identifying ...")
    info = decode_info(ocd.read_many(REGS))
    if info["readback_protected_all"]:
        print("[!] readback protection is ON — refusing (a dump would be all 0xFF/garbage).")
        return 2
    if info["readback_protected_region0"]:
        # PR0 protects code region 0 (the SoftDevice, 0x0..UICR.CLENR0) from debugger reads.
        # The dump would still "succeed" but that region would be garbage, and a restore from it
        # would destroy BLE. Require an explicit acknowledgement.
        print("[!] UICR.RBPCONF.PR0 is set: code region 0 (the SoftDevice) is READBACK PROTECTED.")
        print("    The SoftDevice part of this dump would be garbage and MUST NOT be restored.")
        if not args.allow_region0_protected:
            print("    Refusing. Re-run with --allow-region0-protected to dump the app/bootloader")
            print("    only (and keep a Nordic S110 .hex to restore the stack separately).")
            return 2
        print("    Continuing anyway (--allow-region0-protected): treat 0x0..CLENR0 as INVALID.")
    size = args.size or info["flash_size"] or FLASH_SIZE_QFAA
    stamp = time.strftime("%Y%m%d-%H%M%S")
    tag = f"nrf51_{info['device_id'][:8]}_{stamp}"

    flash_a = os.path.join(args.out, f"{tag}_flash.bin")
    flash_b = os.path.join(args.out, f"{tag}_flash.verify.bin")
    uicr = os.path.join(args.out, f"{tag}_uicr.bin")
    ficr = os.path.join(args.out, f"{tag}_ficr.bin")

    print(f"[*] dumping flash 0x0..0x{size:X} ({size//1024} KB) — pass 1 ...")
    ocd.dump_region(flash_a, FLASH_BASE, size)
    print("[*] dumping flash — pass 2 (independent verify read) ...")
    ocd.dump_region(flash_b, FLASH_BASE, size)
    print("[*] dumping UICR + FICR ...")
    ocd.dump_region(uicr, UICR_BASE, 0x100)
    ocd.dump_region(ficr, FICR_BASE, 0x100)

    ha, hb = sha(flash_a), sha(flash_b)
    if ha != hb:
        print("[!] VERIFY FAILED: the two reads differ — bad wiring/power or an unstable probe.")
        print(f"    pass1 {ha}\n    pass2 {hb}")
        return 3
    os.remove(flash_b)

    d = open(flash_a, "rb").read()
    meta = {
        "tool": "nrf51_swd.py", "created": stamp, "chip": "nRF51822",
        "flash_size": size, "sha256_flash": ha,
        "region0_valid": not info["readback_protected_region0"],
        "files": {"flash": os.path.basename(flash_a), "uicr": os.path.basename(uicr),
                  "ficr": os.path.basename(ficr)},
        **info,
    }
    meta_path = os.path.join(args.out, f"{tag}.json")
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)

    print(f"[+] flash  -> {flash_a}")
    print(f"[+] uicr   -> {uicr}")
    print(f"[+] ficr   -> {ficr}")
    print(f"[+] meta   -> {meta_path}")
    print(f"[+] sha256 : {ha}  (both passes identical)")

    nonff = sum(1 for b in d if b != 0xFF)
    print(f"[+] {nonff*100//len(d)}% of flash is programmed")
    if len(d) > 0x18004:
        sp, rv = struct.unpack_from("<II", d, 0x18000)
        print(f"[+] app @0x18000: SP=0x{sp:08X} reset=0x{rv:08X}"
              + ("  (looks valid)" if 0x20000000 <= sp <= 0x20004000 else "  (?)"))
    return 0


def _have_backup(out: str) -> bool:
    return any(f.endswith(".json") for f in os.listdir(out)) if os.path.isdir(out) else False


def cmd_flash(args) -> int:
    if not os.path.exists(args.image):
        sys.exit(f"no such image: {args.image}")
    addr = int(args.address, 0)
    if UICR_BASE <= addr < UICR_BASE + 0x1000:
        sys.exit("refusing to write UICR (would risk locking SWD access)")
    if not args.i_have_a_backup and not _have_backup(args.out):
        sys.exit(f"no verified dump found in {args.out} — run `dump` first "
                 f"(or pass --i-have-a-backup if it lives elsewhere)")
    ocd = Ocd(args.interface, args.speed, args.verbose)
    img = args.image.replace(os.sep, "/")
    print(f"[*] programming {os.path.basename(args.image)} @0x{addr:08X} ...")
    ocd.run(["halt", f'program "{img}" 0x{addr:X} verify'], timeout=900)
    print("[+] programmed and verified by OpenOCD")
    if not args.no_reset:
        ocd.run(["reset run"])
        print("[+] target reset and running")
    return 0


def cmd_restore(args) -> int:
    meta_path = args.meta
    with open(meta_path) as f:
        meta = json.load(f)
    base = os.path.dirname(os.path.abspath(meta_path))
    flash = os.path.join(base, meta["files"]["flash"])
    if sha(flash) != meta["sha256_flash"]:
        sys.exit("backup sha256 mismatch — refusing to restore a corrupt image")
    if meta.get("region0_valid") is False:
        sys.exit("this dump was taken while the SoftDevice region was readback protected — "
                 "restoring it would overwrite a working BLE stack with garbage. Refusing.")
    print(f"[*] restoring stock flash from {os.path.basename(flash)} ...")
    ocd = Ocd(args.interface, args.speed, args.verbose)
    ocd.run(["halt", f'program "{flash.replace(os.sep, "/")}" 0x0 verify'], timeout=1200)
    ocd.run(["reset run"])
    print("[+] stock firmware restored, target running")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-i", "--interface", default="auto",
                    choices=["auto", "stlink-dap", "stlink", "cmsis-dap", "jlink"])
    ap.add_argument("-s", "--speed", type=int, default=1000, help="adapter kHz (default 1000)")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("-o", "--out", default=DEFAULT_OUT, help="dump directory")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info", help="read FICR/UICR and report protection state").add_argument(
        "--json", action="store_true")
    d = sub.add_parser("dump", help="verified full-flash dump (+UICR/FICR)")
    d.add_argument("--size", type=lambda x: int(x, 0), default=None)
    d.add_argument("--allow-region0-protected", action="store_true",
                   help="dump even when the SoftDevice region is readback protected "
                        "(that part of the image will be invalid — never restore it)")
    f = sub.add_parser("flash", help="program an image and verify")
    f.add_argument("image")
    f.add_argument("--address", default="0x18000")
    f.add_argument("--i-have-a-backup", action="store_true")
    f.add_argument("--no-reset", action="store_true")
    r = sub.add_parser("restore", help="reflash a stock dump from its .json metadata")
    r.add_argument("meta")

    args = ap.parse_args()
    try:
        return {"info": cmd_info, "dump": cmd_dump,
                "flash": cmd_flash, "restore": cmd_restore}[args.cmd](args)
    except RuntimeError as e:
        print(f"[!] {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
