"""
Deep disassembler for Ninebot G30 Max firmware.
Extracts complete function disassembly for protocol-related routines.
Outputs annotated ARM Thumb assembly for reverse engineering to C++.
"""

import os
import struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_LITTLE_ENDIAN
from capstone import CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
APP_BASE = 0x08001000

def load(path):
    with open(path, "rb") as f:
        return f.read()

def addr_to_off(addr):
    return addr - APP_BASE

def off_to_addr(off):
    return off + APP_BASE

def disasm_region(data, start_off, length, label=""):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    region = data[start_off:start_off + length]
    base = off_to_addr(start_off)
    
    print(f"\n{'='*78}")
    print(f"  FUNCTION: {label}")
    print(f"  Address range: 0x{base:08X} - 0x{base+length:08X}")
    print(f"  Offset range:  0x{start_off:06X} - 0x{start_off+length:06X}")
    print(f"{'='*78}")
    
    instructions = []
    for insn in md.disasm(region, base):
        instructions.append(insn)
        
        # Annotate known constants/registers
        annotation = ""
        op = insn.op_str.lower()
        mn = insn.mnemonic.lower()
        
        if "0x5a" in op:
            annotation = "; PROTOCOL_HEADER_BYTE1 = 0x5A"
        elif "0xa5" in op:
            annotation = "; PROTOCOL_HEADER_BYTE2 = 0xA5"
        elif "0x40013800" in op or "0x40013804" in op:
            annotation = "; USART1_SR / USART1_DR"
        elif "0x40004400" in op or "0x40004404" in op:
            annotation = "; USART2_SR / USART2_DR"
        elif "0x40004800" in op or "0x40004804" in op:
            annotation = "; USART3_SR / USART3_DR"
        elif "0x4000440c" in op:
            annotation = "; USART2_CR1"
        elif "0x4000480c" in op:
            annotation = "; USART3_CR1"
        elif "0x4001380c" in op:
            annotation = "; USART1_CR1"
        elif "#0x20" in op and "cmp" in mn:
            annotation = "; ESC_ADDRESS = 0x20"
        elif "#0x21" in op and "cmp" in mn:
            annotation = "; BLE_ADDRESS = 0x21"
        elif "#0x22" in op and "cmp" in mn:
            annotation = "; BMS_ADDRESS = 0x22"
        elif "#0x3e" in op and "cmp" in mn:
            annotation = "; APP_ADDRESS = 0x3E"
        elif "#0x3f" in op and "cmp" in mn:
            annotation = "; PC_ADDRESS = 0x3F"
        elif "0xffff" in op:
            annotation = "; CHECKSUM_XOR_MASK = 0xFFFF"
        elif "#0x271" in op or "#0x0271" in op:
            annotation = "; BRR for 115200 baud @ 72MHz"
        elif "#0x139" in op or "#0x0139" in op:
            annotation = "; BRR for 115200 baud @ 36MHz"
        elif "pop" in mn and "pc" in op:
            annotation = "; FUNCTION RETURN"
        elif "bx" in mn and "lr" in op:
            annotation = "; FUNCTION RETURN"
        elif "push" in mn and "lr" in op:
            annotation = "; FUNCTION PROLOGUE"
        elif "bl" == mn:
            annotation = "; FUNCTION CALL"
        
        # Format: address: hex_bytes  mnemonic  operands  ; annotation
        raw_bytes = data[insn.address - APP_BASE : insn.address - APP_BASE + insn.size]
        hex_str = " ".join(f"{b:02X}" for b in raw_bytes)
        
        print(f"  0x{insn.address:08X}: {hex_str:12s}  {insn.mnemonic:8s} {insn.op_str:30s} {annotation}")
    
    return instructions


def find_function_bounds(data, start_off):
    """Try to find function boundaries from a known interior point."""
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    
    # Search backward for PUSH {... LR}
    func_start = start_off
    for probe in range(start_off, max(0, start_off - 256), -2):
        region = data[probe:probe+4]
        for insn in md.disasm(region, off_to_addr(probe)):
            if insn.mnemonic.lower() == "push" and "lr" in insn.op_str.lower():
                func_start = probe
                break
        if func_start != start_off:
            break
    
    # Search forward for POP {... PC} or BX LR
    func_end = start_off + 256  # default
    region = data[func_start:func_start + 512]
    found_return = False
    for insn in md.disasm(region, off_to_addr(func_start)):
        if (insn.mnemonic.lower() == "pop" and "pc" in insn.op_str.lower()) or \
           (insn.mnemonic.lower() == "bx" and "lr" in insn.op_str.lower()):
            func_end = insn.address - APP_BASE + insn.size
            found_return = True
            # Don't break - there might be more code in the function
            # But if we see another PUSH after a return, the function ended
        elif found_return and insn.mnemonic.lower() == "push" and "lr" in insn.op_str.lower():
            func_end = insn.address - APP_BASE
            break
    
    return func_start, func_end


def disasm_function_at(data, known_off, label=""):
    """Disassemble a complete function containing the given offset."""
    start, end = find_function_bounds(data, known_off)
    length = end - start
    return disasm_region(data, start, length, label)


def scan_for_protocol_functions(data, name):
    """Find and disassemble all protocol-related functions."""
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    
    print(f"\n{'#'*78}")
    print(f"{'#'*78}")
    print(f"##  DEEP DISASSEMBLY: {name}")
    print(f"{'#'*78}")
    print(f"{'#'*78}")
    
    # ── 1. Find packet builder (writes 0x5A then 0xA5 sequentially) ──
    print(f"\n\n  ╔══════════════════════════════════════════════════════════════╗")
    print(f"  ║  SECTION 1: PACKET BUILDER FUNCTIONS                        ║")
    print(f"  ╚══════════════════════════════════════════════════════════════╝")
    
    builders_found = 0
    i = 0
    while i < len(data) - 10:
        # Look for: movs Rx, #0x5A followed by strb, then movs Ry, #0xA5, strb
        region = data[i:i+20]
        md2 = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
        insns = list(md2.disasm(region, off_to_addr(i)))
        
        if len(insns) >= 4:
            # Pattern: movs rX, #0x5a; strb rX, [rY]; movs rX, #0xa5; strb rX, [rY, #1]
            for j in range(len(insns) - 3):
                i0, i1, i2, i3 = insns[j], insns[j+1], insns[j+2], insns[j+3]
                if (i0.mnemonic == "movs" and "#0x5a" in i0.op_str) and \
                   (i1.mnemonic == "strb") and \
                   (i2.mnemonic == "movs" and "#0xa5" in i2.op_str) and \
                   (i3.mnemonic == "strb"):
                    builders_found += 1
                    off = insns[j].address - APP_BASE
                    disasm_function_at(data, off, f"packet_builder_{builders_found} ({name})")
                    i = off + 60  # skip past this function
                    break
            else:
                i += 2
                continue
            continue
        i += 2
    
    if builders_found == 0:
        print("  [!] No packet builder functions found")
    
    # ── 2. Find protocol parser (CMP with 0x5A, 0xA5 state machine) ──
    print(f"\n\n  ╔══════════════════════════════════════════════════════════════╗")
    print(f"  ║  SECTION 2: PROTOCOL PARSER / STATE MACHINE                 ║")
    print(f"  ╚══════════════════════════════════════════════════════════════╝")
    
    parsers_found = 0
    i = 0
    while i < len(data) - 10:
        region = data[i:i+16]
        md2 = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
        insns = list(md2.disasm(region, off_to_addr(i)))
        
        if len(insns) >= 2:
            for j in range(len(insns) - 1):
                # Pattern: cmp rX, #0x5a ... cmp rX, #0xa5 nearby
                if insns[j].mnemonic == "cmp" and "#0x5a" in insns[j].op_str:
                    # Check if 0xa5 comparison is nearby (within ~12 bytes)
                    search_end = min(len(data), i + 20)
                    search_region = data[i:search_end]
                    md3 = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
                    search_insns = list(md3.disasm(search_region, off_to_addr(i)))
                    has_a5 = any(si.mnemonic == "cmp" and "#0xa5" in si.op_str for si in search_insns)
                    
                    if has_a5:
                        parsers_found += 1
                        off = insns[j].address - APP_BASE
                        # Disassemble larger region - parsers are bigger
                        start, end = find_function_bounds(data, off)
                        # Parser functions can be large, extend
                        end = min(len(data), max(end, off + 200))
                        disasm_region(data, start, end - start, f"protocol_parser_{parsers_found} ({name})")
                        i = off + 80
                        break
            else:
                i += 2
                continue
            continue
        i += 2
    
    # ── 3. Find checksum calculation ──
    print(f"\n\n  ╔══════════════════════════════════════════════════════════════╗")
    print(f"  ║  SECTION 3: CHECKSUM CALCULATION                            ║")
    print(f"  ╚══════════════════════════════════════════════════════════════╝")
    
    # Look for: sum loop followed by XOR or MVN (NOT) with 0xFFFF
    # Also look for: adds in a loop with ldrb (byte-by-byte sum)
    # Strategy: find 0xFFFF as a literal pool constant, then trace back to the function
    ffff_bytes = struct.pack("<I", 0x0000FFFF)
    ffff_offsets = []
    off = 0
    while True:
        idx = data.find(ffff_bytes, off)
        if idx == -1:
            break
        ffff_offsets.append(idx)
        off = idx + 1
    
    # Also find 0xFFFF as 16-bit
    ffff16 = struct.pack("<H", 0xFFFF)
    # Look for mvn (bitwise NOT) instructions near loop patterns
    checksums_found = 0
    md2 = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    md2.detail = True
    
    # Disassemble entire firmware looking for checksum-like patterns
    all_insns = list(md2.disasm(data, APP_BASE))
    
    for idx, insn in enumerate(all_insns):
        # Look for: mvns or eor with a register that might hold 0xFFFF
        if insn.mnemonic in ("mvns", "mvn"):
            # Check if there's a loop (adds + ldrb) nearby before this
            nearby_start = max(0, idx - 30)
            nearby = all_insns[nearby_start:idx]
            has_adds = any(i.mnemonic == "adds" for i in nearby)
            has_ldrb = any(i.mnemonic == "ldrb" for i in nearby)
            has_loop = any(i.mnemonic in ("blo", "bne", "blt", "bcc", "b.lo", "b.ne") for i in nearby)
            
            if has_adds and has_ldrb:
                checksums_found += 1
                off = insn.address - APP_BASE
                start, end = find_function_bounds(data, off)
                disasm_region(data, start, end - start, f"checksum_calc_{checksums_found} ({name})")
                if checksums_found >= 3:
                    break

    # Also look for uxth (mask to 16-bit) after eor/xor
    for idx, insn in enumerate(all_insns):
        if insn.mnemonic == "eors" or insn.mnemonic == "eor":
            if idx + 1 < len(all_insns) and all_insns[idx + 1].mnemonic == "uxth":
                checksums_found += 1
                off = insn.address - APP_BASE
                start, end = find_function_bounds(data, off)
                disasm_region(data, start, end - start, f"checksum_eor_{checksums_found} ({name})")
                if checksums_found >= 5:
                    break
    
    if checksums_found == 0:
        # Fallback: look for any function near 0xFFFF literal pool references
        for lp_off in ffff_offsets[:3]:
            print(f"\n  0xFFFF literal pool at offset 0x{lp_off:06X} (0x{off_to_addr(lp_off):08X})")
            # Functions that reference this literal pool entry are within ~1KB before it
            search_start = max(0, lp_off - 200)
            disasm_region(data, search_start, lp_off - search_start + 20, 
                         f"near_ffff_literal ({name})")
    
    # ── 4. Find UART init / configuration ──
    print(f"\n\n  ╔══════════════════════════════════════════════════════════════╗")
    print(f"  ║  SECTION 4: UART INITIALIZATION                             ║")
    print(f"  ╚══════════════════════════════════════════════════════════════╝")
    
    # Look for BRR register writes (0x0271 for 115200 @ 72MHz)
    brr_val = struct.pack("<H", 0x0271)
    brr_off = data.find(brr_val)
    if brr_off != -1:
        # This might be in a literal pool or inline
        # Search backwards for USART CR1 references
        start = max(0, brr_off - 128)
        disasm_function_at(data, start, f"uart_init ({name})")
    
    # ── 5. Find UART TX/RX handlers ──
    print(f"\n\n  ╔══════════════════════════════════════════════════════════════╗")
    print(f"  ║  SECTION 5: UART TX / RX HANDLERS                           ║")
    print(f"  ╚══════════════════════════════════════════════════════════════╝")
    
    # Find functions that reference USART_SR and USART_DR
    usart_sr_addrs = {
        "USART1": struct.pack("<I", 0x40013800),
        "USART2": struct.pack("<I", 0x40004400),
        "USART3": struct.pack("<I", 0x40004800),
    }
    
    tx_found = 0
    for usart_name, sr_bytes in usart_sr_addrs.items():
        sr_off = data.find(sr_bytes)
        if sr_off != -1:
            tx_found += 1
            # This is likely a literal pool entry referenced by a nearby function
            start = max(0, sr_off - 128)
            func_start, func_end = find_function_bounds(data, start)
            # If function is too far back, use sr_off-64 instead
            if sr_off - func_start > 200:
                func_start = max(0, sr_off - 64)
                func_end = sr_off + 32
            disasm_region(data, func_start, func_end - func_start,
                         f"uart_handler_{usart_name} ({name})")

    return {
        "builders": builders_found,
        "parsers": parsers_found,
        "checksums": checksums_found,
    }


def main():
    print("=" * 78)
    print("  Ninebot G30 Max — Deep Function Disassembly")
    print("  ARM Cortex-M3 Thumb Mode | Capstone Engine")
    print("=" * 78)
    
    # Focus on DRV_1.6.13 (newest, most complete) and DRV_1.2.6 for comparison
    firmwares = [
        ("DRV_1.6.13", os.path.join(BASE_DIR, "ESC-MotorController", "firmware", "DRV_1.6.13_Compat.bin")),
        ("DRV_1.2.6", os.path.join(BASE_DIR, "ESC-MotorController", "firmware", "DRV_1.2.6.bin")),
        ("BLE_1.1.7", os.path.join(BASE_DIR, "BLE-Dashboard", "firmware", "BLE_1.1.7.bin")),
        ("BMS_1.7.4.5", os.path.join(BASE_DIR, "BMS-BatteryManagement", "firmware", "BMS_1.7.4.5.bin")),
    ]
    
    for fw_name, fw_path in firmwares:
        if not os.path.exists(fw_path):
            print(f"  [!] Not found: {fw_path}")
            continue
        data = load(fw_path)
        print(f"\n  Loaded {fw_name}: {len(data)} bytes")
        scan_for_protocol_functions(data, fw_name)
    
    print("\n\n[Done] Deep disassembly complete.")


if __name__ == "__main__":
    main()
