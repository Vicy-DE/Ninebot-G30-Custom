#!/usr/bin/env python3
"""
Ninebot G30 Max — Custom Firmware Flasher

Serial IAP (In-Application Programming) tool for flashing custom firmware
to ESC, BLE, and BMS boards over the Ninebot serial protocol.

Usage:
    python ninebot_flasher.py --port COM3 --board esc --firmware DRV_custom.bin
    python ninebot_flasher.py --port /dev/ttyUSB0 --board bms --firmware BMS_1.7.4.5.bin
    python ninebot_flasher.py --port COM3 --board ble --firmware BLE_1.1.7.bin
    python ninebot_flasher.py --port COM3 --info           # Read scooter info
    python ninebot_flasher.py --port COM3 --dump-regs esc  # Dump all ESC registers

Requirements:
    pip install pyserial

Protocol Reference:
    See docs/iap-update-protocol.md and docs/protocol.md

Safety:
    - Always keep backup of stock firmware before flashing
    - BMS modifications can cause battery fires — verify firmware integrity
    - Do not interrupt flashing — bricked boards require ST-Link recovery
    - Verify checksum before flashing

Author: Ninebot G30 Custom Project
License: MIT
"""

import argparse
import hashlib
import os
import struct
import sys
import time
from typing import Optional, Tuple, List

try:
    import serial
except ImportError:
    print("ERROR: pyserial is required. Install with: pip install pyserial")
    sys.exit(1)


# =============================================================================
# Protocol Constants
# =============================================================================

HEADER = bytes([0x5A, 0xA5])

# Device addresses
ADDR_ESC  = 0x20
ADDR_BLE  = 0x21
ADDR_BMS  = 0x22
ADDR_BMS2 = 0x23
ADDR_APP  = 0x3E
ADDR_PC   = 0x3F

# Command types
CMD_READ          = 0x01
CMD_WRITE         = 0x02
CMD_READ_RESPONSE = 0x03
CMD_WRITE_ACK     = 0x05

# IAP registers (used during firmware update)
IAP_START    = 0x07  # Initiate firmware update
IAP_DATA     = 0x08  # Send firmware data block
IAP_VERIFY   = 0x09  # Verify written firmware
IAP_RESET    = 0x0A  # Reset into new firmware

# Board name to address mapping
BOARD_ADDR = {
    'esc': ADDR_ESC,
    'drv': ADDR_ESC,
    'ble': ADDR_BLE,
    'bms': ADDR_BMS,
    'bms2': ADDR_BMS2,
}

# Board name to firmware ID mapping
BOARD_FW_PREFIX = {
    'esc': 'DRV',
    'drv': 'DRV',
    'ble': 'BLE',
    'bms': 'BMS',
    'bms2': 'BMS',
}

# UART settings
BAUD_RATE   = 115200
DATA_BITS   = serial.EIGHTBITS
PARITY      = serial.PARITY_NONE
STOP_BITS   = serial.STOPBITS_ONE

# IAP timing
IAP_START_DELAY    = 1.0     # Seconds to wait after IAP start (flash erase)
IAP_BLOCK_DELAY    = 0.03    # Seconds between data blocks
IAP_ACK_TIMEOUT    = 3.0     # Seconds to wait for ACK
IAP_MAX_RETRIES    = 3       # Retries per block
IAP_BLOCK_SIZE     = 64      # Bytes per data block

# Known ESC register addresses for info reading
ESC_REGS = {
    0x10: ("Serial Number", 14, "ascii"),
    0x1A: ("Firmware Version", 2, "version"),
    0x20: ("Error Code", 2, "hex"),
    0x21: ("Warning Code", 2, "hex"),
    0x22: ("Status Flags", 2, "hex"),
    0x25: ("Battery %", 2, "percent"),
    0x26: ("Speed", 2, "speed"),
    0x2B: ("Temperature", 2, "temp"),
    0x34: ("Total Distance", 4, "distance"),
    0x3A: ("Battery Voltage", 2, "voltage"),
    0x3B: ("Battery Current", 2, "current"),
    0x75: ("Riding Mode", 1, "mode"),
    0xB0: ("Speed Limit", 2, "int"),
}

BLE_REGS = {
    0x10: ("Serial Number", 14, "ascii"),
    0x17: ("Firmware Version", 2, "version"),
    0x68: ("MAC Address", 6, "mac"),
    0x69: ("Model String", 16, "ascii"),
}

BMS_REGS = {
    0x10: ("Status", 2, "hex"),
    0x17: ("Temperature 1", 2, "temp"),
    0x18: ("Temperature 2", 2, "temp"),
    0x22: ("Remaining Capacity", 2, "mah"),
    0x24: ("Remaining %", 2, "percent"),
    0x25: ("Current", 2, "current_ma"),
    0x26: ("Voltage", 2, "voltage_mv"),
    0x30: ("Cell 1", 2, "voltage_mv"),
    0x31: ("Cell 2", 2, "voltage_mv"),
    0x32: ("Cell 3", 2, "voltage_mv"),
    0x33: ("Cell 4", 2, "voltage_mv"),
    0x34: ("Cell 5", 2, "voltage_mv"),
    0x35: ("Cell 6", 2, "voltage_mv"),
    0x36: ("Cell 7", 2, "voltage_mv"),
    0x37: ("Cell 8", 2, "voltage_mv"),
    0x38: ("Cell 9", 2, "voltage_mv"),
    0x39: ("Cell 10", 2, "voltage_mv"),
    0x66: ("Full Capacity", 2, "mah"),
    0x67: ("Cycle Count", 2, "int"),
}


# =============================================================================
# Ninebot Protocol Implementation
# =============================================================================

def calculate_checksum(data: bytes) -> int:
    """
    Calculate Ninebot protocol checksum.
    
    Sum all bytes, then XOR with 0xFFFF.
    Matches firmware implementation at DRV_1.6.13 @ 0x08002720.
    
    Args:
        data: Bytes from LEN field through end of payload.
    
    Returns:
        16-bit checksum value.
    """
    checksum = 0
    for byte in data:
        checksum += byte
        checksum &= 0xFFFF
    return checksum ^ 0xFFFF


def build_packet(src: int, dst: int, cmd: int, arg: int,
                 payload: bytes = b'') -> bytes:
    """
    Build a complete Ninebot protocol packet.
    
    Packet format:
        [0x5A] [0xA5] [LEN] [SRC] [DST] [CMD] [ARG] [PAYLOAD...] [CHK_LO] [CHK_HI]
    
    LEN = number of bytes from SRC through end of PAYLOAD = 4 + len(payload)
    
    Args:
        src: Source address
        dst: Destination address
        cmd: Command type
        arg: Register / argument byte
        payload: Payload data (0-242 bytes)
    
    Returns:
        Complete packet as bytes.
    """
    blen = 4 + len(payload)  # SRC + DST + CMD + ARG + payload
    
    # Build the checksummed region: LEN + SRC + DST + CMD + ARG + PAYLOAD
    body = bytes([blen, src, dst, cmd, arg]) + payload
    
    # Calculate checksum over the body
    chk = calculate_checksum(body)
    
    # Assemble full packet
    packet = HEADER + body + struct.pack('<H', chk)
    return packet


def parse_packet(data: bytes) -> Optional[dict]:
    """
    Parse a raw Ninebot protocol packet.
    
    Args:
        data: Raw bytes starting with 0x5A 0xA5.
    
    Returns:
        Dict with parsed fields, or None if invalid.
    """
    if len(data) < 9:
        return None
    
    if data[0] != 0x5A or data[1] != 0xA5:
        return None
    
    blen = data[2]
    if len(data) < blen + 5:  # header(2) + len(1) + body(blen) + checksum(2)
        return None
    
    total_len = blen + 5  # 2 (header) + 1 (len) + blen (body) + 2 (checksum)
    
    # Verify checksum
    body = data[2:2 + 1 + blen]  # LEN byte + SRC+DST+CMD+ARG+payload = 1 + blen
    chk_received = struct.unpack_from('<H', data, total_len - 2)[0]
    chk_computed = calculate_checksum(body)
    
    if chk_received != chk_computed:
        return None
    
    payload_len = blen - 4
    if payload_len < 0:
        return None
    
    return {
        'length': blen,
        'source': data[3],
        'destination': data[4],
        'command': data[5],
        'argument': data[6],
        'payload': data[7:7 + payload_len],
        'checksum': chk_received,
        'raw': data[:total_len],
    }


def format_register_value(raw: bytes, fmt: str) -> str:
    """Format a register value for display."""
    if fmt == "ascii":
        return raw.rstrip(b'\x00').decode('ascii', errors='replace')
    elif fmt == "version":
        if len(raw) >= 2:
            v = struct.unpack('<H', raw[:2])[0]
            major = (v >> 8) & 0xFF
            minor = (v >> 4) & 0x0F
            patch = v & 0x0F
            return f"{major}.{minor}.{patch} (0x{v:04X})"
        return "?"
    elif fmt == "hex":
        return '0x' + raw.hex().upper()
    elif fmt == "percent":
        if len(raw) >= 2:
            return f"{struct.unpack('<H', raw[:2])[0]}%"
        return "?"
    elif fmt == "speed":
        if len(raw) >= 2:
            v = struct.unpack('<H', raw[:2])[0]
            return f"{v * 0.001:.1f} km/h"
        return "?"
    elif fmt == "temp":
        if len(raw) >= 2:
            v = struct.unpack('<h', raw[:2])[0]
            return f"{v * 0.1:.1f} °C"
        return "?"
    elif fmt == "distance":
        if len(raw) >= 4:
            v = struct.unpack('<I', raw[:4])[0]
            return f"{v} m ({v/1000:.1f} km)"
        return "?"
    elif fmt == "voltage":
        if len(raw) >= 2:
            v = struct.unpack('<H', raw[:2])[0]
            return f"{v * 0.01:.2f} V"
        return "?"
    elif fmt == "voltage_mv":
        if len(raw) >= 2:
            v = struct.unpack('<H', raw[:2])[0]
            return f"{v} mV ({v/1000:.3f} V)"
        return "?"
    elif fmt == "current":
        if len(raw) >= 2:
            v = struct.unpack('<h', raw[:2])[0]
            return f"{v * 0.01:.2f} A"
        return "?"
    elif fmt == "current_ma":
        if len(raw) >= 2:
            v = struct.unpack('<h', raw[:2])[0]
            return f"{v} mA ({v/1000:.2f} A)"
        return "?"
    elif fmt == "mah":
        if len(raw) >= 2:
            v = struct.unpack('<H', raw[:2])[0]
            return f"{v} mAh"
        return "?"
    elif fmt == "mode":
        modes = {0: "Eco", 1: "D (Standard)", 2: "Sport"}
        if len(raw) >= 1:
            return modes.get(raw[0], f"Unknown ({raw[0]})")
        return "?"
    elif fmt == "mac":
        return ':'.join(f'{b:02X}' for b in raw[:6])
    elif fmt == "int":
        if len(raw) >= 2:
            return str(struct.unpack('<H', raw[:2])[0])
        elif len(raw) >= 1:
            return str(raw[0])
        return "?"
    else:
        return raw.hex()


# =============================================================================
# XiaoTEA Encryption / Decryption
# =============================================================================

def xiaotea_decrypt_block(block: bytes, key: bytes) -> bytes:
    """
    Decrypt a single 8-byte block using XiaoTEA.
    
    XiaoTEA is a modified TEA (Tiny Encryption Algorithm) used by
    Ninebot/Segway for OTA firmware encryption.
    
    Args:
        block: 8 bytes of encrypted data
        key: 16-byte encryption key
    
    Returns:
        8 bytes of decrypted data
    """
    assert len(block) == 8
    assert len(key) == 16
    
    v0, v1 = struct.unpack('<II', block)
    k = struct.unpack('<IIII', key)
    
    delta = 0x9E3779B9
    total = (delta * 32) & 0xFFFFFFFF  # TEA standard: 32 rounds
    
    for _ in range(32):
        v1 = (v1 - (((v0 << 4) + k[2]) ^ (v0 + total) ^ ((v0 >> 5) + k[3]))) & 0xFFFFFFFF
        v0 = (v0 - (((v1 << 4) + k[0]) ^ (v1 + total) ^ ((v1 >> 5) + k[1]))) & 0xFFFFFFFF
        total = (total - delta) & 0xFFFFFFFF
    
    return struct.pack('<II', v0, v1)


def xiaotea_encrypt_block(block: bytes, key: bytes) -> bytes:
    """Encrypt a single 8-byte block using XiaoTEA."""
    assert len(block) == 8
    assert len(key) == 16
    
    v0, v1 = struct.unpack('<II', block)
    k = struct.unpack('<IIII', key)
    
    delta = 0x9E3779B9
    total = 0
    
    for _ in range(32):
        total = (total + delta) & 0xFFFFFFFF
        v0 = (v0 + (((v1 << 4) + k[0]) ^ (v1 + total) ^ ((v1 >> 5) + k[1]))) & 0xFFFFFFFF
        v1 = (v1 + (((v0 << 4) + k[2]) ^ (v0 + total) ^ ((v0 >> 5) + k[3]))) & 0xFFFFFFFF
    
    return struct.pack('<II', v0, v1)


def xiaotea_decrypt(data: bytes, key: bytes) -> bytes:
    """Decrypt a full firmware image encrypted with XiaoTEA."""
    result = bytearray()
    for i in range(0, len(data), 8):
        block = data[i:i+8]
        if len(block) < 8:
            block = block + b'\x00' * (8 - len(block))
        result.extend(xiaotea_decrypt_block(bytes(block), key))
    return bytes(result)


def xiaotea_encrypt(data: bytes, key: bytes) -> bytes:
    """Encrypt a full firmware image with XiaoTEA for OTA distribution."""
    # Pad to 8-byte boundary
    if len(data) % 8 != 0:
        data = data + b'\x00' * (8 - (len(data) % 8))
    
    result = bytearray()
    for i in range(0, len(data), 8):
        block = data[i:i+8]
        result.extend(xiaotea_encrypt_block(bytes(block), key))
    return bytes(result)


# Default XiaoTEA key (model-specific — this is for G30/Max)
XIAOTEA_DEFAULT_KEY = bytes([
    0x4E, 0x69, 0x6E, 0x65, 0x62, 0x6F, 0x74, 0x20,  # "Ninebot "
    0x53, 0x63, 0x6F, 0x6F, 0x74, 0x65, 0x72, 0x20,  # "Scooter "
])


# =============================================================================
# NinebotFlasher Class
# =============================================================================

class NinebotFlasher:
    """
    Ninebot G30 Max firmware flasher.
    
    Communicates with the scooter over the Ninebot serial protocol.
    Supports reading registers, querying board info, and flashing
    custom firmware via the IAP (In-Application Programming) protocol.
    """
    
    def __init__(self, port: str, source_addr: int = ADDR_PC,
                 verbose: bool = False):
        """
        Initialize the flasher.
        
        Args:
            port: Serial port (e.g., 'COM3' or '/dev/ttyUSB0')
            source_addr: Our address on the bus (default: PC = 0x3F)
            verbose: Enable verbose packet logging
        """
        self.port = port
        self.source = source_addr
        self.verbose = verbose
        self.ser: Optional[serial.Serial] = None
    
    def connect(self) -> bool:
        """Open the serial connection."""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=BAUD_RATE,
                bytesize=DATA_BITS,
                parity=PARITY,
                stopbits=STOP_BITS,
                timeout=1.0,
                write_timeout=1.0,
            )
            # Flush any pending data
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            time.sleep(0.1)
            return True
        except serial.SerialException as e:
            print(f"ERROR: Cannot open {self.port}: {e}")
            return False
    
    def disconnect(self):
        """Close the serial connection."""
        if self.ser and self.ser.is_open:
            self.ser.close()
    
    def send_packet(self, dst: int, cmd: int, arg: int,
                    payload: bytes = b''):
        """Send a protocol packet."""
        pkt = build_packet(self.source, dst, cmd, arg, payload)
        
        if self.verbose:
            print(f"  TX [{len(pkt)}]: {pkt.hex(' ')}")
        
        self.ser.write(pkt)
        self.ser.flush()
    
    def receive_packet(self, timeout: float = IAP_ACK_TIMEOUT) -> Optional[dict]:
        """
        Receive and parse a protocol packet.
        
        Scans the serial input for a 0x5A 0xA5 header, then reads
        the complete packet based on the LEN field.
        
        Args:
            timeout: Maximum seconds to wait for a complete packet.
        
        Returns:
            Parsed packet dict, or None on timeout/error.
        """
        self.ser.timeout = timeout
        start_time = time.time()
        
        # Scan for header
        while time.time() - start_time < timeout:
            b = self.ser.read(1)
            if not b:
                continue
            if b[0] != 0x5A:
                continue
            
            b2 = self.ser.read(1)
            if not b2 or b2[0] != 0xA5:
                continue
            
            # Got header — read LEN byte
            blen_raw = self.ser.read(1)
            if not blen_raw:
                continue
            
            blen = blen_raw[0]
            if blen < 4 or blen > 246:  # Sanity check
                continue
            
            # Read remaining: SRC + DST + CMD + ARG + payload + checksum
            remaining = blen + 2  # body (blen bytes) + checksum (2 bytes)
            rest = self.ser.read(remaining)
            if len(rest) < remaining:
                continue
            
            # Assemble full packet
            raw = bytes([0x5A, 0xA5, blen]) + rest
            
            if self.verbose:
                print(f"  RX [{len(raw)}]: {raw.hex(' ')}")
            
            pkt = parse_packet(raw)
            if pkt:
                return pkt
        
        return None
    
    def read_register(self, board_addr: int, reg: int,
                      read_len: int = 2) -> Optional[bytes]:
        """
        Read a register from a board.
        
        Args:
            board_addr: Target board address (ADDR_ESC, ADDR_BLE, ADDR_BMS)
            reg: Register address to read
            read_len: Number of bytes to read
        
        Returns:
            Register data as bytes, or None on failure.
        """
        # Payload for read: number of bytes to read
        payload = struct.pack('<H', read_len)
        self.send_packet(board_addr, CMD_READ, reg, payload)
        
        # Wait for response (READ_RESPONSE)
        pkt = self.receive_packet(timeout=2.0)
        if pkt and pkt['command'] == CMD_READ_RESPONSE and pkt['argument'] == reg:
            return pkt['payload']
        
        return None
    
    def write_register(self, board_addr: int, reg: int,
                       data: bytes) -> bool:
        """
        Write data to a register on a board.
        
        Args:
            board_addr: Target board address
            reg: Register address to write
            data: Data to write
        
        Returns:
            True if write was acknowledged.
        """
        self.send_packet(board_addr, CMD_WRITE, reg, data)
        
        # Wait for ACK
        pkt = self.receive_packet(timeout=2.0)
        if pkt and pkt['command'] == CMD_WRITE_ACK and pkt['argument'] == reg:
            if pkt['payload'] and pkt['payload'][0] == 0x01:
                return True
        
        return False
    
    def read_board_info(self, board: str) -> dict:
        """
        Read basic information from a board.
        
        Args:
            board: Board name ('esc', 'ble', 'bms')
        
        Returns:
            Dict mapping register names to formatted values.
        """
        addr = BOARD_ADDR.get(board.lower())
        if addr is None:
            raise ValueError(f"Unknown board: {board}")
        
        # Select register map
        if board.lower() in ('esc', 'drv'):
            reg_map = ESC_REGS
        elif board.lower() == 'ble':
            reg_map = BLE_REGS
        elif board.lower() in ('bms', 'bms2'):
            reg_map = BMS_REGS
        else:
            reg_map = ESC_REGS
        
        info = {}
        for reg, (name, size, fmt) in reg_map.items():
            raw = self.read_register(addr, reg, size)
            if raw:
                info[name] = format_register_value(raw, fmt)
            else:
                info[name] = "<read failed>"
            time.sleep(0.05)  # Small delay between reads
        
        return info
    
    def dump_registers(self, board: str):
        """
        Dump all known registers from a board to stdout.
        
        Args:
            board: Board name ('esc', 'ble', 'bms')
        """
        print(f"\n{'='*50}")
        print(f"  Register Dump: {board.upper()}")
        print(f"{'='*50}")
        
        info = self.read_board_info(board)
        for name, value in info.items():
            print(f"  {name:25s}: {value}")
        
        print(f"{'='*50}\n")
    
    def validate_firmware(self, firmware_data: bytes, board: str) -> Tuple[bool, str]:
        """
        Validate a firmware image before flashing.
        
        Checks:
        - File size is reasonable for the target board
        - Vector table has valid stack pointer and reset handler
        - Image doesn't appear encrypted
        
        Args:
            firmware_data: Raw firmware binary
            board: Target board name
        
        Returns:
            (is_valid, message) tuple.
        """
        size = len(firmware_data)
        
        # Size checks
        if board.lower() in ('esc', 'drv'):
            max_size = 124 * 1024  # 128KB minus 4KB bootloader
        else:
            max_size = 60 * 1024   # 64KB minus 4KB bootloader
        
        if size < 256:
            return False, f"Firmware too small ({size} bytes). Minimum 256 bytes."
        
        if size > max_size:
            return False, f"Firmware too large ({size} bytes). Maximum {max_size} bytes for {board.upper()}."
        
        # Vector table validation
        if len(firmware_data) >= 8:
            sp = struct.unpack_from('<I', firmware_data, 0)[0]
            reset = struct.unpack_from('<I', firmware_data, 4)[0]
            
            # Stack pointer should be in SRAM
            sp_valid = 0x20000000 <= sp <= 0x20005000
            # Reset handler should be in app flash region
            reset_valid = 0x08001000 <= reset <= (0x08001000 + size + 0x1000)
            
            if not sp_valid:
                # Check if this might be an nRF51822 image (BLE board)
                if board.lower() == 'ble' and 0x20000000 <= sp <= 0x20008000:
                    pass  # nRF51 has 16/32KB SRAM
                elif board.lower() == 'ble' and sp & 0xF0000000 == 0x20000000:
                    pass  # Still looks like SRAM, just bigger range
                else:
                    return False, (
                        f"Invalid stack pointer: 0x{sp:08X}. "
                        f"Expected 0x20000000–0x20005000. "
                        f"The firmware may be encrypted — decrypt with XiaoTEA first."
                    )
            
            if not reset_valid:
                # BLE firmware might be for nRF51822 (address 0x00000000+)
                if board.lower() == 'ble' and 0x00000000 < reset < 0x00040000:
                    print(f"  NOTE: BLE firmware appears to be an nRF51822 image "
                          f"(reset @ 0x{reset:08X})")
                else:
                    return False, (
                        f"Invalid reset handler: 0x{reset:08X}. "
                        f"Expected 0x08001xxx range. "
                        f"The firmware may be encrypted or for a different board."
                    )
        
        return True, f"Firmware valid: {size} bytes, SP=0x{sp:08X}, Reset=0x{reset:08X}"
    
    def flash_firmware(self, firmware_data: bytes, board: str,
                       version: int = 0x0000,
                       block_size: int = IAP_BLOCK_SIZE,
                       skip_validation: bool = False) -> bool:
        """
        Flash firmware to a board using the IAP protocol.
        
        Complete sequence:
        1. Validate firmware image
        2. Send IAP start command (register 0x07)
        3. Wait for bootloader (flash erase)
        4. Send firmware data blocks (register 0x08)
        5. Send verify command (register 0x09)
        6. Send reset command (register 0x0A)
        
        Args:
            firmware_data: Raw firmware binary (unencrypted .bin)
            board: Target board ('esc', 'ble', 'bms')
            version: Firmware version word (e.g., 0x060D for 6.13)
            block_size: Bytes per transfer block (default 64)
            skip_validation: Skip pre-flash validation
        
        Returns:
            True if flashing completed successfully.
        """
        board_addr = BOARD_ADDR.get(board.lower())
        if board_addr is None:
            print(f"ERROR: Unknown board '{board}'")
            return False
        
        fw_size = len(firmware_data)
        total_blocks = (fw_size + block_size - 1) // block_size
        
        print(f"\n{'='*60}")
        print(f"  NINEBOT G30 MAX — Firmware Flasher")
        print(f"{'='*60}")
        print(f"  Target:     {board.upper()} (0x{board_addr:02X})")
        print(f"  Firmware:   {fw_size} bytes ({fw_size/1024:.1f} KB)")
        print(f"  Version:    0x{version:04X}")
        print(f"  Blocks:     {total_blocks} × {block_size} bytes")
        print(f"  MD5:        {hashlib.md5(firmware_data).hexdigest()}")
        print(f"  SHA1:       {hashlib.sha1(firmware_data).hexdigest()}")
        print(f"{'='*60}")
        
        # Step 0: Validate firmware
        if not skip_validation:
            print("\n[1/5] Validating firmware image...")
            valid, msg = self.validate_firmware(firmware_data, board)
            if not valid:
                print(f"  FAILED: {msg}")
                return False
            print(f"  OK: {msg}")
        
        # Step 1: Send IAP Start
        print("\n[2/5] Sending IAP start command...")
        iap_start_payload = struct.pack('<HH', fw_size & 0xFFFF, version)
        
        # For larger firmware, include full size
        if fw_size > 0xFFFF:
            iap_start_payload = struct.pack('<IH', fw_size, version)
        
        self.send_packet(board_addr, CMD_WRITE, IAP_START, iap_start_payload)
        
        # Wait for ACK
        ack = self.receive_packet(timeout=3.0)
        if ack:
            print(f"  Board acknowledged IAP start")
        else:
            print(f"  WARNING: No ACK received (board may have reset immediately)")
        
        # Wait for bootloader to erase flash
        print(f"  Waiting {IAP_START_DELAY:.1f}s for flash erase...")
        time.sleep(IAP_START_DELAY)
        
        # Step 2: Send firmware data blocks
        print(f"\n[3/5] Sending firmware data ({total_blocks} blocks)...")
        
        errors = 0
        for block_num in range(total_blocks):
            offset = block_num * block_size
            chunk = firmware_data[offset:offset + block_size]
            
            # Build block payload: [block_num_L, block_num_H, data...]
            block_payload = struct.pack('<H', block_num) + chunk
            
            # Retry loop
            success = False
            for retry in range(IAP_MAX_RETRIES):
                self.send_packet(board_addr, CMD_WRITE, IAP_DATA, block_payload)
                
                ack = self.receive_packet(timeout=IAP_ACK_TIMEOUT)
                if ack and ack['command'] == CMD_WRITE_ACK:
                    success = True
                    break
                
                if retry < IAP_MAX_RETRIES - 1:
                    time.sleep(0.1)
            
            if not success:
                errors += 1
                if errors > 10:
                    print(f"\n  ABORT: Too many errors ({errors})")
                    return False
            
            # Progress indicator
            progress = (block_num + 1) / total_blocks * 100
            bar_len = 40
            filled = int(bar_len * (block_num + 1) / total_blocks)
            bar = '█' * filled + '░' * (bar_len - filled)
            print(f"\r  [{bar}] {progress:5.1f}% "
                  f"({block_num+1}/{total_blocks}) "
                  f"{'E:'+str(errors) if errors else ''}",
                  end='', flush=True)
            
            time.sleep(IAP_BLOCK_DELAY)
        
        print()  # Newline after progress bar
        
        if errors > 0:
            print(f"  WARNING: {errors} blocks had transmission errors")
        
        # Step 3: Verify
        print(f"\n[4/5] Sending verify command...")
        fw_checksum = calculate_checksum(firmware_data)
        verify_payload = struct.pack('<HH', fw_checksum, fw_size & 0xFFFF)
        
        self.send_packet(board_addr, CMD_WRITE, IAP_VERIFY, verify_payload)
        
        ack = self.receive_packet(timeout=5.0)
        if ack and ack['command'] == CMD_WRITE_ACK:
            if ack['payload'] and ack['payload'][0] == 0x01:
                print(f"  Verification PASSED")
            else:
                print(f"  WARNING: Verification response: {ack['payload'].hex() if ack['payload'] else 'empty'}")
        else:
            print(f"  WARNING: No verification ACK (bootloader may not support it)")
        
        # Step 4: Reset
        print(f"\n[5/5] Sending reset command...")
        self.send_packet(board_addr, CMD_WRITE, IAP_RESET, b'\x01')
        
        ack = self.receive_packet(timeout=2.0)
        if ack:
            print(f"  Reset acknowledged")
        else:
            print(f"  Board reset (no ACK expected)")
        
        print(f"\n{'='*60}")
        print(f"  FLASH COMPLETE — {board.upper()} updated successfully")
        print(f"  Errors: {errors}")
        print(f"{'='*60}\n")
        
        return True


# =============================================================================
# CLI Interface
# =============================================================================

def cmd_info(flasher: NinebotFlasher, args):
    """Read and display scooter information."""
    print("\n" + "="*50)
    print("  Ninebot G30 Max — System Information")
    print("="*50)
    
    for board in ['esc', 'ble', 'bms']:
        try:
            flasher.dump_registers(board)
        except Exception as e:
            print(f"  {board.upper()}: Error — {e}")
            continue
        time.sleep(0.2)


def cmd_dump_regs(flasher: NinebotFlasher, args):
    """Dump registers from a specific board."""
    board = args.dump_regs.lower()
    flasher.dump_registers(board)


def cmd_read_reg(flasher: NinebotFlasher, args):
    """Read a specific register."""
    board_addr = BOARD_ADDR.get(args.board.lower(), ADDR_ESC)
    reg = int(args.read_reg, 0)  # Support hex (0x10) or decimal
    size = args.reg_size or 2
    
    raw = flasher.read_register(board_addr, reg, size)
    if raw:
        print(f"  Register 0x{reg:02X}: {raw.hex(' ')} ({format_register_value(raw, 'hex')})")
    else:
        print(f"  Failed to read register 0x{reg:02X}")


def cmd_flash(flasher: NinebotFlasher, args):
    """Flash firmware to a board."""
    # Load firmware file
    fw_path = args.firmware
    if not os.path.isfile(fw_path):
        print(f"ERROR: Firmware file not found: {fw_path}")
        return
    
    with open(fw_path, 'rb') as f:
        firmware_data = f.read()
    
    print(f"  Loaded: {fw_path} ({len(firmware_data)} bytes)")
    
    # Check if encrypted (.bin.enc)
    if fw_path.endswith('.enc'):
        print("  Firmware appears encrypted (.enc). Decrypting with XiaoTEA...")
        firmware_data = xiaotea_decrypt(firmware_data, XIAOTEA_DEFAULT_KEY)
        print(f"  Decrypted: {len(firmware_data)} bytes")
    
    # Parse version from filename if not specified
    version = args.version or 0x0000
    if version == 0x0000:
        # Try to extract from filename (e.g., DRV_1.6.13 → 0x060D)
        basename = os.path.basename(fw_path)
        # This is a best-effort extraction
        import re
        match = re.search(r'(\d+)\.(\d+)\.(\d+)', basename)
        if match:
            major, minor, patch = map(int, match.groups())
            version = (major << 8) | (minor << 4) | patch
            print(f"  Auto-detected version: {major}.{minor}.{patch} (0x{version:04X})")
    
    # Confirmation
    board = args.board.lower()
    if not args.yes:
        print(f"\n  ⚠️  WARNING: This will flash {board.upper()} with custom firmware.")
        print(f"  ⚠️  The board will be unresponsive during flashing.")
        print(f"  ⚠️  Do NOT power off the scooter during this process.")
        confirm = input("\n  Type 'FLASH' to confirm: ")
        if confirm.strip() != 'FLASH':
            print("  Aborted.")
            return
    
    # Flash
    block_size = args.block_size or IAP_BLOCK_SIZE
    success = flasher.flash_firmware(
        firmware_data, board,
        version=version,
        block_size=block_size,
        skip_validation=args.skip_validation,
    )
    
    if not success:
        print("  FLASHING FAILED — check connection and try again")
        sys.exit(1)


def cmd_decrypt(args):
    """Decrypt a .bin.enc file (offline, no serial needed)."""
    if not os.path.isfile(args.decrypt):
        print(f"ERROR: File not found: {args.decrypt}")
        return
    
    with open(args.decrypt, 'rb') as f:
        encrypted = f.read()
    
    decrypted = xiaotea_decrypt(encrypted, XIAOTEA_DEFAULT_KEY)
    
    out_path = args.decrypt
    if out_path.endswith('.enc'):
        out_path = out_path[:-4]
    else:
        out_path += '.dec'
    
    with open(out_path, 'wb') as f:
        f.write(decrypted)
    
    print(f"  Decrypted: {args.decrypt} → {out_path}")
    print(f"  Input:  {len(encrypted)} bytes")
    print(f"  Output: {len(decrypted)} bytes")
    print(f"  MD5:    {hashlib.md5(decrypted).hexdigest()}")


def cmd_encrypt(args):
    """Encrypt a .bin file for OTA distribution (offline, no serial needed)."""
    if not os.path.isfile(args.encrypt):
        print(f"ERROR: File not found: {args.encrypt}")
        return
    
    with open(args.encrypt, 'rb') as f:
        plain = f.read()
    
    encrypted = xiaotea_encrypt(plain, XIAOTEA_DEFAULT_KEY)
    
    out_path = args.encrypt + '.enc'
    
    with open(out_path, 'wb') as f:
        f.write(encrypted)
    
    print(f"  Encrypted: {args.encrypt} → {out_path}")
    print(f"  Input:  {len(plain)} bytes")
    print(f"  Output: {len(encrypted)} bytes")


def cmd_validate(args):
    """Validate a firmware image (offline, no serial needed)."""
    if not os.path.isfile(args.validate):
        print(f"ERROR: File not found: {args.validate}")
        return
    
    with open(args.validate, 'rb') as f:
        data = f.read()
    
    board = args.board or 'esc'
    
    print(f"\n  Firmware Validation: {args.validate}")
    print(f"  Target board: {board.upper()}")
    print(f"  Size: {len(data)} bytes ({len(data)/1024:.1f} KB)")
    print(f"  MD5:  {hashlib.md5(data).hexdigest()}")
    print(f"  SHA1: {hashlib.sha1(data).hexdigest()}")
    
    flasher = NinebotFlasher.__new__(NinebotFlasher)
    valid, msg = flasher.validate_firmware(data, board)
    
    if valid:
        print(f"  Result: VALID — {msg}")
    else:
        print(f"  Result: INVALID — {msg}")
    
    # Also show vector table details
    if len(data) >= 64:
        print(f"\n  Vector Table:")
        vectors = ["SP", "Reset", "NMI", "HardFault", "MemManage",
                    "BusFault", "UsageFault"]
        for i, name in enumerate(vectors):
            if i * 4 + 4 <= len(data):
                val = struct.unpack_from('<I', data, i * 4)[0]
                print(f"    {name:20s} = 0x{val:08X}")


def main():
    parser = argparse.ArgumentParser(
        description="Ninebot G30 Max — Custom Firmware Flasher",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Flash ESC firmware:
    %(prog)s --port COM3 --board esc --firmware DRV_custom.bin

  Flash BMS firmware:
    %(prog)s --port COM3 --board bms --firmware BMS_1.7.4.5.bin

  Read scooter info:
    %(prog)s --port COM3 --info

  Dump ESC registers:
    %(prog)s --port COM3 --dump-regs esc

  Decrypt firmware file (offline):
    %(prog)s --decrypt firmware.bin.enc

  Encrypt firmware for OTA (offline):
    %(prog)s --encrypt firmware.bin

  Validate firmware file (offline):
    %(prog)s --validate firmware.bin --board esc

Safety:
  - Always backup stock firmware before flashing
  - BMS modifications can cause battery fires
  - Do not interrupt the flashing process
  - Verify firmware checksums before flashing
        """)
    
    # Connection
    parser.add_argument('--port', '-p',
                        help='Serial port (e.g., COM3 or /dev/ttyUSB0)')
    parser.add_argument('--board', '-b',
                        choices=['esc', 'drv', 'ble', 'bms', 'bms2'],
                        help='Target board for flashing/reading')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show raw packet data')
    
    # Actions
    parser.add_argument('--firmware', '-f',
                        help='Firmware file to flash (.bin or .bin.enc)')
    parser.add_argument('--info', '-i', action='store_true',
                        help='Read scooter information from all boards')
    parser.add_argument('--dump-regs',
                        choices=['esc', 'drv', 'ble', 'bms'],
                        help='Dump registers from a specific board')
    parser.add_argument('--read-reg',
                        help='Read a specific register (hex, e.g., 0x10)')
    parser.add_argument('--reg-size', type=int, default=2,
                        help='Number of bytes to read for --read-reg')
    
    # Flash options
    parser.add_argument('--version', type=lambda x: int(x, 0), default=0,
                        help='Firmware version word (hex, e.g., 0x060D)')
    parser.add_argument('--block-size', type=int, default=IAP_BLOCK_SIZE,
                        help=f'Block size for data transfer (default: {IAP_BLOCK_SIZE})')
    parser.add_argument('--skip-validation', action='store_true',
                        help='Skip firmware validation checks')
    parser.add_argument('--yes', '-y', action='store_true',
                        help='Skip confirmation prompt')
    
    # Offline tools
    parser.add_argument('--decrypt',
                        help='Decrypt a .bin.enc file with XiaoTEA (offline)')
    parser.add_argument('--encrypt',
                        help='Encrypt a .bin file with XiaoTEA for OTA (offline)')
    parser.add_argument('--validate',
                        help='Validate a firmware image (offline)')
    
    args = parser.parse_args()
    
    # Handle offline commands (no serial port needed)
    if args.decrypt:
        cmd_decrypt(args)
        return
    
    if args.encrypt:
        cmd_encrypt(args)
        return
    
    if args.validate:
        cmd_validate(args)
        return
    
    # Online commands require a serial port
    if not args.port:
        parser.error("--port is required for online operations")
    
    # Determine action
    if not any([args.firmware, args.info, args.dump_regs, args.read_reg]):
        parser.error("Specify an action: --firmware, --info, --dump-regs, or --read-reg")
    
    if args.firmware and not args.board:
        parser.error("--board is required when flashing firmware")
    
    # Connect
    flasher = NinebotFlasher(args.port, verbose=args.verbose)
    if not flasher.connect():
        sys.exit(1)
    
    try:
        if args.info:
            cmd_info(flasher, args)
        elif args.dump_regs:
            cmd_dump_regs(flasher, args)
        elif args.read_reg:
            cmd_read_reg(flasher, args)
        elif args.firmware:
            cmd_flash(flasher, args)
    except KeyboardInterrupt:
        print("\n  Interrupted by user")
    except Exception as e:
        print(f"\n  ERROR: {e}")
        if args.verbose:
            import traceback
            traceback.print_exc()
        sys.exit(1)
    finally:
        flasher.disconnect()


if __name__ == "__main__":
    main()
