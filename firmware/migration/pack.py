#!/usr/bin/env python3
"""Pack the two migration apps into one big image flashed via the stock IAP.

The image is loaded at 0x08001000:
  offset 0x0000  trampoline.bin   (runs at 0x08001000)
  offset 0x3000  updater.bin      (runs at 0x08004000 = 0x08001000 + 0x3000)
gaps filled with 0xFF.

    python pack.py build/trampoline.bin build/updater.bin build/migrate_big.bin
"""
import sys

TRAMP_BASE = 0x08001000
UPDATER_BASE = 0x08004000


def main(argv):
    if len(argv) != 4:
        print("usage: pack.py <trampoline.bin> <updater.bin> <out.bin>", file=sys.stderr)
        return 2
    tramp = open(argv[1], "rb").read()
    upd = open(argv[2], "rb").read()
    upd_off = UPDATER_BASE - TRAMP_BASE          # 0x3000
    if len(tramp) > upd_off:
        print(f"ERROR: trampoline ({len(tramp)} B) overruns the app slot "
              f"(must be < 0x{upd_off:X}).", file=sys.stderr)
        return 1
    img = bytearray(b"\xFF" * upd_off)
    img[0:len(tramp)] = tramp
    img += upd
    with open(argv[3], "wb") as f:
        f.write(img)
    print(f"packed: trampoline {len(tramp)} B @0x{TRAMP_BASE:08X}, "
          f"updater {len(upd)} B @0x{UPDATER_BASE:08X} -> {len(img)} B big app")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
