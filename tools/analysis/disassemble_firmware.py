"""
Ninebot G30 Max Firmware Disassembler & Protocol Verifier
=========================================================
Disassembles ARM Cortex-M3 firmware binaries and searches for
Ninebot protocol markers, UART configuration, checksum routines,
and register references to verify documented protocol.
"""

import os
import struct
import sys
from collections import defaultdict

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_LITTLE_ENDIAN
    HAS_CAPSTONE = True
except ImportError:
    HAS_CAPSTONE = False
    print("[WARN] capstone not available, using basic analysis only")

# ─── Configuration ───────────────────────────────────────────────────────────

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

FIRMWARE_FILES = {
    "DRV_1.2.6": os.path.join(BASE_DIR, "ESC-MotorController", "firmware", "DRV_1.2.6.bin"),
    "DRV_1.6.13": os.path.join(BASE_DIR, "ESC-MotorController", "firmware", "DRV_1.6.13_Compat.bin"),
    "BLE_1.1.0": os.path.join(BASE_DIR, "BLE-Dashboard", "firmware", "BLE_1.1.0.bin"),
    "BLE_1.1.7": os.path.join(BASE_DIR, "BLE-Dashboard", "firmware", "BLE_1.1.7.bin"),
    "BMS_1.3.4": os.path.join(BASE_DIR, "BMS-BatteryManagement", "firmware", "BMS_1.3.4.bin"),
    "BMS_1.7.4.5": os.path.join(BASE_DIR, "BMS-BatteryManagement", "firmware", "BMS_1.7.4.5.bin"),
}

# STM32F103 base address for application firmware
APP_BASE_ADDR = 0x08001000
BOOTLOADER_BASE = 0x08000000

# Protocol constants we expect to find
PROTOCOL_HEADER = bytes([0x5A, 0xA5])
CHECKSUM_XOR = 0xFFFF

# STM32F103 peripheral register addresses
STM32_PERIPHERALS = {
    0x40013800: "USART1_SR",
    0x40013804: "USART1_DR",
    0x40013808: "USART1_BRR",
    0x4001380C: "USART1_CR1",
    0x40013810: "USART1_CR2",
    0x40013814: "USART1_CR3",
    0x40004400: "USART2_SR",
    0x40004404: "USART2_DR",
    0x40004408: "USART2_BRR",
    0x4000440C: "USART2_CR1",
    0x40004410: "USART2_CR2",
    0x40004414: "USART2_CR3",
    0x40004800: "USART3_SR",
    0x40004804: "USART3_DR",
    0x40004808: "USART3_BRR",
    0x4000480C: "USART3_CR1",
    0x40004810: "USART3_CR2",
    0x40004814: "USART3_CR3",
    0x40005400: "I2C1_CR1",
    0x40005404: "I2C1_CR2",
    0x40005408: "I2C1_OAR1",
    0x4000540C: "I2C1_OAR2",
    0x40005410: "I2C1_DR",
    0x40005414: "I2C1_SR1",
    0x40005418: "I2C1_SR2",
    0x4000541C: "I2C1_CCR",
    0x40005800: "I2C2_CR1",
    0x40005804: "I2C2_CR2",
    0x40005810: "I2C2_DR",
    0x40012C00: "TIM1_CR1",
    0x40012C04: "TIM1_CR2",
    0x40000000: "TIM2_CR1",
    0x40000400: "TIM3_CR1",
    0x40010800: "GPIOA_CRL",
    0x40010804: "GPIOA_CRH",
    0x40010808: "GPIOA_IDR",
    0x4001080C: "GPIOA_ODR",
    0x40010C00: "GPIOB_CRL",
    0x40010C04: "GPIOB_CRH",
    0x40010C08: "GPIOB_IDR",
    0x40010C0C: "GPIOB_ODR",
    0x40021000: "RCC_CR",
    0x40021004: "RCC_CFGR",
    0x40021014: "RCC_APB2ENR",
    0x40021018: "RCC_APB1ENR",
    0x40022000: "FLASH_ACR",
    0x40010000: "AFIO_EVCR",
}

# Baud rate divisor values for 72MHz clock
# BRR = fck / baud. For 115200: 72000000/115200 = 625 = 0x271
BAUD_DIVISORS = {
    0x0271: 115200,
    0x1388: 9600,
    0x09C4: 19200,
    0x04E2: 38400,
    0x0138: 230400,
    0x009C: 460800,
    # For 36MHz APB1 clock: 36000000/115200 ≈ 312.5 → 0x0139 (with fraction)
    0x0139: "115200 (36MHz APB1)",
    0x0138: "115200 or 230400",
}


def load_firmware(filepath):
    """Load firmware binary file."""
    with open(filepath, "rb") as f:
        return f.read()


def analyze_vector_table(data, name):
    """Analyze ARM Cortex-M3 vector table at start of binary."""
    print(f"\n{'='*70}")
    print(f"  VECTOR TABLE ANALYSIS: {name}")
    print(f"{'='*70}")
    
    if len(data) < 0x100:
        print("  [!] File too small for vector table analysis")
        return {}
    
    vectors = {}
    vector_names = [
        "Initial SP", "Reset Handler", "NMI Handler", "HardFault Handler",
        "MemManage Handler", "BusFault Handler", "UsageFault Handler",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "SVCall Handler", "Debug Monitor", "Reserved",
        "PendSV Handler", "SysTick Handler",
        # External interrupts
        "WWDG", "PVD", "TAMPER", "RTC", "FLASH", "RCC",
        "EXTI0", "EXTI1", "EXTI2", "EXTI3", "EXTI4",
        "DMA1_Ch1", "DMA1_Ch2", "DMA1_Ch3", "DMA1_Ch4", "DMA1_Ch5",
        "DMA1_Ch6", "DMA1_Ch7", "ADC1_2", "USB_HP/CAN_TX",
        "USB_LP/CAN_RX0", "CAN_RX1", "CAN_SCE", "EXTI9_5",
        "TIM1_BRK", "TIM1_UP", "TIM1_TRG_COM", "TIM1_CC",
        "TIM2", "TIM3", "TIM4", "I2C1_EV", "I2C1_ER",
        "I2C2_EV", "I2C2_ER", "SPI1", "SPI2",
        "USART1", "USART2", "USART3", "EXTI15_10",
    ]
    
    for i, vname in enumerate(vector_names[:min(len(vector_names), len(data)//4)]):
        addr = struct.unpack_from("<I", data, i * 4)[0]
        vectors[vname] = addr
        if addr != 0 and vname not in ("Reserved",):
            # Check if it looks like a valid code address (in flash range)
            if 0x08000000 <= addr <= 0x0801FFFF or addr == 0:
                # Mark Thumb bit
                thumb = addr & 1
                real_addr = addr & ~1
                if vname in ("USART1", "USART2", "USART3", "I2C1_EV", "I2C2_EV",
                             "TIM1_UP", "TIM1_CC", "SysTick Handler", "Reset Handler",
                             "DMA1_Ch1", "DMA1_Ch2", "DMA1_Ch3"):
                    print(f"  [{i:3d}] {vname:20s} = 0x{real_addr:08X} {'(Thumb)' if thumb else ''}")
    
    # Check USART interrupt handlers specifically
    print(f"\n  --- UART Interrupt Handlers ---")
    for uname in ("USART1", "USART2", "USART3"):
        if uname in vectors and vectors[uname] != 0:
            addr = vectors[uname] & ~1
            print(f"  {uname}: 0x{addr:08X} -> active (protocol handler likely here)")
        elif uname in vectors:
            print(f"  {uname}: not configured (0x00000000)")
    
    return vectors


def find_protocol_headers(data, name):
    """Search for 0x5A 0xA5 protocol header bytes in firmware."""
    print(f"\n{'='*70}")
    print(f"  PROTOCOL HEADER SEARCH (0x5A 0xA5): {name}")
    print(f"{'='*70}")
    
    occurrences = []
    offset = 0
    while True:
        idx = data.find(PROTOCOL_HEADER, offset)
        if idx == -1:
            break
        # Get surrounding context
        context_start = max(0, idx - 4)
        context_end = min(len(data), idx + 16)
        context = data[context_start:context_end]
        occurrences.append((idx, context))
        offset = idx + 1
    
    print(f"  Found {len(occurrences)} occurrences of 0x5A 0xA5")
    for idx, ctx in occurrences:
        addr = APP_BASE_ADDR + idx
        hex_ctx = " ".join(f"{b:02X}" for b in ctx)
        print(f"  Offset 0x{idx:06X} (addr 0x{addr:08X}): {hex_ctx}")
    
    return occurrences


def find_byte_constants(data, name):
    """Search for important protocol-related byte constants in code."""
    print(f"\n{'='*70}")
    print(f"  PROTOCOL CONSTANT SEARCH: {name}")
    print(f"{'='*70}")
    
    # Search for protocol addresses as immediate values in Thumb instructions
    # In Thumb, MOV Rd, #imm8 is common for loading small constants
    constants_to_find = {
        0x5A: "Protocol header byte 1 (0x5A)",
        0xA5: "Protocol header byte 2 (0xA5)",
        0x20: "ESC address (0x20)",
        0x21: "BLE address (0x21)",
        0x22: "BMS address (0x22)",
        0x3E: "App address (0x3E)",
        0x3F: "PC address (0x3F)",
    }
    
    results = {}
    for const_val, desc in constants_to_find.items():
        count = 0
        positions = []
        for i in range(len(data)):
            if data[i] == const_val:
                count += 1
                if len(positions) < 5:  # Just track first few
                    positions.append(i)
        results[const_val] = count
        
    for const_val, desc in constants_to_find.items():
        print(f"  0x{const_val:02X} ({desc}): {results[const_val]} raw occurrences")
    
    return results


def find_checksum_pattern(data, name):
    """Search for XOR 0xFFFF checksum pattern in firmware."""
    print(f"\n{'='*70}")
    print(f"  CHECKSUM PATTERN SEARCH (XOR 0xFFFF): {name}")
    print(f"{'='*70}")
    
    # 0xFFFF as a 16-bit little-endian value
    ffff_bytes = struct.pack("<H", 0xFFFF)
    
    occurrences = []
    offset = 0
    while True:
        idx = data.find(ffff_bytes, offset)
        if idx == -1:
            break
        occurrences.append(idx)
        offset = idx + 1
    
    # Also search for 0xFFFF as a 32-bit value (common in ARM immediate encoding)
    ffff_32 = struct.pack("<I", 0xFFFF)
    occ_32 = []
    offset = 0
    while True:
        idx = data.find(ffff_32, offset)
        if idx == -1:
            break
        occ_32.append(idx)
        offset = idx + 1
    
    print(f"  0xFFFF (16-bit LE): {len(occurrences)} occurrences")
    for idx in occurrences[:10]:
        addr = APP_BASE_ADDR + idx
        context = data[max(0,idx-2):min(len(data),idx+6)]
        hex_ctx = " ".join(f"{b:02X}" for b in context)
        print(f"    Offset 0x{idx:06X} (0x{addr:08X}): {hex_ctx}")
    
    print(f"  0x0000FFFF (32-bit LE): {len(occ_32)} occurrences")
    for idx in occ_32[:10]:
        addr = APP_BASE_ADDR + idx
        context = data[max(0,idx-2):min(len(data),idx+6)]
        hex_ctx = " ".join(f"{b:02X}" for b in context)
        print(f"    Offset 0x{idx:06X} (0x{addr:08X}): {hex_ctx}")
    
    return occurrences, occ_32


def find_peripheral_references(data, name):
    """Search for STM32 peripheral register addresses in firmware."""
    print(f"\n{'='*70}")
    print(f"  STM32 PERIPHERAL REGISTER REFERENCES: {name}")
    print(f"{'='*70}")
    
    found = {}
    for reg_addr, reg_name in sorted(STM32_PERIPHERALS.items()):
        # Search for the address as a 32-bit little-endian value (literal pool)
        addr_bytes = struct.pack("<I", reg_addr)
        positions = []
        offset = 0
        while True:
            idx = data.find(addr_bytes, offset)
            if idx == -1:
                break
            positions.append(idx)
            offset = idx + 1
        
        if positions:
            found[reg_name] = positions
    
    # Group by peripheral
    peripherals = defaultdict(list)
    for reg_name, positions in sorted(found.items()):
        periph = reg_name.split("_")[0]
        peripherals[periph].append((reg_name, positions))
    
    for periph, regs in sorted(peripherals.items()):
        print(f"\n  --- {periph} ---")
        for reg_name, positions in regs:
            pos_str = ", ".join(f"0x{p:06X}" for p in positions[:5])
            extra = f" (+{len(positions)-5} more)" if len(positions) > 5 else ""
            print(f"  {reg_name:20s}: {len(positions):2d} refs at {pos_str}{extra}")
    
    return found


def find_baud_rate(data, name):
    """Search for baud rate divisor values."""
    print(f"\n{'='*70}")
    print(f"  BAUD RATE CONFIGURATION SEARCH: {name}")
    print(f"{'='*70}")
    
    # For STM32F103 at 72MHz PCLK2 (USART1) or 36MHz PCLK1 (USART2/3):
    # BRR for USART1  @ 72MHz: 72000000/115200 = 625.0   → 0x0271
    # BRR for USART2/3 @ 36MHz: 36000000/115200 = 312.5  → mantissa=312=0x138, frac=8 → 0x1388? 
    # Actually BRR = mantissa<<4 | fraction
    # 312.5 → mantissa=19, frac=8.5 → BRR = 19*16+8 = 312+8 = nah
    # Actual: USARTDIV = fck/(16*baud) = 36000000/(16*115200) = 19.53125
    # Mantissa = 19 = 0x13, Fraction = 0.53125 * 16 = 8.5 ≈ 9 → BRR = 0x139
    # Or: USARTDIV = 72000000/(16*115200) = 39.0625
    # Mantissa = 39 = 0x27, Fraction = 0.0625 * 16 = 1 → BRR = 0x271
    
    baud_patterns = {
        0x0271: "115200 baud (USART1 @ 72MHz PCLK2)",
        0x0139: "115200 baud (USART2/3 @ 36MHz PCLK1)",
        0x0138: "115200 baud (USART2/3 @ 36MHz PCLK1, alt)",
        0x1388: "9600 baud (USART1 @ 72MHz)",
    }
    
    for brr_val, desc in baud_patterns.items():
        brr_bytes = struct.pack("<H", brr_val)
        positions = []
        offset = 0
        while True:
            idx = data.find(brr_bytes, offset)
            if idx == -1:
                break
            positions.append(idx)
            offset = idx + 1
        
        if positions:
            pos_str = ", ".join(f"0x{p:06X}" for p in positions[:8])
            print(f"  BRR=0x{brr_val:04X} ({desc}): {len(positions)} at {pos_str}")
    
    # Also search for the literal 115200 as a 32-bit value (might be passed to init function)
    for baud in [115200, 9600, 19200, 38400, 57600, 230400, 460800]:
        baud_bytes = struct.pack("<I", baud)
        positions = []
        offset = 0
        while True:
            idx = data.find(baud_bytes, offset)
            if idx == -1:
                break
            positions.append(idx)
            offset = idx + 1
        if positions:
            pos_str = ", ".join(f"0x{p:06X}" for p in positions[:5])
            print(f"  Literal {baud}: {len(positions)} at {pos_str}")


def find_strings(data, name, min_len=4):
    """Extract printable ASCII strings from firmware."""
    print(f"\n{'='*70}")
    print(f"  STRING EXTRACTION: {name}")
    print(f"{'='*70}")
    
    strings = []
    current = ""
    start = 0
    
    for i, b in enumerate(data):
        if 0x20 <= b <= 0x7E:
            if not current:
                start = i
            current += chr(b)
        else:
            if len(current) >= min_len:
                strings.append((start, current))
            current = ""
    
    if len(current) >= min_len:
        strings.append((start, current))
    
    # Filter for interesting strings
    interesting_keywords = [
        "uart", "usart", "serial", "baud", "protocol", "packet",
        "checksum", "crc", "ble", "bluetooth", "ninebot", "segway",
        "motor", "battery", "bms", "esc", "drv", "version", "error",
        "flash", "boot", "update", "firmware", "i2c", "spi",
        "adc", "pwm", "timer", "gpio", "dma",
        "g30", "max", "5aa5", "header",
        "stm32", "gd32", "nrf", "bq76",
        "cell", "voltage", "current", "temperature", "temp",
    ]
    
    print(f"\n  Total strings found (>={min_len} chars): {len(strings)}")
    print(f"\n  --- All extracted strings ---")
    for offset, s in strings:
        addr = APP_BASE_ADDR + offset
        # Highlight interesting ones
        is_interesting = any(kw in s.lower() for kw in interesting_keywords)
        marker = " <<<" if is_interesting else ""
        print(f"  0x{addr:08X}: \"{s}\"{marker}")
    
    return strings


def disassemble_region(data, offset, length, base_addr):
    """Disassemble a region of firmware using capstone."""
    if not HAS_CAPSTONE:
        return []
    
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    
    region = data[offset:offset + length]
    instructions = []
    
    for insn in md.disasm(region, base_addr + offset):
        instructions.append(insn)
    
    return instructions


def disassemble_around_offset(data, offset, name, context=32):
    """Disassemble code around a specific offset."""
    if not HAS_CAPSTONE:
        return
    
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    
    start = max(0, offset - context)
    end = min(len(data), offset + context)
    region = data[start:end]
    
    print(f"\n  Disassembly around 0x{APP_BASE_ADDR + offset:08X}:")
    for insn in md.disasm(region, APP_BASE_ADDR + start):
        marker = " <<<" if insn.address == APP_BASE_ADDR + offset else ""
        print(f"    0x{insn.address:08X}: {insn.mnemonic:8s} {insn.op_str}{marker}")


def find_protocol_handler(data, name):
    """Try to locate the protocol packet handler function."""
    print(f"\n{'='*70}")
    print(f"  PROTOCOL HANDLER ANALYSIS: {name}")
    print(f"{'='*70}")
    
    if not HAS_CAPSTONE:
        print("  [!] Capstone not available - skipping disassembly analysis")
        return
    
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    
    # Strategy: Find code that compares bytes against 0x5A and 0xA5
    # In Thumb mode, CMP Rn, #0x5A would be encoded as:
    # CMP instruction with immediate
    
    # Search for CMP instructions with protocol-relevant immediates
    # Thumb CMP Rn, #imm8: 0010 1nnn iiiiiiii -> byte pattern varies
    
    # Let's look for sequences where 0x5A and 0xA5 appear close together
    # as they would in a header validation routine
    
    candidate_offsets = []
    
    for i in range(len(data) - 20):
        # Look for 0x5A within a few bytes of 0xA5
        window = data[i:i+20]
        if 0x5A in window and 0xA5 in window:
            idx_5a = window.index(0x5A)
            idx_a5 = window.index(0xA5)
            # They should be close in code (within ~10 bytes = ~5 instructions)
            if abs(idx_5a - idx_a5) <= 10:
                # Check if this looks like code (not data)
                # Heuristic: check if there are valid Thumb instructions nearby
                candidate_offsets.append(i + min(idx_5a, idx_a5))
    
    # Deduplicate (keep unique regions)
    if candidate_offsets:
        deduped = [candidate_offsets[0]]
        for off in candidate_offsets[1:]:
            if off - deduped[-1] > 32:
                deduped.append(off)
        candidate_offsets = deduped
    
    print(f"  Found {len(candidate_offsets)} candidate protocol handler regions")
    
    # Disassemble around the most promising candidates
    for off in candidate_offsets[:5]:
        print(f"\n  --- Candidate at offset 0x{off:06X} (0x{APP_BASE_ADDR+off:08X}) ---")
        
        # Disassemble a window around this offset
        start = max(0, off - 16)
        end = min(len(data), off + 48)
        region = data[start:end]
        
        for insn in md.disasm(region, APP_BASE_ADDR + start):
            # Highlight comparisons with protocol bytes
            highlight = ""
            if "0x5a" in insn.op_str.lower() or "#0x5a" in insn.op_str.lower():
                highlight = " <<< HEADER BYTE 1 (0x5A)"
            elif "0xa5" in insn.op_str.lower() or "#0xa5" in insn.op_str.lower():
                highlight = " <<< HEADER BYTE 2 (0xA5)"
            elif "0x20" in insn.op_str.lower() and "cmp" in insn.mnemonic.lower():
                highlight = " <<< ESC ADDR?"
            elif "0x21" in insn.op_str.lower() and "cmp" in insn.mnemonic.lower():
                highlight = " <<< BLE ADDR?"
            elif "0x22" in insn.op_str.lower() and "cmp" in insn.mnemonic.lower():
                highlight = " <<< BMS ADDR?"
            
            print(f"    0x{insn.address:08X}: {insn.mnemonic:8s} {insn.op_str}{highlight}")


def analyze_interrupt_handlers(data, name):
    """Analyze USART interrupt handler code from vector table."""
    print(f"\n{'='*70}")
    print(f"  USART INTERRUPT HANDLER DISASSEMBLY: {name}")
    print(f"{'='*70}")
    
    if not HAS_CAPSTONE:
        print("  [!] Capstone not available")
        return
    
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    
    # USART1 IRQ = vector 37 (index 37+16=53), but in bare vector table:
    # USART1 = vector index 53 (0xD4)
    # USART2 = vector index 54 (0xD8)
    # USART3 = vector index 55 (0xDC)
    
    usart_vectors = {
        "USART1": 53,
        "USART2": 54,
        "USART3": 55,
    }
    
    for usart_name, vec_idx in usart_vectors.items():
        vec_offset = vec_idx * 4
        if vec_offset + 4 > len(data):
            continue
        
        handler_addr = struct.unpack_from("<I", data, vec_offset)[0]
        if handler_addr == 0:
            print(f"\n  {usart_name}: No handler (vector = 0)")
            continue
        
        handler_addr_clean = handler_addr & ~1  # Clear Thumb bit
        fw_offset = handler_addr_clean - APP_BASE_ADDR
        
        if fw_offset < 0 or fw_offset >= len(data):
            # Try with bootloader base
            fw_offset = handler_addr_clean - BOOTLOADER_BASE
            if fw_offset < 0 or fw_offset >= len(data):
                print(f"\n  {usart_name}: Handler at 0x{handler_addr_clean:08X} (outside firmware range)")
                continue
        
        print(f"\n  {usart_name} IRQ Handler at 0x{handler_addr_clean:08X}:")
        
        # Disassemble first 64 bytes of handler
        end = min(len(data), fw_offset + 128)
        region = data[fw_offset:end]
        
        insn_count = 0
        for insn in md.disasm(region, handler_addr_clean):
            highlight = ""
            op_lower = insn.op_str.lower()
            mn_lower = insn.mnemonic.lower()
            
            # Look for interesting patterns
            if "0x5a" in op_lower:
                highlight = " <<< Protocol header 0x5A"
            elif "0xa5" in op_lower:
                highlight = " <<< Protocol header 0xA5"
            elif any(f"0x{addr:x}" in op_lower for addr in [0x40013804, 0x40004404, 0x40004804]):
                highlight = " <<< USART data register"
            elif "bx" in mn_lower and "lr" in op_lower:
                highlight = " <<< Return"
            elif "pop" in mn_lower and "pc" in op_lower:
                highlight = " <<< Return"
            
            print(f"    0x{insn.address:08X}: {insn.mnemonic:8s} {insn.op_str}{highlight}")
            
            insn_count += 1
            if insn_count > 40:
                print("    ... (truncated)")
                break
            
            # Stop at function return
            if ("bx" in mn_lower and "lr" in op_lower) or \
               ("pop" in mn_lower and "pc" in op_lower):
                break


def analyze_firmware(fw_name, fw_path):
    """Complete analysis of a single firmware binary."""
    print(f"\n{'#'*70}")
    print(f"{'#'*70}")
    print(f"##  FIRMWARE ANALYSIS: {fw_name}")
    print(f"##  File: {fw_path}")
    print(f"{'#'*70}")
    print(f"{'#'*70}")
    
    if not os.path.exists(fw_path):
        print(f"  [!] File not found: {fw_path}")
        return
    
    data = load_firmware(fw_path)
    print(f"\n  File size: {len(data)} bytes ({len(data)/1024:.1f} KB)")
    print(f"  Base address: 0x{APP_BASE_ADDR:08X}")
    print(f"  End address:  0x{APP_BASE_ADDR + len(data):08X}")
    
    # MD5 hash
    import hashlib
    md5 = hashlib.md5(data).hexdigest()
    sha256 = hashlib.sha256(data).hexdigest()
    print(f"  MD5:    {md5}")
    print(f"  SHA256: {sha256}")
    
    # Run all analyses
    vectors = analyze_vector_table(data, fw_name)
    find_protocol_headers(data, fw_name)
    find_byte_constants(data, fw_name)
    find_checksum_pattern(data, fw_name)
    find_peripheral_references(data, fw_name)
    find_baud_rate(data, fw_name)
    find_strings(data, fw_name)
    
    if HAS_CAPSTONE:
        find_protocol_handler(data, fw_name)
        analyze_interrupt_handlers(data, fw_name)


def protocol_verification_summary(all_results):
    """Print summary of protocol verification findings."""
    print(f"\n{'#'*70}")
    print(f"  PROTOCOL VERIFICATION SUMMARY")
    print(f"{'#'*70}")
    
    print("""
  Documented Protocol Specification vs Firmware Evidence:
  
  ┌─────────────────────────────────┬──────────┬─────────────────────────────┐
  │ Protocol Feature                │ Status   │ Evidence                    │
  ├─────────────────────────────────┼──────────┼─────────────────────────────┤
  │ Header: 0x5A 0xA5              │ See logs │ Binary search for 5A A5     │
  │ UART @ 115200 8N1              │ See logs │ BRR register values         │
  │ Checksum: sum XOR 0xFFFF       │ See logs │ 0xFFFF constant presence    │
  │ Addresses: 0x20/0x21/0x22      │ See logs │ Byte constant search        │
  │ USART peripheral usage         │ See logs │ Register address references │
  │ I2C for BMS AFE                │ See logs │ I2C register references     │
  │ Cortex-M3 vector table         │ See logs │ Vector table analysis       │
  └─────────────────────────────────┴──────────┴─────────────────────────────┘
  
  See individual firmware analysis sections above for detailed evidence.
""")


def main():
    print("=" * 70)
    print("  Ninebot G30 Max Firmware Disassembler & Protocol Verifier")
    print("  ARM Cortex-M3 (STM32F103) Thumb Mode Analysis")
    print(f"  Capstone engine: {'Available' if HAS_CAPSTONE else 'NOT AVAILABLE'}")
    print("=" * 70)
    
    all_results = {}
    
    for fw_name, fw_path in FIRMWARE_FILES.items():
        analyze_firmware(fw_name, fw_path)
    
    protocol_verification_summary(all_results)
    
    print("\n[Done] Analysis complete.")


if __name__ == "__main__":
    main()
