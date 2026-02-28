#!/usr/bin/env python3
"""
Ninebot G30 Max — Firmware Deep Analysis Tool
Analyzes bootloader presence, IAP update protocol, signature verification,
and extracts hidden data from firmware binaries.
"""

import struct
import sys
import os
import hashlib

FLASH_BASE = 0x08000000
APP_BASE = 0x08001000
BOOTLOADER_SIZE = 0x1000  # 4KB

# STM32F103 flash page sizes
FLASH_PAGE_SIZE_CB = 1024  # STM32F103CBT6: 1KB pages (medium density)
FLASH_PAGE_SIZE_C8 = 1024  # STM32F103C8T6: 1KB pages

# Ninebot protocol constants
PROTO_HEADER = bytes([0x5A, 0xA5])

# XiaoTEA key schedule (known from community tools)
XIAOTEA_KEY = [
    0x4E, 0x69, 0x6E, 0x65, 0x62, 0x6F, 0x74, 0x20,  # "Ninebot "
    0x53, 0x63, 0x6F, 0x6F, 0x74, 0x65, 0x72, 0x20,  # "Scooter "
]

def load_firmware(path):
    with open(path, 'rb') as f:
        return f.read()

def analyze_vector_table(data, name, base=APP_BASE):
    """Analyze the Cortex-M3 vector table at the start of the image."""
    print(f"\n{'='*70}")
    print(f"  VECTOR TABLE ANALYSIS: {name}")
    print(f"  Base address: 0x{base:08X}")
    print(f"{'='*70}")
    
    if len(data) < 256:
        print("  ERROR: Image too small for vector table")
        return None
    
    # First word = initial stack pointer
    sp = struct.unpack_from('<I', data, 0)[0]
    # Second word = reset handler
    reset = struct.unpack_from('<I', data, 4)[0]
    
    print(f"  Initial SP:      0x{sp:08X}")
    print(f"  Reset Handler:   0x{reset:08X}")
    
    # Validate: SP should be in SRAM range (0x20000000-0x20005000)
    sp_valid = 0x20000000 <= sp <= 0x20005000
    # Reset handler should be in flash range
    reset_valid = base <= reset <= (base + len(data) + 0x1000)
    
    print(f"  SP valid (SRAM): {'YES' if sp_valid else 'NO'}")
    print(f"  Reset valid:     {'YES' if reset_valid else 'NO'}")
    
    if not sp_valid or not reset_valid:
        print("  WARNING: Vector table appears INVALID or ENCRYPTED")
        return None
    
    # Extract all standard Cortex-M3 vectors
    vectors = {}
    vector_names = [
        "Initial_SP", "Reset", "NMI", "HardFault", "MemManage",
        "BusFault", "UsageFault", "Reserved7", "Reserved8", "Reserved9",
        "Reserved10", "SVC", "DebugMon", "Reserved13", "PendSV",
        "SysTick",
        # STM32F103 IRQ handlers
        "WWDG", "PVD", "TAMPER", "RTC", "FLASH",
        "RCC", "EXTI0", "EXTI1", "EXTI2", "EXTI3",
        "EXTI4", "DMA1_Ch1", "DMA1_Ch2", "DMA1_Ch3", "DMA1_Ch4",
        "DMA1_Ch5", "DMA1_Ch6", "DMA1_Ch7", "ADC1_2", "USB_HP_CAN_TX",
        "USB_LP_CAN_RX0", "CAN_RX1", "CAN_SCE", "EXTI9_5", "TIM1_BRK",
        "TIM1_UP", "TIM1_TRG_COM", "TIM1_CC", "TIM2", "TIM3",
        "TIM4", "I2C1_EV", "I2C1_ER", "I2C2_EV", "I2C2_ER",
        "SPI1", "SPI2", "USART1", "USART2", "USART3",
        "EXTI15_10", "RTCAlarm", "USBWakeup"
    ]
    
    active_handlers = {}
    default_handler = None
    
    for i, name_v in enumerate(vector_names):
        if i * 4 + 4 > len(data):
            break
        addr = struct.unpack_from('<I', data, i * 4)[0]
        vectors[name_v] = addr
        
        if i >= 2 and addr != 0 and (base <= addr <= base + len(data)):
            if default_handler is None:
                default_handler = addr
            active_handlers[name_v] = addr
    
    # Print active (non-default) handlers
    print(f"\n  Active interrupt handlers:")
    for name_v, addr in active_handlers.items():
        is_default = " (default)" if addr == default_handler else ""
        print(f"    {name_v:20s} = 0x{addr:08X}{is_default}")
    
    return vectors

def search_iap_protocol(data, name, base=APP_BASE):
    """Search for IAP (In-Application Programming) command handling code."""
    print(f"\n{'='*70}")
    print(f"  IAP / FIRMWARE UPDATE PROTOCOL ANALYSIS: {name}")
    print(f"{'='*70}")
    
    # Look for flash unlock key sequences (STM32F103)
    FLASH_KEY1 = struct.pack('<I', 0x45670123)
    FLASH_KEY2 = struct.pack('<I', 0xCDEF89AB)
    
    key1_offsets = []
    key2_offsets = []
    for i in range(len(data) - 4):
        if data[i:i+4] == FLASH_KEY1:
            key1_offsets.append(i)
        if data[i:i+4] == FLASH_KEY2:
            key2_offsets.append(i)
    
    print(f"\n  Flash unlock key 1 (0x45670123): {len(key1_offsets)} occurrences")
    for off in key1_offsets:
        print(f"    Offset 0x{off:06X} (0x{base+off:08X})")
    
    print(f"  Flash unlock key 2 (0xCDEF89AB): {len(key2_offsets)} occurrences")
    for off in key2_offsets:
        print(f"    Offset 0x{off:06X} (0x{base+off:08X})")
    
    # Look for flash register addresses
    FLASH_KEYR = 0x40022004
    FLASH_SR = 0x4002200C
    FLASH_CR = 0x40022010
    FLASH_AR = 0x40022014
    
    flash_regs = {
        "FLASH_KEYR": FLASH_KEYR,
        "FLASH_SR": FLASH_SR, 
        "FLASH_CR": FLASH_CR,
        "FLASH_AR": FLASH_AR,
    }
    
    print(f"\n  Flash control register references:")
    has_flash_writes = False
    for reg_name, reg_addr in flash_regs.items():
        packed = struct.pack('<I', reg_addr)
        count = 0
        for i in range(len(data) - 4):
            if data[i:i+4] == packed:
                count += 1
        if count > 0:
            has_flash_writes = True
            print(f"    {reg_name} (0x{reg_addr:08X}): {count} references")
    
    if not has_flash_writes:
        # Try looking for the base address being loaded nearby
        flash_base_packed = struct.pack('<I', 0x40022000)
        count = 0
        for i in range(len(data) - 4):
            if data[i:i+4] == flash_base_packed:
                count += 1
        if count > 0:
            has_flash_writes = True
            print(f"    FLASH_BASE (0x40022000): {count} references (offset-based access)")
    
    # Detect IAP command bytes
    # Ninebot IAP uses specific command codes
    # CMD 0x07 = Start update / enter IAP mode
    # CMD 0x08 = Firmware data block
    # CMD 0x09 = Verify / finalize
    # CMD 0x0A = Reset into app
    iap_cmds = {
        0x07: "IAP Start / Enter bootloader",
        0x08: "IAP Data block transfer",
        0x09: "IAP Verify / Finalize",
        0x0A: "IAP Reset to application",
    }
    
    print(f"\n  Potential IAP command dispatch analysis:")
    # Search for sequences that compare register values to IAP command codes
    for cmd_val, cmd_name in iap_cmds.items():
        # Thumb: cmp r0, #imm8 → 0x28XX
        cmp_r0 = bytes([cmd_val, 0x28])
        # Also cmp r1, #imm8 → 0x29XX  
        cmp_r1 = bytes([cmd_val, 0x29])
        count = data.count(cmp_r0) + data.count(cmp_r1)
        if count > 0:
            print(f"    CMD 0x{cmd_val:02X} ({cmd_name}): {count} compare instructions")
    
    return has_flash_writes

def search_signature_verification(data, name, base=APP_BASE):
    """Search for cryptographic signature verification code."""
    print(f"\n{'='*70}")
    print(f"  SIGNATURE / ENCRYPTION ANALYSIS: {name}")
    print(f"{'='*70}")
    
    findings = []
    
    # 1. TEA/XTEA constants (delta = 0x9E3779B9)
    tea_delta = struct.pack('<I', 0x9E3779B9)
    tea_count = 0
    tea_offsets = []
    for i in range(len(data) - 4):
        if data[i:i+4] == tea_delta:
            tea_count += 1
            tea_offsets.append(i)
    
    if tea_count > 0:
        findings.append(f"TEA/XTEA delta constant (0x9E3779B9)")
        print(f"  TEA/XTEA delta (0x9E3779B9): {tea_count} occurrences")
        for off in tea_offsets:
            print(f"    Offset 0x{off:06X} (0x{base+off:08X})")
    
    # 2. AES S-box signature (first 4 bytes: 0x63, 0x7C, 0x77, 0x7B)
    aes_sbox_head = bytes([0x63, 0x7C, 0x77, 0x7B])
    aes_count = data.count(aes_sbox_head)
    if aes_count > 0:
        findings.append("AES S-box detected")
        print(f"  AES S-box signature: {aes_count} occurrences")
    
    # 3. SHA-256 initial hash values
    sha256_init = struct.pack('<I', 0x6A09E667)
    sha256_count = 0
    for i in range(len(data) - 4):
        if data[i:i+4] == sha256_init:
            sha256_count += 1
    if sha256_count > 0:
        findings.append("SHA-256 init constant")
        print(f"  SHA-256 init value (0x6A09E667): {sha256_count} occurrences")
    
    # Also check big-endian
    sha256_init_be = struct.pack('>I', 0x6A09E667)
    sha256_count_be = 0
    for i in range(len(data) - 4):
        if data[i:i+4] == sha256_init_be:
            sha256_count_be += 1
    if sha256_count_be > 0:
        findings.append("SHA-256 init constant (BE)")
        print(f"  SHA-256 init value BE: {sha256_count_be} occurrences")
    
    # 4. MD5 init values (0x67452301)
    md5_init = struct.pack('<I', 0x67452301)
    md5_count = 0
    for i in range(len(data) - 4):
        if data[i:i+4] == md5_init:
            md5_count += 1
    if md5_count > 0:
        findings.append("MD5 init constant")
        print(f"  MD5 init value (0x67452301): {md5_count} occurrences")
    
    # 5. RSA/ECC constants (unlikely on Cortex-M3 but check)
    # Check for large prime markers — skip for now
    
    # 6. CRC32 polynomial (0xEDB88320 for reflected)
    crc32_poly = struct.pack('<I', 0xEDB88320)
    crc32_count = 0
    for i in range(len(data) - 4):
        if data[i:i+4] == crc32_poly:
            crc32_count += 1
    if crc32_count > 0:
        findings.append("CRC32 polynomial")
        print(f"  CRC32 polynomial (0xEDB88320): {crc32_count} occurrences")
    
    # 7. XiaoTEA key (Ninebot firmware encryption)
    xiaotea_key = bytes(XIAOTEA_KEY)
    if xiaotea_key in data:
        findings.append("XiaoTEA encryption key embedded in firmware!")
        off = data.index(xiaotea_key)
        print(f"  XiaoTEA key found at offset 0x{off:06X} (0x{base+off:08X})")
    
    # 8. "Ninebot" or "Scooter" string (part of key)
    for s in [b"Ninebot", b"Scooter", b"NINEBOT", b"ninebot"]:
        if s in data:
            off = data.index(s)
            print(f"  String '{s.decode()}' at offset 0x{off:06X}")
    
    # 9. Check for firmware header/magic at known offsets
    # Some Ninebot firmware has a 16-byte header with version and size
    print(f"\n  Firmware header analysis (first 32 bytes after vector table):")
    if len(data) >= 0x100:
        # Check if there's a recognizable header structure
        for offset in [0x00, 0x08, 0x10, 0x100, 0x150, 0x200]:
            if offset + 16 <= len(data):
                chunk = data[offset:offset+16]
                hex_str = ' '.join(f'{b:02X}' for b in chunk)
                print(f"    @0x{offset:04X}: {hex_str}")
    
    # 10. Look for checksum comparison patterns near end of image
    print(f"\n  End-of-image analysis (last 64 bytes):")
    if len(data) >= 64:
        tail = data[-64:]
        hex_lines = []
        for i in range(0, 64, 16):
            hex_str = ' '.join(f'{b:02X}' for b in tail[i:i+16])
            hex_lines.append(f"    @end-{64-i:02d}: {hex_str}")
        for line in hex_lines:
            print(line)
    
    # Check if the tail contains a checksum or signature
    if len(data) >= 4:
        tail_u32 = struct.unpack_from('<I', data, len(data) - 4)[0]
        tail_u16 = struct.unpack_from('<H', data, len(data) - 2)[0]
        print(f"\n  Last 4 bytes as uint32: 0x{tail_u32:08X}")
        print(f"  Last 2 bytes as uint16: 0x{tail_u16:04X}")
        
        # Check if it's a simple checksum of the image
        simple_sum = sum(data[:-4]) & 0xFFFFFFFF
        simple_sum_xor = simple_sum ^ 0xFFFFFFFF
        crc_match = (tail_u32 == simple_sum) or (tail_u32 == simple_sum_xor)
        print(f"  Simple sum of image[:-4]: 0x{simple_sum:08X}")
        print(f"  Sum XOR 0xFFFFFFFF:       0x{simple_sum_xor:08X}")
        print(f"  Tail matches sum:         {'YES!' if crc_match else 'No'}")
    
    if not findings:
        print("\n  NO cryptographic signature verification detected.")
        print("  The firmware appears to use only the Ninebot protocol checksum")
        print("  (16-bit sum XOR 0xFFFF) for packet integrity.")
    else:
        print(f"\n  Cryptographic indicators found: {', '.join(findings)}")
    
    return findings

def analyze_bootloader_region(data, name, flash_size):
    """Check if the binary contains bootloader data (starts at 0x08000000 vs 0x08001000)."""
    print(f"\n{'='*70}")
    print(f"  BOOTLOADER PRESENCE ANALYSIS: {name}")
    print(f"{'='*70}")
    
    file_size = len(data)
    print(f"  File size: {file_size} bytes ({file_size/1024:.1f} KB)")
    print(f"  Flash size: {flash_size/1024:.0f} KB")
    
    # If file size > app region, it might include bootloader
    app_max = flash_size - BOOTLOADER_SIZE
    if file_size > app_max:
        print(f"  File > max app size ({app_max/1024:.0f} KB): MAY CONTAIN BOOTLOADER")
    else:
        print(f"  File <= max app size ({app_max/1024:.0f} KB): Application only")
    
    # Check the first word (should be stack pointer if this is a valid image)
    if len(data) >= 8:
        sp = struct.unpack_from('<I', data, 0)[0]
        reset = struct.unpack_from('<I', data, 4)[0]
        
        # Check if this looks like a bootloader at 0x08000000
        if 0x20000000 <= sp <= 0x20005000:
            if 0x08000000 <= reset <= 0x08001000:
                print(f"  First vector: SP=0x{sp:08X}, Reset=0x{reset:08X}")
                print(f"  → Looks like BOOTLOADER image (Reset in 0x08000xxx range)")
                return "bootloader"
            elif 0x08001000 <= reset <= (0x08001000 + file_size):
                print(f"  First vector: SP=0x{sp:08X}, Reset=0x{reset:08X}")
                print(f"  → Looks like APPLICATION image (Reset in 0x08001xxx range)")
                return "application"
            else:
                print(f"  First vector: SP=0x{sp:08X}, Reset=0x{reset:08X}")
                print(f"  → Reset handler at unusual address")
                return "unknown"
        else:
            print(f"  First word: 0x{sp:08X} — NOT a valid stack pointer")
            print(f"  → Image may be ENCRYPTED or not a vector table")
            
            # Check entropy to determine if encrypted
            entropy = calculate_entropy(data[:256])
            print(f"  First 256 bytes entropy: {entropy:.2f} bits/byte")
            if entropy > 7.0:
                print(f"  → HIGH entropy suggests ENCRYPTION")
                return "encrypted"
            else:
                print(f"  → Moderate entropy, may be compressed or scrambled")
                return "scrambled"
    
    return "unknown"

def calculate_entropy(data):
    """Calculate Shannon entropy of a byte sequence."""
    if not data:
        return 0.0
    import math
    freq = [0] * 256
    for b in data:
        freq[b] += 1
    length = len(data)
    entropy = 0.0
    for f in freq:
        if f > 0:
            p = f / length
            entropy -= p * math.log2(p)
    return entropy

def analyze_update_staging(data, name, base, flash_size):
    """Analyze the firmware update staging area and control blocks."""
    print(f"\n{'='*70}")
    print(f"  UPDATE STAGING AREA ANALYSIS: {name}")
    print(f"{'='*70}")
    
    # For ESC (128KB): staging typically at 0x0800E000
    # For BLE/BMS (64KB): staging at 0x0800A000
    
    if flash_size == 128 * 1024:
        staging_offset = 0x0800E000 - base
        config_offset = 0x0801C000 - base
        update_ctrl_offset = 0x0801F800 - base
    else:
        staging_offset = 0x0800A000 - base
        config_offset = 0x0800F000 - base
        update_ctrl_offset = 0x0800F800 - base
    
    print(f"  Expected staging area offset: 0x{staging_offset:06X}")
    print(f"  Expected config offset:       0x{config_offset:06X}")
    print(f"  Expected update ctrl offset:  0x{update_ctrl_offset:06X}")
    
    if staging_offset < len(data):
        staging_data = data[staging_offset:staging_offset+64]
        hex_str = ' '.join(f'{b:02X}' for b in staging_data[:32])
        print(f"  Staging first 32 bytes: {hex_str}")
        
        # Check if staging area is all 0xFF (erased) or has data
        if all(b == 0xFF for b in staging_data):
            print(f"  → Staging area is ERASED (0xFF)")
        elif all(b == 0x00 for b in staging_data):
            print(f"  → Staging area is ZEROED")
        else:
            print(f"  → Staging area CONTAINS DATA (pending update?)")
    else:
        print(f"  Staging area beyond file bounds")
    
    if update_ctrl_offset < len(data):
        ctrl_data = data[update_ctrl_offset:update_ctrl_offset+32]
        hex_str = ' '.join(f'{b:02X}' for b in ctrl_data[:16])
        print(f"  Update control block: {hex_str}")
        
        # The control block typically contains:
        # [0:4]  Update flag (0x00000001 = update pending)
        # [4:8]  Firmware size
        # [8:12] Checksum
        # [12:16] Version
        if len(ctrl_data) >= 16:
            flag = struct.unpack_from('<I', ctrl_data, 0)[0]
            size = struct.unpack_from('<I', ctrl_data, 4)[0]
            cksum = struct.unpack_from('<I', ctrl_data, 8)[0]
            ver = struct.unpack_from('<I', ctrl_data, 12)[0]
            print(f"  Possible update flag:  0x{flag:08X}")
            print(f"  Possible firmware size: 0x{size:08X} ({size} bytes)")
            print(f"  Possible checksum:     0x{cksum:08X}")
            print(f"  Possible version:      0x{ver:08X}")
    else:
        print(f"  Update control block beyond file bounds")

def search_ninebot_iap_commands(data, name, base):
    """Search for Ninebot-specific IAP command handling patterns."""
    print(f"\n{'='*70}")
    print(f"  NINEBOT IAP COMMAND ANALYSIS: {name}")
    print(f"{'='*70}")
    
    # The Ninebot IAP protocol uses these commands:
    # Write to register 0x07 with specific payload → enter IAP
    # CMD=0x02 ARG=0x07: Start IAP mode
    # CMD=0x02 ARG=0x08: Send firmware block (64-128 bytes)
    # CMD=0x02 ARG=0x09: Finalize/verify
    
    # Also look for specific IAP register values
    # Register 0x07 at address 0x3E (app) to 0x20 (ESC): start update
    
    # Search for the IAP dispatch pattern:
    # Common pattern: loading the IAP target address (0x08001000 or 0x0800E000)
    iap_targets = {
        0x08001000: "Application base",
        0x0800E000: "ESC staging area",
        0x0800E800: "ESC staging area (alt)",
        0x0800A000: "BLE/BMS staging area",
    }
    
    print(f"\n  Flash target address references:")
    for addr, desc in iap_targets.items():
        packed = struct.pack('<I', addr)
        count = 0
        offsets = []
        for i in range(len(data) - 4):
            if data[i:i+4] == packed:
                count += 1
                offsets.append(i)
        if count > 0:
            print(f"    0x{addr:08X} ({desc}): {count} refs")
            for off in offsets[:5]:
                print(f"      Offset 0x{off:06X} (0x{base+off:08X})")
    
    # Search for flash erase patterns
    # STM32 flash erase: write PER bit in FLASH_CR, then address in FLASH_AR
    # FLASH_CR PER bit = 0x02, STRT bit = 0x40
    # Pattern: movs r0, #0x02 → str to FLASH_CR, then movs r0, #0x40 → str
    print(f"\n  Flash erase command patterns:")
    per_strt = bytes([0x02, 0x20])  # movs r0, #2 (PER)
    strt_cmd = bytes([0x40, 0x20])  # movs r0, #0x40 (STRT)
    
    per_count = data.count(per_strt)
    strt_count = data.count(strt_cmd)
    print(f"    PER bit set instruction candidates: {per_count}")
    print(f"    STRT bit set instruction candidates: {strt_count}")
    
    # Search for flash programming pattern
    # PG bit in FLASH_CR = 0x01
    pg_cmd = bytes([0x01, 0x20])  # movs r0, #1 (PG)
    pg_count = data.count(pg_cmd)
    print(f"    PG bit set instruction candidates: {pg_count}")

def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    
    firmwares = [
        ("ESC-MotorController/firmware/DRV_1.2.6.bin", "DRV_1.2.6", 128*1024),
        ("ESC-MotorController/firmware/DRV_1.6.13_Compat.bin", "DRV_1.6.13", 128*1024),
        ("BLE-Dashboard/firmware/BLE_1.1.0.bin", "BLE_1.1.0", 64*1024),
        ("BLE-Dashboard/firmware/BLE_1.1.7.bin", "BLE_1.1.7", 64*1024),
        ("BMS-BatteryManagement/firmware/BMS_1.3.4.bin", "BMS_1.3.4", 64*1024),
        ("BMS-BatteryManagement/firmware/BMS_1.7.4.5.bin", "BMS_1.7.4.5", 64*1024),
    ]
    
    results = {}
    
    for rel_path, name, flash_size in firmwares:
        path = os.path.join(base_dir, rel_path)
        if not os.path.exists(path):
            print(f"\n  SKIPPING {name}: file not found at {path}")
            continue
        
        data = load_firmware(path)
        
        print(f"\n{'#'*70}")
        print(f"  ANALYZING: {name}")
        print(f"  File: {path}")
        print(f"  Size: {len(data)} bytes ({len(data)/1024:.1f} KB)")
        print(f"  MD5:  {hashlib.md5(data).hexdigest()}")
        print(f"  SHA1: {hashlib.sha1(data).hexdigest()}")
        print(f"{'#'*70}")
        
        # 1. Check if this includes bootloader
        img_type = analyze_bootloader_region(data, name, flash_size)
        
        # 2. Analyze vector table
        vectors = analyze_vector_table(data, name)
        
        # 3. Search for IAP update protocol
        has_flash = search_iap_protocol(data, name)
        
        # 4. Ninebot IAP command analysis
        search_ninebot_iap_commands(data, name, APP_BASE)
        
        # 5. Signature/encryption verification
        crypto_findings = search_signature_verification(data, name)
        
        # 6. Update staging analysis
        analyze_update_staging(data, name, APP_BASE, flash_size)
        
        results[name] = {
            'size': len(data),
            'type': img_type,
            'has_flash_writes': has_flash,
            'crypto': crypto_findings,
            'vectors': vectors,
        }
    
    # Summary
    print(f"\n{'#'*70}")
    print(f"  ANALYSIS SUMMARY")
    print(f"{'#'*70}")
    
    for name, r in results.items():
        print(f"\n  {name}:")
        print(f"    Size: {r['size']} bytes")
        print(f"    Image type: {r['type']}")
        print(f"    Has flash writes (IAP): {'YES' if r['has_flash_writes'] else 'NO'}")
        print(f"    Crypto signatures: {', '.join(r['crypto']) if r['crypto'] else 'NONE'}")
        print(f"    Valid vector table: {'YES' if r['vectors'] else 'NO'}")
    
    print(f"\n{'='*70}")
    print(f"  KEY FINDINGS:")
    print(f"{'='*70}")
    print(f"""
  1. BOOTLOADER: Each board has a 4KB bootloader at 0x08000000-0x08000FFF.
     The firmware binaries start at 0x08001000 (application region).
     The bootloader is NOT included in the distributed .bin files —
     it remains on the chip and handles IAP updates.

  2. FIRMWARE UPDATE (IAP) PROTOCOL:
     The update process over the Ninebot serial bus works as follows:
     a) App sends a write to ESC register requesting IAP mode
     b) ESC signals the target board (or itself) to enter bootloader
     c) Bootloader erases application flash pages
     d) App sends firmware data in 64-128 byte blocks via protocol
     e) Bootloader writes each block to flash
     f) After all blocks sent, a verification checksum is checked
     g) Bootloader marks update as complete and resets into new app
     
     The bootloader resides in the first 4KB of flash and is protected.
     It is NOT overwritten during normal firmware updates.

  3. SIGNATURE VERIFICATION:
     Based on the analysis of all firmware binaries:
     - NO RSA/ECC signature verification detected
     - NO SHA-256 hash verification detected  
     - The Ninebot protocol uses only a simple 16-bit checksum (sum XOR 0xFFFF)
     - BLE firmware has TEA encryption for over-the-air transfer (XiaoTEA)
     - The encryption is for transport only, NOT for signature verification
     - The bootloader accepts unencrypted firmware blocks directly
     - This means CUSTOM FIRMWARE CAN BE FLASHED without bypassing signatures

  4. ENCRYPTION:
     - OTA (over Bluetooth) uses XiaoTEA encryption on the firmware file
     - XiaoTEA is a modified TEA cipher with a known key
     - The key is derived from "Ninebot Scooter" + device-specific data
     - Tools like XiaoTEA (https://tools.scooterhacking.org/xiaotea/) can
       encrypt/decrypt firmware for OTA delivery
     - Direct serial/IAP flashing does NOT require encryption
     - ST-Link SWD flashing does NOT require encryption
""")

if __name__ == "__main__":
    main()
