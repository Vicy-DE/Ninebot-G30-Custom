#!/usr/bin/env python3
"""
Recursive-descent disassembler for STM32F103 Thumb firmware.

Produces a GNU assembler (.s) source file that reassembles into
a bit-identical binary. Works by:
  1. Extracting vector table entry points
  2. Recursively following all branch/call targets
  3. Emitting discovered code as Thumb instructions
  4. Emitting everything else as .byte data
"""

import sys
import struct
import re
from pathlib import Path

try:
    import capstone
except ImportError:
    print("ERROR: pip install capstone", file=sys.stderr)
    sys.exit(1)


# ── Configuration ────────────────────────────────────────────────────────────
BASE_ADDR      = 0x08001000   # Application start (after bootloader)
FLASH_START    = 0x08000000
FLASH_END      = 0x08020000   # 128 KB flash for STM32F103CBT6
NUM_VECTORS    = 76           # Cortex-M3 vector table entries (NVIC up to IRQ59)
THUMB_BIT      = 1            # Bit 0 set = Thumb mode


# ── Helper functions ─────────────────────────────────────────────────────────

def is_valid_flash_addr(addr):
    """Check if address falls within STM32F103 flash range."""
    return FLASH_START <= addr < FLASH_END


def addr_to_offset(addr):
    """Convert flash address to file offset."""
    return addr - BASE_ADDR


def offset_to_addr(offset):
    """Convert file offset to flash address."""
    return offset + BASE_ADDR


# ── Recursive Descent Disassembler ───────────────────────────────────────────

class FirmwareDisassembler:
    def __init__(self, binary_data, base_addr=BASE_ADDR):
        self.data = binary_data
        self.size = len(binary_data)
        self.base = base_addr
        self.end_addr = base_addr + self.size

        # Capstone disassembler for ARM Thumb
        self.cs = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
        self.cs.detail = True
        self.cs.skipdata = True

        # Classification maps
        self.code_bytes = set()          # Set of offsets classified as code
        self.visited_functions = set()   # Set of function entry addresses
        self.branch_targets = set()      # All branch/call targets found
        self.data_refs = set()           # Addresses referenced as data (literal pools)

        # instruction cache: offset -> (address, size, mnemonic, op_str, bytes_hex)
        self.insn_cache = {}

        # Label tracking
        self.labels = {}  # addr -> label_name
        self.label_counter = 0

    def _read_u32(self, offset):
        if offset + 4 <= self.size:
            return struct.unpack_from('<I', self.data, offset)[0]
        return None

    def _read_u16(self, offset):
        if offset + 2 <= self.size:
            return struct.unpack_from('<H', self.data, offset)[0]
        return None

    def get_label(self, addr, prefix="loc"):
        """Get or create a label for an address."""
        if addr not in self.labels:
            self.label_counter += 1
            self.labels[addr] = f"{prefix}_{addr:08X}"
        return self.labels[addr]

    def extract_vector_table(self):
        """Extract code entry points from the Cortex-M3 vector table."""
        entries = []
        for i in range(NUM_VECTORS):
            offset = i * 4
            val = self._read_u32(offset)
            if val is None:
                break

            if i == 0:
                # First entry is initial SP value, not a code address
                entries.append(('SP_INIT', val))
                continue

            # Clear Thumb bit to get actual address
            addr = val & ~THUMB_BIT
            if is_valid_flash_addr(addr) and self.base <= addr < self.end_addr:
                entries.append((f'vector_{i}', addr))
                self.branch_targets.add(addr)
            else:
                entries.append((f'vector_{i}_raw', val))

        return entries

    def disassemble_at(self, addr, max_bytes=None):
        """Disassemble instructions starting at addr, returns list of instructions."""
        offset = addr_to_offset(addr)
        if offset < 0 or offset >= self.size:
            return []

        end_off = self.size if max_bytes is None else min(offset + max_bytes, self.size)
        code = self.data[offset:end_off]
        return list(self.cs.disasm(code, addr))

    def follow_code(self, entry_addr):
        """
        Recursively follow code flow from entry_addr.
        Uses a worklist algorithm to discover all reachable code.
        """
        if entry_addr in self.visited_functions:
            return
        if not (self.base <= entry_addr < self.end_addr):
            return

        worklist = [entry_addr]
        self.visited_functions.add(entry_addr)
        self.get_label(entry_addr, "func")

        while worklist:
            addr = worklist.pop()
            offset = addr_to_offset(addr)

            if offset < 0 or offset >= self.size:
                continue
            if offset in self.code_bytes:
                continue

            # Disassemble from this address
            remaining = self.size - offset
            code = self.data[offset:offset + min(remaining, 4096)]
            instructions = list(self.cs.disasm(code, addr))

            for insn in instructions:
                insn_offset = addr_to_offset(insn.address)

                # Skip if already classified
                if insn_offset in self.code_bytes:
                    break

                # Mark bytes as code
                for b in range(insn.size):
                    self.code_bytes.add(insn_offset + b)

                # Cache the instruction
                bytes_hex = self.data[insn_offset:insn_offset + insn.size].hex().upper()
                self.insn_cache[insn_offset] = (
                    insn.address, insn.size, insn.mnemonic, insn.op_str, bytes_hex
                )

                # Analyze for branches and calls
                mnemonic = insn.mnemonic.lower()
                op_str = insn.op_str.strip()

                # Detect branch/call targets
                is_branch = mnemonic.startswith('b') and mnemonic not in ('bfc', 'bfi', 'bic', 'bkpt')
                is_call = mnemonic in ('bl', 'blx')
                is_cbz = mnemonic in ('cbz', 'cbnz')

                if is_branch or is_call or is_cbz:
                    target = self._extract_branch_target(insn, op_str)
                    if target is not None and self.base <= target < self.end_addr:
                        self.branch_targets.add(target)
                        if target not in self.visited_functions:
                            target_offset = addr_to_offset(target)
                            if target_offset not in self.code_bytes:
                                if is_call:
                                    # Calls create new function entries
                                    self.visited_functions.add(target)
                                    self.get_label(target, "func")
                                else:
                                    self.get_label(target, "loc")
                                worklist.append(target)

                # Detect literal pool references (LDR Rn, [PC, #imm])
                if mnemonic in ('ldr', 'ldr.w'):
                    lit_addr = self._extract_literal_pool_addr(insn)
                    if lit_addr is not None:
                        self.data_refs.add(lit_addr)

                # Stop following at unconditional branches / returns
                is_unconditional_branch = (
                    mnemonic == 'b' or
                    mnemonic == 'b.w' or
                    mnemonic == 'bx'
                )
                is_return = (
                    (mnemonic == 'pop' and 'pc' in op_str) or
                    (mnemonic == 'bx' and op_str == 'lr') or
                    (mnemonic == 'pop.w' and 'pc' in op_str)
                )

                if is_return or is_unconditional_branch:
                    # For unconditional branch (not return), follow the target
                    if is_unconditional_branch and not is_return:
                        target = self._extract_branch_target(insn, op_str)
                        if target and self.base <= target < self.end_addr:
                            target_off = addr_to_offset(target)
                            if target_off not in self.code_bytes:
                                worklist.append(target)
                    break

                # Handle table branches (tbb, tbh) - stop following
                if mnemonic in ('tbb', 'tbh'):
                    break

    def _extract_branch_target(self, insn, op_str):
        """Extract the target address from a branch instruction."""
        # Direct branch: b #0x08001234 or bl #0x08001234
        m = re.search(r'#(0x[0-9a-fA-F]+)', op_str)
        if m:
            try:
                return int(m.group(1), 16)
            except ValueError:
                pass

        # CBZ/CBNZ: cbnz r0, #0x08001234
        parts = op_str.split(',')
        if len(parts) >= 2:
            m = re.search(r'#(0x[0-9a-fA-F]+)', parts[-1].strip())
            if m:
                try:
                    return int(m.group(1), 16)
                except ValueError:
                    pass
        return None

    def _extract_literal_pool_addr(self, insn):
        """Extract the literal pool address from ldr Rn, [pc, #imm]."""
        op_str = insn.op_str
        if '[pc' in op_str.lower():
            m = re.search(r'#(0x[0-9a-fA-F]+|\d+)', op_str.split(',')[-1])
            if m:
                try:
                    imm = int(m.group(1), 16) if '0x' in m.group(1) else int(m.group(1))
                    # PC is aligned to word boundary, + 4 for pipeline
                    pc_val = (insn.address + 4) & ~3
                    return pc_val + imm
                except ValueError:
                    pass
        return None

    def analyze(self):
        """Run the full analysis pipeline."""
        print("  [1/4] Extracting vector table...", flush=True)
        vectors = self.extract_vector_table()

        # Label known vectors
        vector_names = [
            'initial_sp', 'Reset_Handler', 'NMI_Handler', 'HardFault_Handler',
            'MemManage_Handler', 'BusFault_Handler', 'UsageFault_Handler',
            'Reserved_7', 'Reserved_8', 'Reserved_9', 'Reserved_10',
            'SVC_Handler', 'DebugMon_Handler', 'Reserved_13', 'PendSV_Handler',
            'SysTick_Handler',
            # STM32F103 IRQs
            'WWDG_IRQHandler', 'PVD_IRQHandler', 'TAMPER_IRQHandler',
            'RTC_IRQHandler', 'FLASH_IRQHandler', 'RCC_IRQHandler',
            'EXTI0_IRQHandler', 'EXTI1_IRQHandler', 'EXTI2_IRQHandler',
            'EXTI3_IRQHandler', 'EXTI4_IRQHandler', 'DMA1_Channel1_IRQHandler',
            'DMA1_Channel2_IRQHandler', 'DMA1_Channel3_IRQHandler',
            'DMA1_Channel4_IRQHandler', 'DMA1_Channel5_IRQHandler',
            'DMA1_Channel6_IRQHandler', 'DMA1_Channel7_IRQHandler',
            'ADC1_2_IRQHandler', 'USB_HP_CAN1_TX_IRQHandler',
            'USB_LP_CAN1_RX0_IRQHandler', 'CAN1_RX1_IRQHandler',
            'CAN1_SCE_IRQHandler', 'EXTI9_5_IRQHandler',
            'TIM1_BRK_IRQHandler', 'TIM1_UP_IRQHandler',
            'TIM1_TRG_COM_IRQHandler', 'TIM1_CC_IRQHandler',
            'TIM2_IRQHandler', 'TIM3_IRQHandler', 'TIM4_IRQHandler',
            'I2C1_EV_IRQHandler', 'I2C1_ER_IRQHandler',
            'I2C2_EV_IRQHandler', 'I2C2_ER_IRQHandler',
            'SPI1_IRQHandler', 'SPI2_IRQHandler',
            'USART1_IRQHandler', 'USART2_IRQHandler', 'USART3_IRQHandler',
            'EXTI15_10_IRQHandler', 'RTCAlarm_IRQHandler',
            'USBWakeUp_IRQHandler',
        ]

        code_entries = []
        for i, (name, val) in enumerate(vectors):
            if i == 0:
                continue  # Skip SP
            addr = val & ~THUMB_BIT
            if is_valid_flash_addr(addr) and self.base <= addr < self.end_addr:
                if i < len(vector_names):
                    label = vector_names[i]
                else:
                    label = f"IRQ{i - 16}_Handler"
                self.labels[addr] = label
                code_entries.append(addr)

        print(f"     Found {len(code_entries)} valid vector entries", flush=True)

        # Add known protocol functions from previous analysis
        known_funcs = {
            0x08002720: 'calculateChecksum',
            0x080036AC: 'buildPacket',
            0x08007128: 'parseProtocolByte_USART1',
            0x08007468: 'parseProtocolByte_USART2',
            0x080077AC: 'parseProtocolByte_USART3',
            0x080071F4: 'enqueuePacket_USART1',
            0x08007534: 'enqueuePacket_USART2',
            0x08007878: 'enqueuePacket_USART3',
            0x08007610: 'uartTransmitHandler_USART1',
            0x08006F80: 'uartTransmitHandler_USART2',
            0x080072D0: 'uartTransmitHandler_USART3',
            0x08005468: 'dispatchReceivedPacket',
            0x0800700C: 'usart2_init',
        }
        for addr, name in known_funcs.items():
            if self.base <= addr < self.end_addr:
                self.labels[addr] = name
                code_entries.append(addr)

        print(f"  [2/4] Recursive descent from {len(code_entries)} entry points...", flush=True)
        for addr in code_entries:
            self.follow_code(addr)

        # Second pass: scan for functions we may have missed
        # Look for push {..., lr} patterns that indicate function prologues
        print("  [3/4] Scanning for undiscovered function prologues...", flush=True)
        additional = 0
        for offset in range(0, self.size - 2, 2):
            if offset in self.code_bytes:
                continue
            hw = self._read_u16(offset)
            if hw is None:
                continue
            # push {r4, lr} = 0xB510, push {r4,r5,lr} = 0xB530,
            # push {r4,r5,r6,lr} = 0xB570, push {r4,r5,r6,r7,lr} = 0xB5F0
            # More generally: 0xB5xx where bit 8 is set (LR is pushed)
            if (hw & 0xFF00) == 0xB500:
                addr = offset_to_addr(offset)
                if addr not in self.visited_functions:
                    self.follow_code(addr)
                    additional += 1
            # push.w {r1..r11, lr}: 0xE92D 0x4xxx or 0xE92D 0x5xxx etc
            if hw == 0xE92D and offset + 4 <= self.size:
                hw2 = self._read_u16(offset + 2)
                if hw2 is not None and (hw2 & 0x4000):  # LR bit set
                    addr = offset_to_addr(offset)
                    if addr not in self.visited_functions:
                        self.follow_code(addr)
                        additional += 1

        print(f"     Found {additional} additional functions via prologue scan", flush=True)

        code_pct = len(self.code_bytes) / self.size * 100
        print(f"  [4/4] Analysis complete: {len(self.code_bytes)}/{self.size} bytes "
              f"classified as code ({code_pct:.1f}%)", flush=True)
        print(f"     Functions: {len(self.visited_functions)}, "
              f"Labels: {len(self.labels)}, "
              f"Branch targets: {len(self.branch_targets)}", flush=True)

    def generate_asm(self, firmware_name="DRV_1.6.13"):
        """
        Generate GNU assembler source file.

        ALL instructions are emitted as raw .byte directives with the
        disassembled mnemonic/operands as comments.  This guarantees
        bit-identical reassembly regardless of encoding ambiguities
        (narrow vs wide branches, literal pool addressing, etc.).

        Labels are placed at function and branch-target addresses for
        readability but do NOT affect code generation.
        """
        lines = []

        # Header
        lines.append(f"@ ============================================================================")
        lines.append(f"@ {firmware_name} - Disassembled Firmware (raw-byte reassembly)")
        lines.append(f"@ Target: STM32F103CBT6 (ARM Cortex-M3, Thumb)")
        lines.append(f"@ Base address: 0x{self.base:08X}")
        lines.append(f"@ Binary size: {self.size} bytes ({self.size:#x})")
        lines.append(f"@ Code coverage: {len(self.code_bytes)}/{self.size} bytes "
                      f"({len(self.code_bytes)/self.size*100:.1f}%)")
        lines.append(f"@ Functions: {len(self.visited_functions)}, "
                      f"Labels: {len(self.labels)}")
        lines.append(f"@ Generated by recursive-descent disassembler")
        lines.append(f"@ ============================================================================")
        lines.append("")
        lines.append("    .syntax unified")
        lines.append("    .cpu cortex-m3")
        lines.append("    .thumb")
        lines.append("")
        lines.append("    .section .text")
        lines.append(f"    .org 0")
        lines.append("")

        # Emit the entire binary as raw bytes with annotation
        offset = 0
        while offset < self.size:
            addr = offset_to_addr(offset)

            # ---- Labels ----
            if addr in self.labels:
                lines.append("")
                lines.append(f"@ ---- {self.labels[addr]} ----")
                lines.append(f"{self.labels[addr]}:  @ 0x{addr:08X}")
            elif addr in self.branch_targets:
                lbl = self.get_label(addr, "loc")
                lines.append(f"{lbl}:  @ 0x{addr:08X}")

            # ---- Code or data? ----
            if offset in self.insn_cache:
                iaddr, isize, mnemonic, op_str, bhex = self.insn_cache[offset]
                raw = self.data[offset:offset + isize]
                byte_str = ', '.join(f'0x{b:02X}' for b in raw)
                # Resolve branch target names in the comment
                comment_op = self._resolve_label_in_comment(op_str)
                padded = f"    .byte {byte_str}"
                lines.append(f"{padded:<44s} @ {addr:08X}: {mnemonic} {comment_op}")
                offset += isize
            else:
                # Data — batch consecutive data bytes up to next label or code
                data_start = offset
                while (offset < self.size and
                       offset not in self.insn_cache and
                       (offset == data_start or
                        (offset_to_addr(offset) not in self.labels and
                         offset_to_addr(offset) not in self.branch_targets))):
                    offset += 1

                data_len = offset - data_start
                self._emit_data_block(lines, data_start, data_len)

        lines.append("")
        lines.append("    .end")
        lines.append("")

        return '\n'.join(lines)

    def _resolve_label_in_comment(self, op_str):
        """Replace addresses in operand string with label names for comments."""
        def _replace_addr(m):
            val = int(m.group(1), 16)
            if val in self.labels:
                return self.labels[val]
            target = val & ~THUMB_BIT
            if target in self.labels:
                return self.labels[target]
            return m.group(0)
        return re.sub(r'#(0x[0-9a-fA-F]+)', _replace_addr, op_str)

    def _emit_data_block(self, lines, start_offset, length):
        """Emit a block of data as .byte directives."""
        if length == 0:
            return

        addr = offset_to_addr(start_offset)

        # Try to emit as .word where aligned
        off = start_offset
        end = start_offset + length

        while off < end:
            curr_addr = offset_to_addr(off)

            # Check for labels in the middle of data
            if off != start_offset and curr_addr in self.labels:
                lines.append(f"    .thumb_func")
                lines.append(f"    .global {self.labels[curr_addr]}")
                lines.append(f"{self.labels[curr_addr]}:  @ 0x{curr_addr:08X}")
            elif off != start_offset and curr_addr in self.branch_targets:
                lbl = self.get_label(curr_addr, "loc")
                lines.append(f"{lbl}:  @ 0x{curr_addr:08X}")

            # Emit aligned .word if possible, otherwise .byte
            remaining = end - off
            if (off % 4 == 0) and remaining >= 4:
                val = self._read_u32(off)
                # Check if this word looks like a flash address that has a label
                if val is not None:
                    target = val & ~THUMB_BIT
                    if target in self.labels and is_valid_flash_addr(target):
                        lines.append(f"    .word {self.labels[target]} + {val & THUMB_BIT}   "
                                     f"@ {curr_addr:08X}: =0x{val:08X}")
                    else:
                        lines.append(f"    .word 0x{val:08X}                @ {curr_addr:08X}")
                    off += 4
                else:
                    b = self.data[off]
                    lines.append(f"    .byte 0x{b:02X}                      @ {curr_addr:08X}")
                    off += 1
            elif (off % 2 == 0) and remaining >= 2:
                val = self._read_u16(off)
                if val is not None:
                    lines.append(f"    .short 0x{val:04X}                    @ {curr_addr:08X}")
                    off += 2
                else:
                    b = self.data[off]
                    lines.append(f"    .byte 0x{b:02X}                      @ {curr_addr:08X}")
                    off += 1
            else:
                b = self.data[off]
                lines.append(f"    .byte 0x{b:02X}                      @ {curr_addr:08X}")
                off += 1


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <firmware.bin> [output.s] [base_addr_hex]")
        print(f"  Default base: 0x{BASE_ADDR:08X}")
        sys.exit(1)

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 else input_path.with_suffix('.s')
    base = int(sys.argv[3], 16) if len(sys.argv) > 3 else BASE_ADDR

    print(f"Reading {input_path}...", flush=True)
    data = input_path.read_bytes()
    print(f"  Size: {len(data)} bytes ({len(data):#x})", flush=True)

    fw_name = input_path.stem

    print(f"\nAnalyzing firmware (base=0x{base:08X})...", flush=True)
    disasm = FirmwareDisassembler(data, base)
    disasm.analyze()

    print(f"\nGenerating assembly source...", flush=True)
    asm_source = disasm.generate_asm(fw_name)

    output_path.write_text(asm_source, encoding='utf-8')
    asm_lines = asm_source.count('\n')
    print(f"Wrote {output_path} ({output_path.stat().st_size} bytes, {asm_lines} lines)", flush=True)
    print(f"\nDone! Build with:", flush=True)
    print(f"  arm-none-eabi-as -mcpu=cortex-m3 -mthumb {output_path.name} -o firmware.o", flush=True)
    print(f"  arm-none-eabi-ld -T linker.ld firmware.o -o firmware.elf", flush=True)
    print(f"  arm-none-eabi-objcopy -O binary firmware.elf firmware.bin", flush=True)


if __name__ == '__main__':
    main()
