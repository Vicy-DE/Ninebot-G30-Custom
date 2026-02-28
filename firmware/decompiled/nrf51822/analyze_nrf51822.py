#!/usr/bin/env python3
"""
nRF51822 Firmware Analyzer & Disassembler
=========================================

Analyzes the BLE dashboard firmware as nRF51822 (ARM Cortex-M0, Thumb-only) images.
The BLE .bin files distributed for the Ninebot G30 Max are NOT STM32 firmware —
they are the nRF51822 application firmware loaded at 0x00018000 (post-SoftDevice).

Architecture: ARM Cortex-M0 (Thumb-only, no Thumb-2 32-bit instructions except BL/BLX)
SoftDevice: Nordic S110 v8.0 or S130 v2.0 (occupies 0x00000000–0x00017FFF)
Application base: 0x00018000
SRAM: 0x20000000–0x20003FFF (16KB for QFAA variant)

Usage:
    python analyze_nrf51822.py <firmware.bin> [--output <outfile>]
"""

import struct
import sys
import os
import argparse
from collections import defaultdict

# ============================================================================
#  nRF51822 Constants
# ============================================================================

NRF51_APP_BASE = 0x00018000    # Application start (after S110 SoftDevice)
NRF51_FLASH_END = 0x00040000   # 256 KB total flash
NRF51_SRAM_BASE = 0x20000000
NRF51_SRAM_END  = 0x20004000   # 16 KB SRAM (QFAA)
NRF51_SRAM_32K  = 0x20008000   # 32 KB SRAM (QFAC)
NRF51_BOOTLOADER = 0x0003C000  # Standard Nordic DFU bootloader location
NRF51_UICR_BASE = 0x10001000   # User Information Configuration Registers
NRF51_FICR_BASE = 0x10000000   # Factory Information Configuration Registers

# SoftDevice SVC (Supervisor Call) number ranges
# These are used by the application to call into the SoftDevice BLE stack
SOFTDEVICE_SVC_RANGES = {
    # S110 v8.0 SVC numbers
    (0x60, 0x6F): "sd_softdevice_*",        # SoftDevice enable/disable
    (0x70, 0x7F): "sd_ble_gap_*",           # GAP functions
    (0x80, 0x8F): "sd_ble_gattc_*",         # GATT Client
    (0x90, 0x9F): "sd_ble_gatts_*",         # GATT Server
    (0xA0, 0xAF): "sd_ble_l2cap_*",         # L2CAP
    (0xB0, 0xBF): "sd_ble_evt_*",           # Event handling
    (0x10, 0x1F): "sd_nvic_*",              # NVIC interrupt control
    (0x20, 0x2F): "sd_ppi_*",               # PPI (Programmable Peripheral Interconnect)
    (0x30, 0x3F): "sd_power_*",             # Power management
    (0x40, 0x4F): "sd_timeslot_*",          # Radio timeslot
    (0x0D, 0x0D): "sd_app_evt_wait",        # Wait for event
    (0x0E, 0x0E): "sd_clock_hfclk_request", # HF clock request
    (0x0F, 0x0F): "sd_clock_hfclk_release", # HF clock release
}

# nRF51822 Peripheral Register Map
NRF51_PERIPHERALS = {
    0x40000000: "POWER",          # Power control
    0x40000000: "CLOCK",          # Clock control (overlaps with POWER)
    0x40001000: "RADIO",          # 2.4 GHz Radio
    0x40002000: "UART0",          # UART
    0x40003000: "SPI0/TWI0",      # SPI Master 0 / I2C 0
    0x40004000: "SPI1/TWI1/SPIS1", # SPI Master 1 / I2C 1
    0x40006000: "GPIOTE",         # GPIO Tasks and Events
    0x40007000: "ADC",            # Analog to Digital Converter
    0x40008000: "TIMER0",         # Timer/Counter 0
    0x40009000: "TIMER1",         # Timer/Counter 1
    0x4000A000: "TIMER2",         # Timer/Counter 2
    0x4000B000: "RTC0",           # Real Time Counter 0
    0x4000C000: "TEMP",           # Temperature Sensor
    0x4000D000: "RNG",            # Random Number Generator
    0x4000E000: "ECB",            # AES ECB Mode Encryption
    0x4000F000: "CCM/AAR",        # AES CCM / Address Resolver
    0x40010000: "WDT",            # Watchdog Timer
    0x40011000: "RTC1",           # Real Time Counter 1
    0x40012000: "QDEC",           # Quadrature Decoder
    0x40013000: "LPCOMP",         # Low Power Comparator
    0x4001E000: "NVMC",           # Non-Volatile Memory Controller
    0x4001F000: "PPI",            # Programmable Peripheral Interconnect
    0x50000000: "GPIO",           # General Purpose I/O
}

# UART0 specific registers
UART0_REGS = {
    0x40002000: "TASKS_STARTRX",
    0x40002004: "TASKS_STOPRX",
    0x40002008: "TASKS_STARTTX",
    0x4000200C: "TASKS_STOPTX",
    0x40002014: "TASKS_SUSPEND",
    0x40002100: "EVENTS_CTS",
    0x40002104: "EVENTS_NCTS",
    0x40002108: "EVENTS_RXDRDY",
    0x40002110: "EVENTS_TXDRDY",
    0x40002118: "EVENTS_ERROR",
    0x40002124: "EVENTS_RXTO",
    0x40002300: "INTEN",
    0x40002304: "INTENSET",
    0x40002308: "INTENCLR",
    0x40002444: "ERRORSRC",
    0x40002500: "ENABLE",
    0x40002508: "PSELRTS",
    0x4000250C: "PSELTXD",
    0x40002510: "PSELCTS",
    0x40002514: "PSELRXD",
    0x40002518: "RXD",
    0x4000251C: "TXD",
    0x40002524: "BAUDRATE",
    0x4000256C: "CONFIG",
}

# NVMC (Non-Volatile Memory Controller) registers — critical for flash operations
NVMC_REGS = {
    0x4001E400: "NVMC.READY",      # Ready flag
    0x4001E504: "NVMC.CONFIG",     # Configuration (0=Read, 1=Write, 2=Erase)
    0x4001E508: "NVMC.ERASEPAGE",  # Erase page (write page address here)
    0x4001E50C: "NVMC.ERASEPCR1",  # Erase page in code region 1
    0x4001E510: "NVMC.ERASEALL",   # Erase all
    0x4001E514: "NVMC.ERASEPCR0",  # Erase page in code region 0
    0x4001E518: "NVMC.ERASEUICR",  # Erase UICR
}

# UART baudrate values
UART_BAUDRATES = {
    0x00004000: 1200,
    0x00008000: 2400,
    0x00010000: 4800,
    0x0001D000: 9600,
    0x00027000: 14400,
    0x0003B000: 19200,
    0x0003E800: 28800,
    0x00060000: 38400,
    0x00075000: 57600,
    0x009D5000: 76800,
    0x00100000: 115200,
    0x00200000: 230400,
    0x00400000: 250000,
    0x00800000: 460800,
    0x01000000: 921600,
    0x10000000: 1000000,
}

# Cortex-M0 Vector Table (16 system + 32 peripheral IRQs = 48 entries)
# nRF51822 IRQ assignments
NRF51_IRQ_NAMES = {
    0:  "POWER_CLOCK",
    1:  "RADIO",
    2:  "UART0",
    3:  "SPI0_TWI0",
    4:  "SPI1_TWI1",
    5:  "Reserved5",
    6:  "GPIOTE",
    7:  "ADC",
    8:  "TIMER0",
    9:  "TIMER1",
    10: "TIMER2",
    11: "RTC0",
    12: "TEMP",
    13: "RNG",
    14: "ECB",
    15: "CCM_AAR",
    16: "WDT",
    17: "RTC1",
    18: "QDEC",
    19: "LPCOMP",
    20: "SWI0",
    21: "SWI1",
    22: "SWI2",
    23: "SWI3",
    24: "SWI4",
    25: "SWI5",
    26: "Reserved26",
    27: "Reserved27",
    28: "Reserved28",
    29: "Reserved29",
    30: "Reserved30",
    31: "Reserved31",
}

# Xiaomi MiIO BLE constants (found in firmware strings)
MIIO_SIGNATURES = [
    b"Mi Serivce",     # [sic] — Xiaomi service init
    b"miio ble flash",
    b"mi_service",
    b"cloud bind",
    b"app bond",
    b"login cfm",
    b"decrypted auth",
    b"encrypt sn",
    b"beaconkey",
    b"token",
    b"Register succ",
]

# Nordic SDK signatures
NORDIC_SIGNATURES = [
    b"psm callback",        # Persistent Storage Manager
    b"flash operation",      # Flash write/erase operations
    b"flash write",
    b"flash read",
    b"update succ",
    b"load succ",
    b"store succ",
    b"clear succ",
]

# ============================================================================
#  Thumb Instruction Decoder (Cortex-M0 subset)
# ============================================================================

class ThumbDecoder:
    """Decodes 16-bit Thumb instructions (Cortex-M0 subset) + BL/BLX (32-bit)."""

    def __init__(self, data, base_addr):
        self.data = data
        self.base = base_addr

    def read16(self, offset):
        if offset + 2 <= len(self.data):
            return struct.unpack_from('<H', self.data, offset)[0]
        return None

    def read32(self, offset):
        if offset + 4 <= len(self.data):
            return struct.unpack_from('<I', self.data, offset)[0]
        return None

    def decode_at(self, offset):
        """Decode instruction at offset. Returns (mnemonic, operands, size, branch_target)."""
        hw = self.read16(offset)
        if hw is None:
            return ("???", "", 2, None)

        addr = self.base + offset

        # === 32-bit BL/BLX (Thumb-2 only instructions allowed on M0) ===
        if (hw >> 11) == 0x1E:  # 0xF000-0xF7FF: BL/BLX prefix
            hw2 = self.read16(offset + 2)
            if hw2 is not None:
                if (hw2 >> 12) == 0xD or (hw2 >> 12) == 0xF:
                    # BL instruction
                    s = (hw >> 10) & 1
                    imm10 = hw & 0x3FF
                    j1 = (hw2 >> 13) & 1
                    j2 = (hw2 >> 11) & 1
                    imm11 = hw2 & 0x7FF
                    i1 = (~(j1 ^ s)) & 1
                    i2 = (~(j2 ^ s)) & 1
                    imm32 = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
                    if s:
                        imm32 |= 0xFE000000  # sign extend
                        imm32 -= 0x100000000
                    target = (addr + 4 + imm32) & 0xFFFFFFFF
                    is_blx = ((hw2 >> 12) & 1) == 0
                    mnem = "blx" if is_blx else "bl"
                    return (mnem, f"#0x{target:x}", 4, target)

        # === 16-bit Thumb instructions ===

        # LSL immediate
        if (hw >> 11) == 0b00000:
            rd = hw & 7
            rm = (hw >> 3) & 7
            imm5 = (hw >> 6) & 0x1F
            return ("lsls", f"r{rd}, r{rm}, #{imm5}", 2, None)

        # LSR immediate
        if (hw >> 11) == 0b00001:
            rd = hw & 7
            rm = (hw >> 3) & 7
            imm5 = (hw >> 6) & 0x1F
            if imm5 == 0: imm5 = 32
            return ("lsrs", f"r{rd}, r{rm}, #{imm5}", 2, None)

        # ASR immediate
        if (hw >> 11) == 0b00010:
            rd = hw & 7
            rm = (hw >> 3) & 7
            imm5 = (hw >> 6) & 0x1F
            if imm5 == 0: imm5 = 32
            return ("asrs", f"r{rd}, r{rm}, #{imm5}", 2, None)

        # ADD/SUB register
        if (hw >> 9) == 0b0001100:
            rd = hw & 7
            rn = (hw >> 3) & 7
            rm = (hw >> 6) & 7
            return ("adds", f"r{rd}, r{rn}, r{rm}", 2, None)
        if (hw >> 9) == 0b0001101:
            rd = hw & 7
            rn = (hw >> 3) & 7
            rm = (hw >> 6) & 7
            return ("subs", f"r{rd}, r{rn}, r{rm}", 2, None)

        # ADD/SUB 3-bit immediate
        if (hw >> 9) == 0b0001110:
            rd = hw & 7
            rn = (hw >> 3) & 7
            imm3 = (hw >> 6) & 7
            return ("adds", f"r{rd}, r{rn}, #{imm3}", 2, None)
        if (hw >> 9) == 0b0001111:
            rd = hw & 7
            rn = (hw >> 3) & 7
            imm3 = (hw >> 6) & 7
            return ("subs", f"r{rd}, r{rn}, #{imm3}", 2, None)

        # MOV/CMP/ADD/SUB 8-bit immediate
        if (hw >> 11) == 0b00100:
            rd = (hw >> 8) & 7
            imm8 = hw & 0xFF
            return ("movs", f"r{rd}, #0x{imm8:x}", 2, None)
        if (hw >> 11) == 0b00101:
            rn = (hw >> 8) & 7
            imm8 = hw & 0xFF
            return ("cmp", f"r{rn}, #0x{imm8:x}", 2, None)
        if (hw >> 11) == 0b00110:
            rd = (hw >> 8) & 7
            imm8 = hw & 0xFF
            return ("adds", f"r{rd}, #{imm8}", 2, None)
        if (hw >> 11) == 0b00111:
            rd = (hw >> 8) & 7
            imm8 = hw & 0xFF
            return ("subs", f"r{rd}, #{imm8}", 2, None)

        # Data processing (ALU)
        if (hw >> 10) == 0b010000:
            op = (hw >> 6) & 0xF
            rm = (hw >> 3) & 7
            rd = hw & 7
            alu_ops = {
                0: "ands", 1: "eors", 2: "lsls", 3: "lsrs",
                4: "asrs", 5: "adcs", 6: "sbcs", 7: "rors",
                8: "tst", 9: "rsbs", 10: "cmp", 11: "cmn",
                12: "orrs", 13: "muls", 14: "bics", 15: "mvns"
            }
            mnem = alu_ops.get(op, "???")
            if op == 9:
                return (mnem, f"r{rd}, r{rm}, #0", 2, None)
            return (mnem, f"r{rd}, r{rm}", 2, None)

        # Special data / branch-exchange
        if (hw >> 10) == 0b010001:
            op = (hw >> 8) & 3
            rd = ((hw >> 4) & 8) | (hw & 7)
            rm = (hw >> 3) & 0xF
            if op == 0:
                return ("add", f"r{rd}, r{rm}", 2, None)
            elif op == 1:
                return ("cmp", f"r{rd}, r{rm}", 2, None)
            elif op == 2:
                return ("mov", f"r{rd}, r{rm}", 2, None)
            elif op == 3:
                if (hw >> 7) & 1:
                    return ("blx", f"r{rm}", 2, None)
                else:
                    return ("bx", f"r{rm}", 2, None)

        # LDR (PC-relative) — used to load constants from literal pool
        if (hw >> 11) == 0b01001:
            rt = (hw >> 8) & 7
            imm8 = hw & 0xFF
            target = ((addr + 4) & ~3) + (imm8 << 2)
            # Try to read the literal value
            lit_offset = target - self.base
            lit_val = None
            if 0 <= lit_offset < len(self.data) - 3:
                lit_val = struct.unpack_from('<I', self.data, lit_offset)[0]
            comment = ""
            if lit_val is not None:
                comment = f"  ; =0x{lit_val:08X}"
                # Annotate known peripherals
                periph = self._identify_peripheral(lit_val)
                if periph:
                    comment += f" ({periph})"
            return ("ldr", f"r{rt}, [pc, #0x{imm8 << 2:x}]{comment}", 2, None)

        # Load/Store register offset
        load_store_reg = {
            0b0101000: ("str",   "r{rd}, [r{rn}, r{rm}]"),
            0b0101001: ("strh",  "r{rd}, [r{rn}, r{rm}]"),
            0b0101010: ("strb",  "r{rd}, [r{rn}, r{rm}]"),
            0b0101011: ("ldrsb", "r{rd}, [r{rn}, r{rm}]"),
            0b0101100: ("ldr",   "r{rd}, [r{rn}, r{rm}]"),
            0b0101101: ("ldrh",  "r{rd}, [r{rn}, r{rm}]"),
            0b0101110: ("ldrb",  "r{rd}, [r{rn}, r{rm}]"),
            0b0101111: ("ldrsh", "r{rd}, [r{rn}, r{rm}]"),
        }
        key = (hw >> 9) & 0x7F
        if key in load_store_reg:
            mnem, fmt = load_store_reg[key]
            rd = hw & 7
            rn = (hw >> 3) & 7
            rm = (hw >> 6) & 7
            return (mnem, fmt.format(rd=rd, rn=rn, rm=rm), 2, None)

        # STR/LDR immediate offset (word)
        if (hw >> 11) == 0b01100:
            rd = hw & 7
            rn = (hw >> 3) & 7
            imm5 = ((hw >> 6) & 0x1F) << 2
            return ("str", f"r{rd}, [r{rn}, #0x{imm5:x}]", 2, None)
        if (hw >> 11) == 0b01101:
            rt = hw & 7
            rn = (hw >> 3) & 7
            imm5 = ((hw >> 6) & 0x1F) << 2
            return ("ldr", f"r{rt}, [r{rn}, #0x{imm5:x}]", 2, None)

        # STRB/LDRB immediate offset
        if (hw >> 11) == 0b01110:
            rd = hw & 7
            rn = (hw >> 3) & 7
            imm5 = (hw >> 6) & 0x1F
            return ("strb", f"r{rd}, [r{rn}, #0x{imm5:x}]", 2, None)
        if (hw >> 11) == 0b01111:
            rt = hw & 7
            rn = (hw >> 3) & 7
            imm5 = (hw >> 6) & 0x1F
            return ("ldrb", f"r{rt}, [r{rn}, #0x{imm5:x}]", 2, None)

        # STRH/LDRH immediate offset
        if (hw >> 11) == 0b10000:
            rd = hw & 7
            rn = (hw >> 3) & 7
            imm5 = ((hw >> 6) & 0x1F) << 1
            return ("strh", f"r{rd}, [r{rn}, #0x{imm5:x}]", 2, None)
        if (hw >> 11) == 0b10001:
            rt = hw & 7
            rn = (hw >> 3) & 7
            imm5 = ((hw >> 6) & 0x1F) << 1
            return ("ldrh", f"r{rt}, [r{rn}, #0x{imm5:x}]", 2, None)

        # STR/LDR SP-relative
        if (hw >> 11) == 0b10010:
            rt = (hw >> 8) & 7
            imm8 = (hw & 0xFF) << 2
            return ("str", f"r{rt}, [sp, #0x{imm8:x}]", 2, None)
        if (hw >> 11) == 0b10011:
            rt = (hw >> 8) & 7
            imm8 = (hw & 0xFF) << 2
            return ("ldr", f"r{rt}, [sp, #0x{imm8:x}]", 2, None)

        # ADR (PC-relative address)
        if (hw >> 11) == 0b10100:
            rd = (hw >> 8) & 7
            imm8 = (hw & 0xFF) << 2
            target = ((addr + 4) & ~3) + imm8
            return ("adr", f"r{rd}, #0x{target:x}", 2, target)

        # ADD SP + immediate
        if (hw >> 11) == 0b10101:
            rd = (hw >> 8) & 7
            imm8 = (hw & 0xFF) << 2
            return ("add", f"r{rd}, sp, #{imm8}", 2, None)

        # Miscellaneous
        if (hw >> 12) == 0b1011:
            # ADD/SUB SP
            if (hw >> 8) == 0b10110000:
                imm7 = (hw & 0x7F) << 2
                return ("add", f"sp, #{imm7}", 2, None)
            if (hw >> 8) == 0b10110001:
                imm7 = (hw & 0x7F) << 2
                return ("sub", f"sp, #{imm7}", 2, None)

            # SXTH, SXTB, UXTH, UXTB
            if (hw >> 6) == 0b1011001000:
                rd = hw & 7; rm = (hw >> 3) & 7
                return ("sxth", f"r{rd}, r{rm}", 2, None)
            if (hw >> 6) == 0b1011001001:
                rd = hw & 7; rm = (hw >> 3) & 7
                return ("sxtb", f"r{rd}, r{rm}", 2, None)
            if (hw >> 6) == 0b1011001010:
                rd = hw & 7; rm = (hw >> 3) & 7
                return ("uxth", f"r{rd}, r{rm}", 2, None)
            if (hw >> 6) == 0b1011001011:
                rd = hw & 7; rm = (hw >> 3) & 7
                return ("uxtb", f"r{rd}, r{rm}", 2, None)

            # PUSH
            if (hw >> 9) == 0b1011010:
                rlist = hw & 0xFF
                lr = (hw >> 8) & 1
                regs = [f"r{i}" for i in range(8) if rlist & (1 << i)]
                if lr:
                    regs.append("lr")
                return ("push", "{" + ", ".join(regs) + "}", 2, None)

            # SETEND, CPS (rare on M0)
            # REV, REV16, REVSH
            if (hw >> 6) == 0b1011101000:
                rd = hw & 7; rm = (hw >> 3) & 7
                return ("rev", f"r{rd}, r{rm}", 2, None)
            if (hw >> 6) == 0b1011101001:
                rd = hw & 7; rm = (hw >> 3) & 7
                return ("rev16", f"r{rd}, r{rm}", 2, None)
            if (hw >> 6) == 0b1011101011:
                rd = hw & 7; rm = (hw >> 3) & 7
                return ("revsh", f"r{rd}, r{rm}", 2, None)

            # POP
            if (hw >> 9) == 0b1011110:
                rlist = hw & 0xFF
                pc = (hw >> 8) & 1
                regs = [f"r{i}" for i in range(8) if rlist & (1 << i)]
                if pc:
                    regs.append("pc")
                return ("pop", "{" + ", ".join(regs) + "}", 2, None)

            # BKPT
            if (hw >> 8) == 0b10111110:
                imm8 = hw & 0xFF
                return ("bkpt", f"#{imm8}", 2, None)

            # NOP, SEV, WFE, WFI, YIELD
            if hw == 0xBF00:
                return ("nop", "", 2, None)
            if hw == 0xBF10:
                return ("yield", "", 2, None)
            if hw == 0xBF20:
                return ("wfe", "", 2, None)
            if hw == 0xBF30:
                return ("wfi", "", 2, None)
            if hw == 0xBF40:
                return ("sev", "", 2, None)

        # STM (Store Multiple)
        if (hw >> 11) == 0b11000:
            rn = (hw >> 8) & 7
            rlist = hw & 0xFF
            regs = [f"r{i}" for i in range(8) if rlist & (1 << i)]
            return ("stm", f"r{rn}!, {{{', '.join(regs)}}}", 2, None)

        # LDM (Load Multiple)
        if (hw >> 11) == 0b11001:
            rn = (hw >> 8) & 7
            rlist = hw & 0xFF
            regs = [f"r{i}" for i in range(8) if rlist & (1 << i)]
            wb = "!" if not (rlist & (1 << rn)) else ""
            return ("ldm", f"r{rn}{wb}, {{{', '.join(regs)}}}", 2, None)

        # Conditional branch
        if (hw >> 12) == 0b1101:
            cond = (hw >> 8) & 0xF
            if cond == 0xE:
                return ("udf", f"#{hw & 0xFF}", 2, None)
            if cond == 0xF:
                # SVC (Supervisor Call) — used to call SoftDevice
                svc_num = hw & 0xFF
                svc_name = self._identify_svc(svc_num)
                comment = f"  ; {svc_name}" if svc_name else ""
                return ("svc", f"#{svc_num} (0x{svc_num:02x}){comment}", 2, None)
            imm8 = hw & 0xFF
            if imm8 & 0x80:
                imm8 -= 256
            target = (addr + 4 + (imm8 << 1)) & 0xFFFFFFFF
            cond_names = {
                0: "beq", 1: "bne", 2: "bcs", 3: "bcc",
                4: "bmi", 5: "bpl", 6: "bvs", 7: "bvc",
                8: "bhi", 9: "bls", 10: "bge", 11: "blt",
                12: "bgt", 13: "ble"
            }
            mnem = cond_names.get(cond, f"b{cond}")
            return (mnem, f"#0x{target:x}", 2, target)

        # Unconditional branch
        if (hw >> 11) == 0b11100:
            imm11 = hw & 0x7FF
            if imm11 & 0x400:
                imm11 -= 2048
            target = (addr + 4 + (imm11 << 1)) & 0xFFFFFFFF
            return ("b", f"#0x{target:x}", 2, target)

        # CPSID / CPSIE
        if hw == 0xB662:
            return ("cpsie", "i", 2, None)
        if hw == 0xB672:
            return ("cpsid", "i", 2, None)

        # Fallback: unknown instruction
        return (f".hword", f"0x{hw:04X}", 2, None)

    def _identify_svc(self, num):
        """Identify SoftDevice SVC call by number."""
        for (lo, hi), name in SOFTDEVICE_SVC_RANGES.items():
            if lo <= num <= hi:
                return name
        return None

    def _identify_peripheral(self, addr):
        """Identify peripheral from address."""
        # Check specific UART registers
        if addr in UART0_REGS:
            return UART0_REGS[addr]
        if addr in NVMC_REGS:
            return NVMC_REGS[addr]
        # Check peripheral base addresses
        base = addr & 0xFFFFF000
        if base in NRF51_PERIPHERALS:
            return NRF51_PERIPHERALS[base]
        # Check UICR/FICR
        if 0x10000000 <= addr < 0x10000400:
            return "FICR"
        if 0x10001000 <= addr < 0x10001400:
            return "UICR"
        # Check SoftDevice region
        if 0x00000000 <= addr < 0x00018000:
            return "SoftDevice"
        # Check SRAM
        if 0x20000000 <= addr < 0x20008000:
            return "SRAM"
        return None


# ============================================================================
#  Firmware Analyzer
# ============================================================================

class NRF51Analyzer:
    """Complete nRF51822 firmware analysis."""

    def __init__(self, filepath):
        with open(filepath, 'rb') as f:
            self.data = f.read()
        self.filepath = filepath
        self.filename = os.path.basename(filepath)
        self.base = NRF51_APP_BASE
        self.decoder = ThumbDecoder(self.data, self.base)
        self.strings = {}          # offset -> string
        self.functions = {}        # addr -> name/info
        self.branch_targets = set()
        self.call_targets = {}     # addr -> [callers]
        self.svc_calls = {}        # addr -> svc_num
        self.nvmc_accesses = []    # (addr, instruction)
        self.peripheral_refs = defaultdict(list)  # periph_name -> [addrs]
        self.literal_pool = {}     # addr -> value

    def analyze(self):
        """Run all analysis passes."""
        print(f"\n{'='*72}")
        print(f"  nRF51822 FIRMWARE ANALYSIS: {self.filename}")
        print(f"{'='*72}\n")
        self._header_info()
        self._vector_table()
        self._extract_strings()
        self._disassemble_pass1()  # Collect branch targets, SVC calls, NVMC accesses
        self._identify_functions()
        self._analyze_update_mechanism()
        self._analyze_miio_protocol()
        self._analyze_flash_operations()
        self._analyze_softdevice_calls()
        self._analyze_uart_config()
        self._analyze_dfu_bootloader_refs()
        self._full_disassembly()

    def _header_info(self):
        """Print basic file info."""
        import hashlib
        print(f"  File: {self.filepath}")
        print(f"  Size: {len(self.data)} bytes ({len(self.data)/1024:.1f} KB)")
        print(f"  Target: nRF51822-QFAA (ARM Cortex-M0, 256KB Flash, 16KB SRAM)")
        print(f"  Base address: 0x{self.base:08X} (post-SoftDevice S110)")
        print(f"  End address:  0x{self.base + len(self.data):08X}")
        print(f"  MD5:    {hashlib.md5(self.data).hexdigest()}")
        print(f"  SHA256: {hashlib.sha256(self.data).hexdigest()}")
        print()

        # Determine SoftDevice version from app base
        if self.base == 0x00018000:
            print(f"  SoftDevice: S110 v8.0 (96 KB, ends at 0x00018000)")
        elif self.base == 0x0001B000:
            print(f"  SoftDevice: S130 v2.0 (108 KB, ends at 0x0001B000)")
        else:
            print(f"  SoftDevice: Unknown (app at 0x{self.base:08X})")

        free_flash = NRF51_BOOTLOADER - (self.base + len(self.data))
        print(f"  Free flash: {free_flash} bytes ({free_flash/1024:.1f} KB) before bootloader area")
        print()

    def _vector_table(self):
        """Parse and display Cortex-M0 vector table."""
        print(f"{'='*72}")
        print(f"  VECTOR TABLE (Cortex-M0)")
        print(f"{'='*72}\n")

        vec_count = min(48, len(self.data) // 4)
        vt = struct.unpack_from(f'<{vec_count}I', self.data, 0)

        # System exceptions
        sys_names = [
            "Initial SP", "Reset", "NMI", "HardFault",
            "Reserved", "Reserved", "Reserved", "Reserved",
            "Reserved", "Reserved", "Reserved", "SVCall",
            "Reserved", "Reserved", "PendSV", "SysTick"
        ]

        sp_val = vt[0]
        print(f"  Initial SP: 0x{sp_val:08X}", end="")
        if NRF51_SRAM_BASE <= sp_val <= NRF51_SRAM_END:
            stack_size = NRF51_SRAM_END - sp_val
            used_sram = sp_val - NRF51_SRAM_BASE
            print(f"  (SRAM top - {stack_size} bytes for stack, {used_sram} bytes used)")
        elif NRF51_SRAM_BASE <= sp_val <= NRF51_SRAM_32K:
            print(f"  (nRF51822-QFAC 32KB SRAM variant)")
        else:
            print(f"  (UNEXPECTED for nRF51822!)")

        print()
        active_handlers = []

        for i in range(1, min(16, vec_count)):
            handler = vt[i]
            name = sys_names[i] if i < len(sys_names) else f"SysVec_{i}"
            if handler != 0:
                in_app = self.base <= handler < self.base + len(self.data)
                status = "APP" if in_app else "EXTERNAL"
                print(f"  [{i:2d}] {name:14s}: 0x{handler:08X}  ({status})")
                if handler != 0 and in_app:
                    active_handlers.append((name, handler))
            elif name not in ("Reserved",):
                print(f"  [{i:2d}] {name:14s}: 0x{handler:08X}  (not set)")

        print(f"\n  --- Peripheral IRQ Handlers ---")
        for i in range(16, vec_count):
            handler = vt[i]
            irq_num = i - 16
            irq_name = NRF51_IRQ_NAMES.get(irq_num, f"IRQ_{irq_num}")
            if handler != 0:
                in_app = self.base <= handler < self.base + len(self.data)
                status = "APP" if in_app else "SD/EXT"
                print(f"  [{i:2d}] IRQ{irq_num:2d} {irq_name:16s}: 0x{handler:08X}  ({status})")
                active_handlers.append((irq_name, handler))

        print(f"\n  Active interrupt handlers: {len(active_handlers)}")
        for name, addr in active_handlers:
            self.functions[addr] = f"IRQ_{name}"
        print()

    def _extract_strings(self):
        """Extract readable strings (>=4 chars)."""
        print(f"{'='*72}")
        print(f"  STRING EXTRACTION")
        print(f"{'='*72}\n")

        min_len = 4
        current = []
        start = None
        important_strings = []

        for i, b in enumerate(self.data):
            if 0x20 <= b < 0x7F:
                if not current:
                    start = i
                current.append(chr(b))
            else:
                if len(current) >= min_len:
                    s = ''.join(current)
                    addr = self.base + start
                    self.strings[start] = s
                    # Check for important patterns
                    s_lower = s.lower()
                    is_important = any(kw in s_lower for kw in [
                        'flash', 'update', 'dfu', 'boot', 'miio', 'token',
                        'auth', 'bind', 'register', 'encrypt', 'decrypt',
                        'psm', 'uart', 'error', 'succ', 'fail', 'version',
                        'sn', 'key', 'ninebot', 'scooter', 'ble', 'service',
                        'connected', 'written', 'callback', 'init', 'state',
                        'login', 'cloud', 'bond', 'reset'
                    ])
                    if is_important:
                        important_strings.append((addr, s))
                current = []
                start = None

        print(f"  Total strings found: {len(self.strings)}")
        print(f"  Important strings: {len(important_strings)}\n")

        if important_strings:
            print(f"  --- Key Strings (firmware update / BLE / MiIO related) ---\n")
            for addr, s in sorted(important_strings):
                print(f"  0x{addr:08X}: \"{s}\"")
            print()

    def _disassemble_pass1(self):
        """First pass: collect branch targets, SVC calls, peripheral references."""
        offset = 0
        while offset < len(self.data) - 1:
            addr = self.base + offset
            mnem, ops, size, target = self.decoder.decode_at(offset)

            # Collect branch targets
            if target is not None:
                if mnem in ('bl', 'blx'):
                    if target not in self.call_targets:
                        self.call_targets[target] = []
                    self.call_targets[target].append(addr)
                elif mnem.startswith('b'):
                    self.branch_targets.add(target)

            # Collect SVC calls
            if mnem == 'svc':
                svc_num = int(ops.split('#')[1].split(' ')[0])
                self.svc_calls[addr] = svc_num

            # Collect literal pool references to NVMC
            if mnem == 'ldr' and '=0x' in ops:
                val_str = ops.split('=0x')[1].split(' ')[0].rstrip(')')
                try:
                    val = int(val_str, 16)
                    self.literal_pool[addr] = val
                    if 0x4001E000 <= val < 0x4001F000:
                        self.nvmc_accesses.append((addr, val))
                    periph = self.decoder._identify_peripheral(val)
                    if periph:
                        self.peripheral_refs[periph].append(addr)
                except ValueError:
                    pass

            offset += size

    def _identify_functions(self):
        """Identify function entry points from PUSH {... lr} patterns and call targets."""
        print(f"{'='*72}")
        print(f"  FUNCTION IDENTIFICATION")
        print(f"{'='*72}\n")

        # Find PUSH {... lr} patterns (function prologues)
        push_funcs = []
        offset = 0
        while offset < len(self.data) - 1:
            hw = struct.unpack_from('<H', self.data, offset)[0]
            # PUSH {..., lr} = 0xB5xx where bit 8 is set
            if (hw & 0xFF00) == 0xB500:
                addr = self.base + offset
                if addr not in self.functions:
                    self.functions[addr] = f"sub_{addr:08X}"
                push_funcs.append(addr)
            offset += 2

        # Add call targets as functions
        for target, callers in self.call_targets.items():
            if target not in self.functions:
                if self.base <= target < self.base + len(self.data):
                    self.functions[target] = f"sub_{target:08X}"

        print(f"  Functions with PUSH {{lr}} prologue: {len(push_funcs)}")
        print(f"  Unique BL/BLX call targets: {len(self.call_targets)}")
        print(f"  Total identified functions: {len(self.functions)}")

        # List most-called functions
        sorted_calls = sorted(self.call_targets.items(), key=lambda x: len(x[1]), reverse=True)
        if sorted_calls:
            print(f"\n  --- Most Called Functions (top 20) ---\n")
            for target, callers in sorted_calls[:20]:
                name = self.functions.get(target, f"0x{target:08X}")
                in_app = self.base <= target < self.base + len(self.data)
                location = "APP" if in_app else "SOFTDEVICE" if target < self.base else "EXTERNAL"
                print(f"  0x{target:08X} ({location:10s}) called {len(callers):3d} times  {name}")
        print()

    def _analyze_update_mechanism(self):
        """Analyze firmware self-update capabilities."""
        print(f"{'='*72}")
        print(f"  FIRMWARE UPDATE MECHANISM ANALYSIS")
        print(f"{'='*72}\n")

        # Check for NVMC (Non-Volatile Memory Controller) access
        print(f"  --- NVMC (Flash Controller) Access ---\n")
        if self.nvmc_accesses:
            for addr, nvmc_addr in self.nvmc_accesses:
                reg_name = NVMC_REGS.get(nvmc_addr, f"NVMC+0x{nvmc_addr - 0x4001E000:03X}")
                print(f"  0x{addr:08X}: references {reg_name} (0x{nvmc_addr:08X})")

                # Disassemble context around NVMC access
                offset = addr - self.base
                print(f"    Context:")
                for ctx_off in range(max(0, offset - 8), min(len(self.data) - 1, offset + 16), 2):
                    ctx_addr = self.base + ctx_off
                    mnem, ops, size, _ = self.decoder.decode_at(ctx_off)
                    marker = " >>>" if ctx_off == offset else "    "
                    print(f"    {marker} 0x{ctx_addr:08X}: {mnem:8s} {ops}")
                    if size == 4:
                        ctx_off += 2  # skip second halfword of 32-bit instruction
                print()
        else:
            print(f"  No direct NVMC register references found in literal pool.\n")
            print(f"  This suggests flash operations are done via SoftDevice SVC calls")
            print(f"  (sd_flash_page_erase, sd_flash_write) rather than direct register access.\n")

        # Check for SoftDevice flash-related SVC calls
        # sd_flash_write = SVC 0x60 range, sd_flash_page_erase = SVC 0x61 range
        flash_svcs = []
        for addr, svc_num in self.svc_calls.items():
            if 0x60 <= svc_num <= 0x6F:
                flash_svcs.append((addr, svc_num))

        if flash_svcs:
            print(f"  --- SoftDevice Flash SVC Calls ---\n")
            for addr, svc_num in flash_svcs:
                print(f"  0x{addr:08X}: SVC #{svc_num} (0x{svc_num:02x}) — sd_softdevice/flash operation")

        # Look for flash operation strings nearby
        flash_strings = []
        for off, s in self.strings.items():
            s_lower = s.lower()
            if any(kw in s_lower for kw in ['flash', 'erase', 'nvmc', 'program']):
                flash_strings.append((self.base + off, s))

        if flash_strings:
            print(f"\n  --- Flash-Related Strings ---\n")
            for addr, s in sorted(flash_strings):
                print(f"  0x{addr:08X}: \"{s}\"")
        print()

    def _analyze_miio_protocol(self):
        """Analyze Xiaomi MiIO BLE protocol implementation."""
        print(f"{'='*72}")
        print(f"  XIAOMI MiIO BLE PROTOCOL ANALYSIS")
        print(f"{'='*72}\n")

        miio_strings = []
        for off, s in self.strings.items():
            s_lower = s.lower()
            if any(kw in s_lower for kw in ['miio', 'mi_service', 'mi serivce',
                                              'token', 'auth', 'cloud bind',
                                              'app bond', 'login', 'encrypt sn',
                                              'beaconkey', 'register succ']):
                miio_strings.append((self.base + off, s))

        if miio_strings:
            print(f"  Found {len(miio_strings)} MiIO-related strings:\n")
            for addr, s in sorted(miio_strings):
                print(f"  0x{addr:08X}: \"{s}\"")

            print(f"\n  --- MiIO Authentication Flow (from strings) ---\n")
            print(f"  1. Mi Service Init → \"[E]: Mi Serivce Init fail\"")
            print(f"  2. Auth Write → \"On Auth Written\" → \"decrypted auth\"")
            print(f"  3. Token Write → \"On Token Written\"")
            print(f"  4. Cloud Bind → \"cloud bind succ/fail\"")
            print(f"  5. App Bond → \"app bond succ/fail\" → \"mi_service, bond succ\"")
            print(f"  6. Login → \"login cfm handler\" → \"login cfm succ/fail\"")
            print(f"  7. SN Exchange → \"SN Arrived\" → \"sn: %02x\"")
            print(f"  8. Register → \"Register succ, new token, encrypt sn, beaconkey\"")
            print(f"  9. Flash Register → \"miio ble flash register succ/fail\"")

            print(f"\n  --- MiIO Flash Registration ---\n")
            print(f"  The 'miio ble flash register' mechanism is Xiaomi's proprietary")
            print(f"  BLE OTA framework. Once the device is authenticated and registered")
            print(f"  with the MiIO cloud, flash operations can be performed over BLE.")
            print(f"  This is the PRIMARY update path for the nRF51822 firmware.")
        else:
            print(f"  No MiIO protocol strings found.")
        print()

    def _analyze_flash_operations(self):
        """Analyze flash read/write/erase operations."""
        print(f"{'='*72}")
        print(f"  FLASH OPERATIONS ANALYSIS")
        print(f"{'='*72}\n")

        flash_strings = []
        for off, s in self.strings.items():
            s_lower = s.lower()
            if any(kw in s_lower for kw in ['flash', 'psm', 'update succ',
                                              'load succ', 'store succ', 'clear succ']):
                flash_strings.append((self.base + off, s))

        if flash_strings:
            print(f"  Found {len(flash_strings)} flash/storage operation strings:\n")
            for addr, s in sorted(flash_strings):
                print(f"  0x{addr:08X}: \"{s}\"")

            print(f"\n  --- Persistent Storage Manager (PSM) ---\n")
            print(f"  The firmware uses Nordic SDK's Persistent Storage Manager for")
            print(f"  non-volatile storage of BLE bonds, tokens, and configuration.")
            print(f"  PSM operations: load, store, clear, update")
            print(f"  These use SoftDevice's sd_flash_write/sd_flash_page_erase SVCs.")

            print(f"\n  --- Flash Operation Flow ---\n")
            print(f"  1. Check flash write function: \"flash write func not setting\"")
            print(f"  2. Perform write: \"flash write succ\" / \"flash write fail\"")
            print(f"  3. Read back: \"flash read error\" (on failure)")
            print(f"  4. PSM callback: \"psm callback\" (async notification)")
            print(f"  5. Update result: \"update succ. len = %d\"")
        print()

    def _analyze_softdevice_calls(self):
        """Analyze SoftDevice SVC calls."""
        print(f"{'='*72}")
        print(f"  SOFTDEVICE SVC CALL ANALYSIS")
        print(f"{'='*72}\n")

        if self.svc_calls:
            # Group by SVC range
            svc_groups = defaultdict(list)
            for addr, svc_num in sorted(self.svc_calls.items()):
                group = None
                for (lo, hi), name in SOFTDEVICE_SVC_RANGES.items():
                    if lo <= svc_num <= hi:
                        group = name
                        break
                if group is None:
                    group = f"Unknown (0x{svc_num:02x})"
                svc_groups[group].append((addr, svc_num))

            print(f"  Total SVC calls: {len(self.svc_calls)}\n")
            for group, calls in sorted(svc_groups.items()):
                print(f"  {group}: {len(calls)} calls")
                for addr, num in calls[:5]:
                    print(f"    0x{addr:08X}: SVC #{num} (0x{num:02x})")
                if len(calls) > 5:
                    print(f"    ... and {len(calls)-5} more")
                print()
        else:
            print(f"  No SVC calls found. This is unusual for an nRF51822 app.\n")

    def _analyze_uart_config(self):
        """Analyze UART configuration."""
        print(f"{'='*72}")
        print(f"  UART CONFIGURATION ANALYSIS")
        print(f"{'='*72}\n")

        uart_refs = []
        for addr, val in self.literal_pool.items():
            if 0x40002000 <= val < 0x40003000:
                reg_name = UART0_REGS.get(val, f"UART0+0x{val - 0x40002000:03X}")
                uart_refs.append((addr, val, reg_name))

        if uart_refs:
            print(f"  Found {len(uart_refs)} UART register references:\n")
            for addr, val, name in sorted(uart_refs):
                print(f"  0x{addr:08X}: references {name} (0x{val:08X})")

        # Check for baud rate values in literal pool
        baud_refs = []
        for addr, val in self.literal_pool.items():
            if val in UART_BAUDRATES:
                baud_refs.append((addr, val, UART_BAUDRATES[val]))

        if baud_refs:
            print(f"\n  --- Baud Rate Configuration ---\n")
            for addr, val, baud in sorted(baud_refs):
                print(f"  0x{addr:08X}: BAUDRATE = 0x{val:08X} ({baud} baud)")
        elif not uart_refs:
            print(f"  No direct UART register references found.")
            print(f"  UART may be configured via SoftDevice or a library function.\n")
        print()

    def _analyze_dfu_bootloader_refs(self):
        """Look for DFU bootloader references."""
        print(f"{'='*72}")
        print(f"  DFU / BOOTLOADER REFERENCE ANALYSIS")
        print(f"{'='*72}\n")

        # Check for bootloader address (0x0003C000) in literal pool
        boot_refs = []
        for addr, val in self.literal_pool.items():
            if val == NRF51_BOOTLOADER or val == 0x0003C000:
                boot_refs.append((addr, "Bootloader base address"))
            elif val == 0x0003FC00 or val == 0x0003FBFF:
                boot_refs.append((addr, "Bootloader settings page"))
            elif val == 0x10001014:
                boot_refs.append((addr, "UICR.BOOTLOADERADDR"))
            elif val == 0x4000051C:  # GPREGRET
                boot_refs.append((addr, "POWER.GPREGRET (DFU trigger)"))
            elif val == 0x40000544:  # GPREGRET2
                boot_refs.append((addr, "POWER.GPREGRET2"))
            elif val == 0xB1:  # Nordic DFU magic value for GPREGRET
                boot_refs.append((addr, "DFU magic value (0xB1)"))

        if boot_refs:
            print(f"  Found {len(boot_refs)} bootloader/DFU references:\n")
            for addr, desc in sorted(boot_refs):
                print(f"  0x{addr:08X}: {desc}")
        else:
            print(f"  No direct bootloader address references found in literal pool.\n")

        # Check for UICR references
        uicr_refs = []
        for addr, val in self.literal_pool.items():
            if 0x10001000 <= val < 0x10001400:
                uicr_refs.append((addr, val))

        if uicr_refs:
            print(f"\n  --- UICR References ---\n")
            uicr_names = {
                0x10001000: "UICR.CLENR0 (Code region 0 length)",
                0x10001004: "UICR.RBPCONF (Readback protection)",
                0x10001008: "UICR.XTALFREQ (Crystal frequency)",
                0x1000100C: "UICR.FWID (Firmware ID)",
                0x10001010: "UICR.BOOTLOADERADDR_OLD",
                0x10001014: "UICR.BOOTLOADERADDR",
                0x10001018: "UICR.NRFFW[0]",
                0x1000101C: "UICR.NRFFW[1]",
            }
            for addr, val in sorted(uicr_refs):
                desc = uicr_names.get(val, f"UICR+0x{val - 0x10001000:03X}")
                print(f"  0x{addr:08X}: references {desc}")

        # Check for SoftDevice-related constants
        sd_markers = []
        for addr, val in self.literal_pool.items():
            if val == 0x00018000:  # App base (S110)
                sd_markers.append((addr, "App base address (S110 end)"))
            elif val == 0x0001B000:  # App base (S130)
                sd_markers.append((addr, "App base address (S130 end)"))
            elif val == 0x00017FFF:
                sd_markers.append((addr, "SoftDevice end - 1"))

        if sd_markers:
            print(f"\n  --- SoftDevice Address Constants ---\n")
            for addr, desc in sorted(sd_markers):
                print(f"  0x{addr:08X}: {desc}")

        # Scan binary for bootloader-related byte patterns
        boot_patterns = {
            b'\x00\xC0\x03\x00': "0x0003C000 (bootloader address, LE)",
            b'\x00\xFC\x03\x00': "0x0003FC00 (bootloader settings, LE)",
            b'\x14\x10\x00\x10': "0x10001014 (UICR.BOOTLOADERADDR, LE)",
        }
        print(f"\n  --- Binary Pattern Scan ---\n")
        found_any = False
        for pattern, desc in boot_patterns.items():
            idx = 0
            while True:
                idx = self.data.find(pattern, idx)
                if idx == -1:
                    break
                print(f"  Offset 0x{idx:06X} (0x{self.base + idx:08X}): {desc}")
                found_any = True
                idx += 1
        if not found_any:
            print(f"  No bootloader address patterns found in binary.")

        # Check for GPREGRET patterns (used to trigger DFU on reset)
        gpregret_pattern = struct.pack('<I', 0x4000051C)  # POWER.GPREGRET address
        idx = self.data.find(gpregret_pattern)
        if idx != -1:
            print(f"\n  POWER.GPREGRET found at offset 0x{idx:06X} (0x{self.base + idx:08X})")
            print(f"  → This register is used to trigger DFU mode on reset.")
            print(f"  → Writing 0xB1 to GPREGRET and resetting enters Nordic DFU bootloader.")
        print()

    def _full_disassembly(self):
        """Produce annotated disassembly of key regions."""
        print(f"{'='*72}")
        print(f"  DISASSEMBLY — KEY REGIONS")
        print(f"{'='*72}\n")

        # Disassemble Reset Handler and first N instructions
        vt = struct.unpack_from('<2I', self.data, 0)
        reset = vt[1]
        if self.base <= reset < self.base + len(self.data):
            self._disassemble_region("Reset Handler", reset, 80)

        # Disassemble around flash-related strings
        flash_offsets = []
        for off, s in self.strings.items():
            s_lower = s.lower()
            if any(kw in s_lower for kw in ['flash operation error', 'flash write',
                                              'update succ', 'miio ble flash']):
                flash_offsets.append(off)

        # Find functions that reference these strings (by looking for ADR/LDR pointing near them)
        for str_off in flash_offsets:
            str_addr = self.base + str_off
            # Search for functions that load this string address
            for pool_addr, pool_val in self.literal_pool.items():
                if abs(pool_val - str_addr) < 16:
                    func_addr = self._find_function_start(pool_addr)
                    if func_addr:
                        name = self.strings.get(str_off, "")[:40]
                        self._disassemble_region(f'Function referencing "{name}"',
                                                  func_addr, 60)
                        break

        # Disassemble interrupt handlers
        vec_count = min(48, len(self.data) // 4)
        vt_full = struct.unpack_from(f'<{vec_count}I', self.data, 0)
        for i in [2, 3, 11, 15]:  # NMI, HardFault, SVCall, SysTick
            if i < len(vt_full) and vt_full[i] != 0:
                handler = vt_full[i]
                if self.base <= handler < self.base + len(self.data):
                    name = ["", "Reset", "NMI", "HardFault", "", "", "", "",
                            "", "", "", "SVCall", "", "", "PendSV", "SysTick"][i] if i < 16 else f"IRQ{i-16}"
                    self._disassemble_region(f"{name} Handler", handler, 30)

    def _find_function_start(self, addr):
        """Walk backwards from addr to find PUSH {... lr}."""
        offset = addr - self.base
        for i in range(offset, max(0, offset - 200), -2):
            if i + 2 <= len(self.data):
                hw = struct.unpack_from('<H', self.data, i)[0]
                if (hw & 0xFF00) == 0xB500:
                    return self.base + i
        return None

    def _disassemble_region(self, title, start_addr, num_instructions):
        """Disassemble a region of code."""
        print(f"\n  --- {title} @ 0x{start_addr:08X} ---\n")
        offset = start_addr - self.base
        if offset < 0 or offset >= len(self.data):
            print(f"    Address out of range")
            return

        count = 0
        while count < num_instructions and offset < len(self.data) - 1:
            addr = self.base + offset
            mnem, ops, size, target = self.decoder.decode_at(offset)

            # Annotations
            annotations = []
            if addr in self.functions:
                annotations.append(f"; {self.functions[addr]}")
            if addr in self.branch_targets:
                annotations.append("; branch target")
            if addr in self.call_targets:
                annotations.append(f"; called from {len(self.call_targets[addr])} places")

            # Format
            if size == 4:
                hw1 = struct.unpack_from('<H', self.data, offset)[0]
                hw2 = struct.unpack_from('<H', self.data, offset + 2)[0]
                hex_str = f"{hw1:04X} {hw2:04X}"
            else:
                hw = struct.unpack_from('<H', self.data, offset)[0]
                hex_str = f"{hw:04X}     "

            ann_str = "  " + " ".join(annotations) if annotations else ""
            print(f"    0x{addr:08X}: {hex_str}  {mnem:8s} {ops}{ann_str}")

            offset += size
            count += 1

            # Stop at function epilogue (POP {... pc} or BX LR)
            if mnem == 'pop' and 'pc' in ops:
                break
            if mnem == 'bx' and ops == 'lr':
                break

    def write_output(self, outfile):
        """Redirect analysis output to file."""
        import io
        old_stdout = sys.stdout
        sys.stdout = buffer = io.StringIO()
        self.analyze()
        sys.stdout = old_stdout
        output = buffer.getvalue()
        with open(outfile, 'w', encoding='utf-8') as f:
            f.write(output)
        print(f"Analysis written to: {outfile}")
        print(f"Output size: {len(output)} bytes")
        return output


# ============================================================================
#  Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description="nRF51822 Firmware Analyzer for Ninebot G30 Max BLE")
    parser.add_argument('firmware', help='Path to BLE firmware .bin file')
    parser.add_argument('--output', '-o', help='Output file (default: stdout)')
    parser.add_argument('--base', type=lambda x: int(x, 0), default=NRF51_APP_BASE,
                        help=f'Base address (default: 0x{NRF51_APP_BASE:08X})')
    args = parser.parse_args()

    if not os.path.exists(args.firmware):
        print(f"Error: File not found: {args.firmware}")
        sys.exit(1)

    analyzer = NRF51Analyzer(args.firmware)
    analyzer.base = args.base
    analyzer.decoder = ThumbDecoder(analyzer.data, args.base)

    if args.output:
        analyzer.write_output(args.output)
    else:
        analyzer.analyze()


if __name__ == '__main__':
    main()
