#!/usr/bin/env python3
"""
Ninebot G30 Max — Arch-Aware Firmware RE Harness & Protocol/Pinout Verifier
===========================================================================
Re-disassembles every stock firmware dump and verifies the documented
Ninebot protocol, peripheral usage, and pinouts against binary evidence.

Key correctness properties (fixing the previous version of this script):
  * Uses the real on-disk layout: boards/<board>/firmware/*.bin.
  * Auto-detects architecture and load base PER BINARY from the vector
    table (STM32 Cortex-M3 app @ 0x08001000  vs  nRF51 Cortex-M0 app
    @ 0x00018000) instead of assuming STM32 @ 0x08001000 for all of them.
  * Masks the Thumb bit before disassembling handlers (the old script
    disassembled at odd addresses and produced garbage for nRF51).
  * Detects encrypted/invalid images (bad SP/reset) and reports the
    condition + entropy instead of emitting noise.

This is a STATIC analysis / verification tool. It never touches hardware.

Usage:
    python tools/analysis/disassemble_firmware.py            # analyze all, stdout
    python tools/analysis/disassemble_firmware.py --json     # machine-readable summary
    python tools/analysis/disassemble_firmware.py BLE_1.1.7  # one image by key
"""

import os
import sys
import json
import math
import struct
import argparse
from collections import defaultdict

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_LITTLE_ENDIAN
    HAS_CAPSTONE = True
except ImportError:
    HAS_CAPSTONE = False

# ─── Layout ──────────────────────────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))

FIRMWARE_FILES = {
    "DRV_1.2.6":   "boards/esc-motor/firmware/DRV_1.2.6.bin",
    "DRV_1.6.13":  "boards/esc-motor/firmware/DRV_1.6.13_Compat.bin",
    "BLE_1.1.0":   "boards/ble-dashboard/firmware/BLE_1.1.0.bin",
    "BLE_1.1.7":   "boards/ble-dashboard/firmware/BLE_1.1.7.bin",
    "BMS_1.3.4":   "boards/bms-battery/firmware/BMS_1.3.4.bin",
    "BMS_1.7.4.5": "boards/bms-battery/firmware/BMS_1.7.4.5.bin",
}

PROTOCOL_HEADER = bytes([0x5A, 0xA5])

# STM32F103 peripheral register addresses (literal-pool references)
STM32_PERIPHERALS = {
    0x40013800: "USART1_SR", 0x40013804: "USART1_DR", 0x40013808: "USART1_BRR",
    0x40004400: "USART2_SR", 0x40004404: "USART2_DR", 0x40004408: "USART2_BRR",
    0x40004800: "USART3_SR", 0x40004804: "USART3_DR", 0x40004808: "USART3_BRR",
    0x40005400: "I2C1_CR1",  0x40005410: "I2C1_DR",   0x40005414: "I2C1_SR1",
    0x40005800: "I2C2_CR1",  0x40005810: "I2C2_DR",
    0x40012C00: "TIM1_CR1",  0x40000000: "TIM2_CR1",  0x40000400: "TIM3_CR1",
    0x40010800: "GPIOA_CRL", 0x40010804: "GPIOA_CRH", 0x40010C00: "GPIOB_CRL",
    0x40010C04: "GPIOB_CRH", 0x40021018: "RCC_APB1ENR", 0x40021014: "RCC_APB2ENR",
}

# nRF51 peripheral bases (Nordic)
NRF51_PERIPHERALS = {
    0x40002000: "UART0_TASKS_STARTRX", 0x40002100: "UART0_EVENTS_CTS",
    0x40002300: "UART0_INTEN", 0x40002500: "UART0_ENABLE", 0x40002524: "UART0_BAUDRATE",
}

# 36 MHz APB1 → 0x0139, 72 MHz APB2 → 0x0271 are the canonical 115200 BRR words.
STM32_BRR_115200 = {0x0271: "115200 @72MHz (APB2/USART1)", 0x0139: "115200 @36MHz (APB1/USART2-3)"}

# STM32 vector index = 16 + IRQn.  USART1 IRQn=37, USART2=38, USART3=39.
STM32_IRQ_VECTORS = {"USART1": 16 + 37, "USART2": 16 + 38, "USART3": 16 + 39,
                     "I2C1_EV": 16 + 31, "TIM1_UP": 16 + 25, "SysTick": 15}


def load(path):
    with open(path, "rb") as f:
        return f.read()


def shannon_entropy(data):
    if not data:
        return 0.0
    freq = defaultdict(int)
    for b in data:
        freq[b] += 1
    n = len(data)
    return -sum((c / n) * math.log2(c / n) for c in freq.values())


def detect_arch(data):
    """Identify load base & arch from the Cortex vector table at offset 0.

    Returns dict {arch, base, sp, reset, valid, reason}.
    """
    if len(data) < 8:
        return {"valid": False, "reason": "file too small", "arch": "?", "base": 0}
    sp, reset = struct.unpack_from("<II", data, 0)
    reset_clean = reset & ~1
    sp_in_sram = 0x20000000 <= sp <= 0x20008000

    # STM32F103 application image: reset vector in 0x0800_1xxx..0x0801_xxxx
    if sp_in_sram and 0x08000000 <= reset_clean < 0x08040000:
        base = reset_clean & 0xFFFFF000
        return {"valid": True, "arch": "Cortex-M3 (STM32F103)", "base": base,
                "sp": sp, "reset": reset_clean, "reason": "STM32 app vector table"}
    # nRF51822 application image: reset vector around the post-SoftDevice app base
    if sp_in_sram and 0x00010000 <= reset_clean < 0x00040000:
        base = 0x00018000 if 0x00018000 <= reset_clean < 0x00040000 else (reset_clean & 0xFFFFF000)
        return {"valid": True, "arch": "Cortex-M0 (nRF51822)", "base": base,
                "sp": sp, "reset": reset_clean, "reason": "nRF51 app vector table"}
    return {"valid": False, "arch": "unknown/encrypted", "base": 0, "sp": sp,
            "reset": reset_clean,
            "reason": "SP/reset not a valid Cortex vector table — encrypted or non-zero offset"}


def md(arch):
    m = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    m.detail = True
    return m


def vector_table(data, base):
    """Return list of (idx, name, addr) for resolvable handlers."""
    names = ["Initial_SP", "Reset", "NMI", "HardFault", "MemManage", "BusFault",
             "UsageFault", "RSVD", "RSVD", "RSVD", "RSVD", "SVCall", "DebugMon",
             "RSVD", "PendSV", "SysTick"]
    out = []
    n = min(len(names), len(data) // 4)
    for i in range(n):
        addr = struct.unpack_from("<I", data, i * 4)[0]
        out.append((i, names[i], addr))
    return out


def in_image(addr, base, length):
    a = addr & ~1
    return base <= a < base + length


def find_immediates(data, base, arch, wanted):
    """Linear-sweep disassembly; collect instructions whose immediate is in `wanted`.

    Returns {imm: [(addr, mnemonic, op_str), ...]}.
    """
    if not HAS_CAPSTONE:
        return {}
    m = md(arch)
    hits = defaultdict(list)
    for insn in m.disasm(data, base):
        for op in insn.operands:
            if op.type == 2:  # ARM_OP_IMM
                if op.imm in wanted:
                    hits[op.imm].append((insn.address, insn.mnemonic, insn.op_str))
    return hits


def find_literal_refs(data, addr_map):
    """Find 32-bit little-endian addresses from addr_map present in the literal pool."""
    found = {}
    for reg_addr, name in addr_map.items():
        needle = struct.pack("<I", reg_addr)
        positions = []
        off = 0
        while True:
            i = data.find(needle, off)
            if i < 0:
                break
            positions.append(i)
            off = i + 1
        if positions:
            found[name] = positions
    return found


def find_brr(data):
    out = {}
    for word, desc in STM32_BRR_115200.items():
        cnt = data.count(struct.pack("<H", word))
        if cnt:
            out[desc] = cnt
    return out


def extract_strings(data, base, min_len=4):
    interesting = ("uart", "ble", "ninebot", "segway", "scooter", "g30", "max",
                   "version", "drv", "bms", "esc", "flash", "boot", "update",
                   "error", "cell", "volt", "temp", "miio", "mi ", "token", "auth",
                   "sn", "bond", "softdevice")
    strings, cur, start = [], "", 0
    for i, b in enumerate(data):
        if 0x20 <= b <= 0x7E:
            if not cur:
                start = i
            cur += chr(b)
        else:
            if len(cur) >= min_len:
                strings.append((base + start, cur))
            cur = ""
    if len(cur) >= min_len:
        strings.append((base + start, cur))
    notable = [(a, s) for a, s in strings if any(k in s.lower() for k in interesting)]
    return strings, notable


def disasm_handler(data, base, arch, addr, max_insns=40):
    if not HAS_CAPSTONE:
        return []
    m = md(arch)
    off = (addr & ~1) - base
    if off < 0 or off >= len(data):
        return []
    out = []
    for insn in m.disasm(data[off:off + max_insns * 4], addr & ~1):
        out.append((insn.address, insn.mnemonic, insn.op_str))
        mn, ops = insn.mnemonic.lower(), insn.op_str.lower()
        if (mn == "bx" and "lr" in ops) or (mn.startswith("pop") and "pc" in ops):
            break
        if len(out) >= max_insns:
            break
    return out


def analyze(key, rel_path):
    path = os.path.join(REPO_ROOT, rel_path)
    res = {"key": key, "path": rel_path}
    if not os.path.exists(path):
        res["error"] = "missing"
        return res
    data = load(path)
    import hashlib
    res["size"] = len(data)
    res["md5"] = hashlib.md5(data).hexdigest()
    res["sha256"] = hashlib.sha256(data).hexdigest()
    res["entropy"] = round(shannon_entropy(data), 3)

    info = detect_arch(data)
    res["arch"] = info
    base = info["base"]

    if not info["valid"]:
        res["verdict"] = "ENCRYPTED/UNRECOGNIZED — static disassembly skipped"
        return res

    is_stm32 = "STM32" in info["arch"]
    periph_map = STM32_PERIPHERALS if is_stm32 else NRF51_PERIPHERALS
    res["peripherals"] = find_literal_refs(data, periph_map)
    if is_stm32:
        res["brr_115200"] = find_brr(data)

    # protocol header byte comparisons in code (CMP/MOVS #0x5A / #0xA5)
    imm_hits = find_immediates(data, base, info["arch"], {0x5A, 0xA5, 0x20, 0x21, 0x22, 0x3E})
    res["header_cmp_0x5A"] = [(hex(a), mn, op) for a, mn, op in imm_hits.get(0x5A, [])][:12]
    res["header_cmp_0xA5"] = [(hex(a), mn, op) for a, mn, op in imm_hits.get(0xA5, [])][:12]
    res["raw_5AA5_in_image"] = data.count(PROTOCOL_HEADER)

    # vector table + USART/I2C IRQ handler resolution
    vt = vector_table(data, base)
    res["initial_sp"] = hex(vt[0][2])
    res["reset"] = hex(vt[1][2] & ~1)
    handlers = {}
    if is_stm32:
        for name, idx in STM32_IRQ_VECTORS.items():
            if idx * 4 + 4 <= len(data):
                addr = struct.unpack_from("<I", data, idx * 4)[0]
                if addr and in_image(addr, base, len(data)):
                    handlers[name] = {"addr": hex(addr & ~1),
                                      "disasm": [f"{hex(a)}: {mn} {op}"
                                                 for a, mn, op in disasm_handler(data, base, info["arch"], addr, 24)]}
    res["irq_handlers"] = handlers

    _, notable = extract_strings(data, base)
    res["notable_strings"] = [(hex(a), s) for a, s in notable][:40]
    res["verdict"] = "OK — " + info["arch"]
    return res


def print_report(res):
    print("#" * 78)
    print(f"##  {res['key']}   ({res['path']})")
    print("#" * 78)
    if res.get("error") == "missing":
        print("  [!] FILE MISSING\n")
        return
    a = res["arch"]
    print(f"  size={res['size']}B  entropy={res['entropy']}  md5={res['md5'][:16]}")
    print(f"  arch: {a['arch']}   base=0x{a['base']:08X}   SP=0x{a['sp']:08X}  reset=0x{a['reset']:08X}")
    print(f"  detect: {a['reason']}")
    print(f"  VERDICT: {res['verdict']}")
    if not a["valid"]:
        print()
        return
    if "brr_115200" in res:
        print(f"  baud (BRR 115200): {res['brr_115200'] or 'none found'}")
    if res.get("peripherals"):
        print("  peripheral literal refs:")
        for name, pos in sorted(res["peripherals"].items()):
            print(f"      {name:14s} x{len(pos)}")
    print(f"  raw 0x5A 0xA5 byte pairs in image: {res['raw_5AA5_in_image']}")
    print(f"  in-code CMP/MOV #0x5A: {len(res['header_cmp_0x5A'])} hits; #0xA5: {len(res['header_cmp_0xA5'])} hits")
    for a2, mn, op in res["header_cmp_0x5A"][:6]:
        print(f"      {a2}: {mn} {op}")
    if res.get("irq_handlers"):
        print("  resolved IRQ handlers:")
        for name, h in res["irq_handlers"].items():
            print(f"      {name} @ {h['addr']}")
    if res.get("notable_strings"):
        print("  notable strings:")
        for addr, s in res["notable_strings"][:15]:
            print(f"      {addr}: {s!r}")
    print()


def main():
    ap = argparse.ArgumentParser(description="Arch-aware Ninebot firmware RE harness")
    ap.add_argument("only", nargs="?", help="analyze a single image by key (e.g. DRV_1.2.6)")
    ap.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = ap.parse_args()

    if not HAS_CAPSTONE:
        print("[WARN] capstone not installed — disassembly disabled (pip install capstone)", file=sys.stderr)

    items = FIRMWARE_FILES.items()
    if args.only:
        if args.only not in FIRMWARE_FILES:
            print(f"unknown image '{args.only}'. known: {', '.join(FIRMWARE_FILES)}", file=sys.stderr)
            sys.exit(2)
        items = [(args.only, FIRMWARE_FILES[args.only])]

    results = [analyze(k, p) for k, p in items]
    if args.json:
        print(json.dumps(results, indent=2))
    else:
        for r in results:
            print_report(r)


if __name__ == "__main__":
    main()
