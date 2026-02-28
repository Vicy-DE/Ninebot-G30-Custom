#!/usr/bin/env python3
"""
Verify reassembled firmware binary against the original.
Reports byte-level differences if any.
"""

import sys
from pathlib import Path


def compare_binaries(original_path, rebuilt_path):
    orig = Path(original_path).read_bytes()
    rebuilt = Path(rebuilt_path).read_bytes()

    print(f"Original : {original_path} ({len(orig)} bytes)")
    print(f"Rebuilt  : {rebuilt_path} ({len(rebuilt)} bytes)")
    print()

    if len(orig) != len(rebuilt):
        print(f"SIZE MISMATCH: original={len(orig)}, rebuilt={len(rebuilt)}")
        min_len = min(len(orig), len(rebuilt))
    else:
        print(f"Size match: {len(orig)} bytes")
        min_len = len(orig)

    diffs = []
    for i in range(min_len):
        if orig[i] != rebuilt[i]:
            diffs.append(i)

    if len(orig) > len(rebuilt):
        for i in range(len(rebuilt), len(orig)):
            diffs.append(i)
    elif len(rebuilt) > len(orig):
        for i in range(len(orig), len(rebuilt)):
            diffs.append(i)

    if not diffs:
        print("\nBINARIES ARE IDENTICAL!")
        return True

    print(f"\n{len(diffs)} byte(s) differ:")

    # Show first 50 differences
    shown = 0
    for i in diffs[:50]:
        base_addr = 0x08001000 + i
        o = orig[i] if i < len(orig) else '--'
        r = rebuilt[i] if i < len(rebuilt) else '--'
        if isinstance(o, int):
            o = f'0x{o:02X}'
        if isinstance(r, int):
            r = f'0x{r:02X}'
        print(f"  offset 0x{i:06X} (addr 0x{base_addr:08X}): orig={o}  rebuilt={r}")
        shown += 1

    if len(diffs) > 50:
        print(f"  ... and {len(diffs) - 50} more differences")

    # Summarize contiguous difference regions
    print(f"\nDifference regions:")
    region_start = diffs[0]
    region_end = diffs[0]
    regions = []
    for d in diffs[1:]:
        if d == region_end + 1:
            region_end = d
        else:
            regions.append((region_start, region_end))
            region_start = d
            region_end = d
    regions.append((region_start, region_end))

    for start, end in regions[:20]:
        base_start = 0x08001000 + start
        base_end = 0x08001000 + end
        length = end - start + 1
        print(f"  0x{base_start:08X} - 0x{base_end:08X} ({length} bytes)")

    if len(regions) > 20:
        print(f"  ... and {len(regions) - 20} more regions")

    return False


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <original.bin> <rebuilt.bin>")
        sys.exit(1)

    ok = compare_binaries(sys.argv[1], sys.argv[2])
    sys.exit(0 if ok else 1)
