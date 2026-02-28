; =============================================================================
; Ninebot G30 Max - Annotated ARM Thumb Assembly
; =============================================================================
;
; Source: DRV_1.6.13_Compat.bin (ESC Motor Controller)
; Target: STM32F103CBT6 ARM Cortex-M3, Thumb instruction set
; Base address: 0x08001000 (application entry, after bootloader)
;
; This file contains the key protocol functions extracted and annotated
; from the stock firmware binary. See ninebot_protocol_reconstructed.cpp
; for the C++ equivalent code.
;
; Register conventions (ARM EABI / Thumb):
;   r0-r3:  Function arguments and scratch registers
;   r4-r11: Callee-saved registers
;   r12/ip: Intra-procedure scratch register
;   r13/sp: Stack pointer
;   r14/lr: Link register (return address)
;   r15/pc: Program counter
;
; =============================================================================


; =============================================================================
; FUNCTION: calculateChecksum
; Address:  0x08002720 - 0x08002738
; Size:     26 bytes
; =============================================================================
;
; uint16_t calculateChecksum(const uint8_t* data, uint8_t length)
;
; Computes Ninebot protocol checksum: ~(sum of bytes) & 0xFFFF
; Called by buildPacket to generate the 2-byte trailer.
;
; Parameters:
;   r0 = const uint8_t* data    (pointer to checksum input, starts at LEN field)
;   r1 = uint8_t length         (number of bytes to sum)
;
; Returns:
;   r0 = uint16_t checksum      (~sum & 0xFFFF)
;
; C equivalent:
;   uint16_t sum = 0;
;   for (uint8_t i = 0; i < length; i++) sum += data[i];
;   return ~sum & 0xFFFF;
;
calculateChecksum:
    08002720: F0 B5       push    {r4, lr}           ; Save r4 and return address
    08002722: 00 23       movs    r3, #0             ; sum = 0
    08002724: 00 22       movs    r2, #0             ; i = 0
    08002726: 02 E0       b       .checkLoop         ; Jump to loop condition check

.sumByte:                                             ; --- Loop body ---
    08002728: 14 5C       ldrb    r4, [r0, r2]       ; r4 = data[i]
    0800272A: 1B 19       add     r3, r4             ; sum += data[i]
    0800272C: 9B B2       uxth    r3, r3             ; sum &= 0xFFFF (zero-extend to 16-bit)
    0800272E: 52 1C       adds    r2, r2, #1         ; i++

.checkLoop:                                           ; --- Loop condition ---
    08002730: 8A 42       cmp     r2, r1             ; i < length?
    08002732: F9 D3       blo     .sumByte           ; Yes -> continue summing

    08002734: D8 43       mvns    r0, r3             ; r0 = ~sum (bitwise NOT)
    08002736: 80 B2       uxth    r0, r0             ; r0 &= 0xFFFF (mask to 16-bit)
    08002738: F0 BD       pop     {r4, pc}           ; Return checksum in r0


; =============================================================================
; FUNCTION: buildPacket
; Address:  0x080036AC - 0x080036F6
; Size:     75 bytes
; =============================================================================
;
; uint8_t buildPacket(uint8_t srcAddr, uint8_t dstAddr, uint8_t payloadLength,
;                     uint8_t command, uint8_t argument,
;                     const uint8_t* payload, uint8_t* outputBuffer)
;
; Constructs a complete Ninebot protocol packet:
;   [0x5A][0xA5][LEN][SRC][DST][CMD][ARG][PAYLOAD...][CHK_LO][CHK_HI]
;
; Parameters (ARM calling convention):
;   r0  = srcAddr          (source device address, e.g. 0x20 = ESC)
;   r1  = dstAddr          (destination address)
;   r2  = payloadLength    (number of payload bytes, also written as LEN)
;   r3  = command          (command byte)
;   sp+0x14 = argument     (loaded into ip/r12)
;   sp+0x18 = payload      (loaded into r7)
;   sp+0x1C = outputBuffer (loaded into r4)
;
; Returns:
;   r0 = total bytes written (via implicit register state at return)
;
buildPacket:
    080036AC: F0 B5          push    {r4, r5, r6, r7, lr}      ; Save callee-saved regs
    080036AE: DD E9 05 C7    ldrd    ip, r7, [sp, #0x14]       ; ip = argument, r7 = payload ptr
    080036B2: 07 9C          ldr     r4, [sp, #0x1C]           ; r4 = outputBuffer

    ; --- Write protocol header ---
    080036B4: 00 26          movs    r6, #0                    ; payloadIndex = 0
    080036B6: 5A 25          movs    r5, #0x5A                 ; HEADER_BYTE_1
    080036B8: 25 70          strb    r5, [r4, #0]              ; buf[0] = 0x5A
    080036BA: A5 25          movs    r5, #0xA5                 ; HEADER_BYTE_2
    080036BC: 65 70          strb    r5, [r4, #1]              ; buf[1] = 0xA5

    ; --- Write packet metadata ---
    080036BE: A2 70          strb    r2, [r4, #2]              ; buf[2] = payloadLength (LEN)
    080036C0: E0 70          strb    r0, [r4, #3]              ; buf[3] = srcAddr
    080036C2: 21 71          strb    r1, [r4, #4]              ; buf[4] = dstAddr
    080036C4: 63 71          strb    r3, [r4, #5]              ; buf[5] = command
    080036C6: 07 25          movs    r5, #7                    ; bufferIndex = 7 (payload start)
    080036C8: 84 F8 06 C0    strb.w  ip, [r4, #6]              ; buf[6] = argument
    080036CC: 06 E0          b       .copyCheck                ; Jump to loop condition

.copyByte:                                                     ; --- Payload copy loop ---
    080036CE: 28 46          mov     r0, r5                    ; r0 = bufferIndex (save for strb)
    080036D0: B9 5D          ldrb    r1, [r7, r6]              ; r1 = payload[payloadIndex]
    080036D2: 6D 1C          adds    r5, r5, #1                ; bufferIndex++
    080036D4: ED B2          uxtb    r5, r5                    ; Mask to 8-bit
    080036D6: 21 54          strb    r1, [r4, r0]              ; buf[old_bufferIndex] = payload byte
    080036D8: 76 1C          adds    r6, r6, #1                ; payloadIndex++
    080036DA: F6 B2          uxtb    r6, r6                    ; Mask to 8-bit

.copyCheck:
    080036DC: 96 42          cmp     r6, r2                    ; payloadIndex < payloadLength?
    080036DE: F6 D3          blo     .copyByte                 ; Yes -> copy next byte

    ; --- Calculate checksum over [LEN..PAYLOAD] ---
    080036E0: A9 1E          subs    r1, r5, #2                ; checksumLen = bufIdx - 2
    080036E2: A0 1C          adds    r0, r4, #2                ; checksumPtr = &buf[2] (skip header)
    080036E4: FF F7 1C F8    bl      calculateChecksum          ; r0 = calculateChecksum(ptr, len)

    ; --- Append 16-bit checksum (little-endian) ---
    080036E8: 69 1C          adds    r1, r5, #1                ; r1 = bufferIndex + 1 (CHK_HI pos)
    080036EA: C9 B2          uxtb    r1, r1                    ; Mask to 8-bit
    080036EC: 60 55          strb    r0, [r4, r5]              ; buf[bufIdx] = checksum & 0xFF
    080036EE: 02 0A          lsrs    r2, r0, #8                ; r2 = checksum >> 8
    080036F0: 48 1C          adds    r0, r1, #1                ; (total size in r0)
    080036F2: C0 B2          uxtb    r0, r0                    ; Mask to 8-bit
    080036F4: 62 54          strb    r2, [r4, r1]              ; buf[bufIdx+1] = checksum >> 8
    080036F6: F0 BD          pop     {r4, r5, r6, r7, pc}      ; Return


; =============================================================================
; FUNCTION: parseProtocolByte (USART1 variant)
; Address:  0x08007128 - 0x080071B4
; Size:     142 bytes
; =============================================================================
;
; void parseProtocolByte(uint8_t incomingByte)
;
; State machine that receives one byte at a time from UART and assembles
; a complete protocol packet. Verifies checksum and dispatches valid packets.
;
; Three identical copies exist for USART1/2/3, differing only in:
;   - State struct pointer (loaded from PC-relative literal)
;   - RX buffer pointer (loaded from PC-relative literal)
;   - Channel ID passed to dispatchReceivedPacket (0/1/2)
;
; Parameters:
;   r0 = uint8_t incomingByte (the byte just received from USART_DR)
;
; SRAM references:
;   parserState = 0x200003AC (loaded at 0x0800712A via [pc, #0x8C])
;   rxBuffer    = 0x200010F8 (loaded at 0x08007134 via [pc, #0x84])
;
parseProtocolByte_USART1:
    08007128: 70 B5          push    {r4, r5, r6, lr}         ; Save registers
    0800712A: 23 4C          ldr     r4, [pc, #0x8C]          ; r4 = &parserState [=0x200003AC]
    0800712C: 00 25          movs    r5, #0                   ; r5 = 0 (constant for clearing)

    ; ---- Check current state ----
    0800712E: 21 7A          ldrb    r1, [r4, #8]             ; r1 = receivingPacket flag
    08007130: 00 29          cmp     r1, #0
    08007132: 2D D0          beq     .headerDetect            ; if (!receiving) -> check header

; ============== RECEIVING STATE ==============

    ; Store incoming byte in RX buffer
    08007134: 21 4B          ldr     r3, [pc, #0x84]          ; r3 = &rxBuffer [=0x200010F8]
    08007136: 61 7A          ldrb    r1, [r4, #9]             ; r1 = rxByteIndex
    08007138: 58 54          strb    r0, [r3, r1]             ; rxBuffer[rxByteIndex] = incomingByte

    ; On first data byte: calculate expected packet length
    0800713A: 41 B9          cbnz    r1, .advanceIndex        ; if (rxByteIndex != 0) skip
    0800713C: C2 1D          adds    r2, r0, #7               ; expectedLen = byte + 7
    0800713E: D2 B2          uxtb    r2, r2                   ; Mask to 8-bit
    08007140: A2 72          strb    r2, [r4, #0xA]           ; Store expectedRxLength

    ; Overflow check: reject if too long
    08007142: F3 2A          cmp     r2, #0xF3                ; MAX_PACKET_DATA_SIZE = 243
    08007144: 03 D9          bls     .advanceIndex            ; if (len <= 243) -> OK

    ; OVERFLOW: Reset and return
    08007146: 25 72          strb    r5, [r4, #8]             ; receivingPacket = 0
    08007148: E5 71          strb    r5, [r4, #7]             ; gotFirstHeader = 0
    0800714A: A5 81          strh    r5, [r4, #0xC]           ; rxRunningChecksum = 0
    0800714C: 70 BD          pop     {r4, r5, r6, pc}         ; return

.advanceIndex:
    ; Increment byte counter
    0800714E: 49 1C          adds    r1, r1, #1               ; rxByteIndex++
    08007150: C9 B2          uxtb    r1, r1                   ; Mask to 8-bit
    08007152: 61 72          strb    r1, [r4, #9]             ; Store new index

    ; Load running checksum and expected length
    08007154: 18 4A          ldr     r2, [pc, #0x60]          ; r2 = &parserState [same struct]
    08007156: A6 7A          ldrb    r6, [r4, #0xA]           ; r6 = expectedRxLength
    08007158: 92 89          ldrh    r2, [r2, #0xC]           ; r2 = rxRunningChecksum

    ; Check if packet is complete
    0800715A: B1 42          cmp     r1, r6                   ; rxByteIndex == expectedRxLength?
    0800715C: 15 D1          bne     .accumulate              ; No -> accumulate and return

; ============== CHECKSUM VERIFICATION ==============

    ; Extract received checksum bytes from end of buffer
    0800715E: 58 18          adds    r0, r3, r1               ; r0 = &rxBuffer[expectedRxLength]
    08007160: 10 F8 02 1D    ldrb    r1, [r0, #-2]!           ; r1 = rxBuf[len-2] = CHK_LO
                                                               ;   r0 now points to rxBuf[len-2]

    ; Calculate expected checksum
    ;   runningSum has accumulated bytes [0..len-2]
    ;   Subtract the CHK_LO byte that was included in the sum
    ;   Result: sum of data bytes only (without checksum bytes)
    08007164: 52 1A          subs    r2, r2, r1               ; adjustedSum = runningSum - chkLo
    08007166: D2 43          mvns    r2, r2                   ; calculated = ~adjustedSum
    08007168: 92 B2          uxth    r2, r2                   ; calculated &= 0xFFFF

    08007169: A2 81          strh    r2, [r4, #0xC]           ; (debug: store calculated checksum)

    ; Reconstruct received checksum (little-endian)
    0800716C: 40 78          ldrb    r0, [r0, #1]             ; r0 = rxBuf[len-1] = CHK_HI
    0800716E: 01 EB 00 20    add.w   r0, r1, r0, lsl #8       ; received = chkLo | (chkHi << 8)
    08007172: 80 B2          uxth    r0, r0                   ; received &= 0xFFFF

    ; Compare checksums
    08007174: 82 42          cmp     r2, r0                   ; calculated == received?
    08007176: 03 D1          bne     .resetState              ; Mismatch -> discard

    ; CHECKSUM PASSED: Dispatch the valid packet
    08007178: 10 49          ldr     r1, [pc, #0x40]          ; r1 = &rxBuffer (for dispatch)
    0800717A: 01 20          movs    r0, #1                   ; r0 = channel 1 (USART1)
    0800717C: FE F7 74 F9    bl      dispatchReceivedPacket   ; dispatch(channel=1, buffer)

.resetState:
    ; Reset parser to IDLE regardless of checksum result
    08007180: 25 72          strb    r5, [r4, #8]             ; receivingPacket = 0
    08007182: E5 71          strb    r5, [r4, #7]             ; gotFirstHeader = 0
    08007184: 65 72          strb    r5, [r4, #9]             ; rxByteIndex = 0
    08007186: A5 81          strh    r5, [r4, #0xC]           ; rxRunningChecksum = 0
    08007188: 70 BD          pop     {r4, r5, r6, pc}         ; return

.accumulate:
    ; Packet not complete: add current byte to running checksum
    0800718A: 10 44          add     r0, r2                   ; sum = incomingByte + runningSum
    0800718C: A0 81          strh    r0, [r4, #0xC]           ; rxRunningChecksum = sum
    0800718E: 70 BD          pop     {r4, r5, r6, pc}         ; return

; ============== IDLE STATE: HEADER DETECTION ==============

.headerDetect:
    08007190: 09 49          ldr     r1, [pc, #0x24]          ; r1 = &parserState
    08007192: 01 22          movs    r2, #1                   ; r2 = 1 (flag value)

    ; Check for first header byte (0x5A)
    08007194: 5A 28          cmp     r0, #0x5A                ; incomingByte == 0x5A?
    08007196: C9 79          ldrb    r1, [r1, #7]             ; r1 = gotFirstHeader
    08007198: 00 D1          bne     .checkSecondByte         ; Not 0x5A -> check 0xA5
    0800719A: 11 B1          cbz     r1, .setFirstHeader      ; if (!gotFirstHeader) -> set it

.checkSecondByte:
    ; Check for second header byte (0xA5)
    0800719C: A5 28          cmp     r0, #0xA5                ; incomingByte == 0xA5?
    0800719E: 02 D0          beq     .startReceiving          ; Yes -> begin receiving
    080071A0: 05 E0          b       .resetAll                ; No -> reset everything

.setFirstHeader:
    ; Mark that we've seen the 0x5A byte, wait for 0xA5
    080071A2: E2 71          strb    r2, [r4, #7]             ; gotFirstHeader = 1
    080071A4: 70 BD          pop     {r4, r5, r6, pc}         ; return

.startReceiving:
    ; 0x5A followed by 0xA5: transition to RECEIVING state
    080071A6: 11 B1          cbz     r1, .resetAll             ; if (!gotFirstHeader) -> invalid
    080071A8: 22 72          strb    r2, [r4, #8]             ; receivingPacket = 1
    080071AA: A5 81          strh    r5, [r4, #0xC]           ; rxRunningChecksum = 0
    080071AC: 70 BD          pop     {r4, r5, r6, pc}         ; return

.resetAll:
    ; Invalid sequence: clear all parser state
    080071AE: 25 72          strb    r5, [r4, #8]             ; receivingPacket = 0
    080071B0: E5 71          strb    r5, [r4, #7]             ; gotFirstHeader = 0
    080071B2: A5 81          strh    r5, [r4, #0xC]           ; rxRunningChecksum = 0
    080071B4: 70 BD          pop     {r4, r5, r6, pc}         ; return

    ; Literal pool for parseProtocolByte_USART1:
    ;   [pc+0x8C] @ 0x080071B8 = 0x200003AC  (parserState for USART1)
    ;   [pc+0x84] @ 0x080071BC = 0x200010F8  (rxBuffer for USART1)
    ;   [pc+0x60] @ 0x080071B8 = 0x200003AC  (same parserState, for checksum)
    ;   [pc+0x40] @ 0x080071BC = 0x200010F8  (rxBuffer for dispatch)


; =============================================================================
; FUNCTION: uartTransmitHandler (USART1 variant)
; Address:  0x08007610 - 0x08007696
; Size:     136 bytes
; =============================================================================
;
; void uartTransmitHandler(void)
;
; Handles USART TX byte-by-byte from queued packet buffers.
; Called from USART1_IRQHandler or polled from main loop.
;
; Uses a 4-slot circular buffer. Each invocation sends one byte.
; When a slot is fully transmitted, advances to the next slot.
;
; SRAM references:
;   parserState = 0x200003AC
;   USART_SR    = 0x40013800  (USART1 status register)
;   USART_DR    = 0x40013804  (USART1 data register)
;   txBuffers   = 0x20000BFC  (TX buffer base)
;
uartTransmitHandler_USART1:
    08007610: 30 B5          push    {r4, r5, lr}             ; Save registers
    08007612: 21 4A          ldr     r2, [pc, #0x84]          ; r2 = &parserState

    ; Check mutual exclusion
    08007614: D0 78          ldrb    r0, [r2, #3]             ; r0 = txBusy
    08007616: 00 28          cmp     r0, #0
    08007618: 0B D1          bne     .txReturn                ; if (txBusy) return
    0800761A: 50 79          ldrb    r0, [r2, #5]             ; r0 = rxActive
    0800761C: 00 28          cmp     r0, #0
    0800761E: 08 D1          bne     .txReturn                ; if (rxActive) return

    ; Acquire lock
    08007620: 01 21          movs    r1, #1
    08007622: 51 71          strb    r1, [r2, #5]             ; rxActive = 1

    ; Check USART TXE (Transmit Data Register Empty)
    08007624: 1D 4B          ldr     r3, [pc, #0x74]          ; r3 = &USART1_SR [=0x40013800]
    08007626: 18 88          ldrh    r0, [r3]                 ; r0 = USART1_SR
    08007628: 04 06          lsls    r4, r0, #0x18            ; Test bit 7 (TXE) via shift
    0800762A: 4F F0 00 00    mov.w   r0, #0                   ; r0 = 0
    0800762E: 01 D4          bmi     .txReady                 ; if (TXE) -> send byte
    08007630: 50 71          strb    r0, [r2, #5]             ; rxActive = 0 (release)

.txReturn:
    08007632: 30 BD          pop     {r4, r5, pc}             ; return

.txReady:
    ; Check if there are packets to send
    08007634: 92 F9 01 40    ldrsb   r4, [r2, #1]             ; r4 = txWriteSlot
    08007638: 92 F9 00 50    ldrsb   r5, [r2]                 ; r5 = currentTxSlot
    0800763C: AC 42          cmp     r4, r5                   ; writeSlot == readSlot?
    0800763E: 07 D1          bne     .sendByte                ; No -> send next byte

    ; All packets transmitted: check TC and clean up
    08007640: 19 88          ldrh    r1, [r3]                 ; USART_SR
    08007642: 49 06          lsls    r1, r1, #0x19            ; Test bit 6 (TC)
    08007644: 02 D5          bpl     .txDone                  ; if (!TC) skip
    08007646: 91 79          ldrb    r1, [r2, #6]             ; txEnabled?
    08007648: 01 B1          cbz     r1, .txDone              ; if (!txEnabled) skip
    0800764A: 90 71          strb    r0, [r2, #6]             ; txEnabled = 0

.txDone:
    0800764C: 50 71          strb    r0, [r2, #5]             ; rxActive = 0
    0800764E: 30 BD          pop     {r4, r5, pc}             ; return

.sendByte:
    ; Enable TX if first time
    08007650: 93 79          ldrb    r3, [r2, #6]             ; txEnabled?
    08007652: 03 B9          cbnz    r3, .doSend              ; if already enabled, skip
    08007654: 91 71          strb    r1, [r2, #6]             ; txEnabled = 1

.doSend:
    ; Calculate byte address: txBuffers[currentTxSlot].packetData[txByteOffset]
    08007656: 12 4B          ldr     r3, [pc, #0x48]          ; r3 = &txBuffers base [=0x20000BFC]
    08007658: 92 F9 00 10    ldrsb   r1, [r2]                 ; r1 = currentTxSlot
    0800765C: 0F 4D          ldr     r5, [pc, #0x3C]          ; r5 = &USART1_DR [=0x40013800]
    0800765E: 03 EB 01 24    add.w   r4, r3, r1, lsl #8       ; r4 = &txBuffers[slot*256]
    08007662: 51 69          ldr     r1, [r2, #0x14]          ; r1 = txByteOffset
    08007664: 2D 1D          adds    r5, r5, #4               ; r5 = &USART_DR (base+4)
    08007666: 0C 44          add     r4, r1                   ; r4 += txByteOffset
    08007668: 24 79          ldrb    r4, [r4, #4]             ; r4 = slot.packetData[offset]
    0800766A: 2C 80          strh    r4, [r5]                 ; USART_DR = byte (start TX)

    ; Advance offset
    0800766C: 49 1C          adds    r1, r1, #1               ; txByteOffset++
    0800766E: 51 61          str     r1, [r2, #0x14]          ; Store new offset

    ; Check if packet fully sent
    08007670: 92 F9 00 40    ldrsb   r4, [r2]                 ; r4 = currentTxSlot
    08007674: 03 EB 04 23    add.w   r3, r3, r4, lsl #8       ; r3 = &txBuffers[currentSlot]
    08007678: 1B 68          ldr     r3, [r3]                 ; r3 = slot.packetLength
    0800767A: 8B 42          cmp     r3, r1                   ; packetLength > txByteOffset?
    0800767C: 0A DC          bgt     .txRelease               ; Yes -> more bytes remain

    ; Packet complete: advance to next slot
    0800767E: 50 61          str     r0, [r2, #0x14]          ; txByteOffset = 0
    08007680: 11 78          ldrb    r1, [r2]                 ; currentTxSlot++
    08007682: 49 1C          adds    r1, r1, #1
    08007684: 11 70          strb    r1, [r2]
    08007686: 11 78          ldrb    r1, [r2]
    08007688: 04 29          cmp     r1, #4                   ; Wrap around at 4
    0800768A: 00 D1          bne     .noWrap
    0800768C: 10 70          strb    r0, [r2]                 ; currentTxSlot = 0

.noWrap:
    0800768E: 91 78          ldrb    r1, [r2, #2]             ; pendingTxCount++
    08007690: 49 1C          adds    r1, r1, #1               ;   (free up the slot)
    08007692: 91 70          strb    r1, [r2, #2]

.txRelease:
    08007694: 50 71          strb    r0, [r2, #5]             ; rxActive = 0 (release lock)
    08007696: 30 BD          pop     {r4, r5, pc}             ; return


; =============================================================================
; FUNCTION: GPIO/USART pin configuration function (partial, data table)
; Address:  0x08006519 - region
; =============================================================================
;
; The UART initialization region at 0x08006519 in DRV_1.6.13 contains
; mostly data tables and configuration descriptors rather than clean
; executable code. The actual USART_Init() HAL function is called at
; 0x0800700C (see below).
;
; Key data observed:
;   0x08006551: GPIO config for USART alternate function pins
;   0x08006555: 0x4C = GPIO_Mode_AF_PP (Alternate Function Push-Pull)
;
; Actual USART2 initialization function at 0x0800700C:
;
usart2_init:
    0800700C: 10 B5          push    {r4, lr}
    0800700E: 8C B0          sub     sp, #0x30               ; Allocate USART_InitTypeDef on stack
    08007010: 12 4C          ldr     r4, [pc, #0x48]         ; r4 = USART2_BASE = 0x40004400
    08007012: 20 46          mov     r0, r4
    08007014: FB F7 D4 FD    bl      USART_DeInit             ; Reset USART2 to defaults

    ; Fill USART_InitTypeDef structure on stack:
    ;   BaudRate       = (configured via clock tree)
    ;   WordLength     = 0 (8-bit)
    ;   StopBits       = 0 (1 stop bit)
    ;   Parity         = 0 (none)
    ;   HWFlowControl  = 0 (none)
    ;   Mode           = TX | RX
    ;   RxBufferSize   = 0xFA (250 bytes)
    ;   DMABurstSize   = 0x80

    08007024: FA 22          movs    r2, #0xFA               ; rxBufferSize = 250
    08007026: 04 92          str     r2, [sp, #0x10]
    0800702A: 80 20          movs    r0, #0x80               ; DMA transfer size
    0800702C: 06 90          str     r0, [sp, #0x18]
    08007032: 20 20          movs    r0, #0x20               ; RXNE interrupt enable
    08007034: 09 90          str     r0, [sp, #0x24]
    08007036: 00 02          lsls    r0, r0, #8              ; = 0x2000 (additional flag)
    08007038: 0A 90          str     r0, [sp, #0x28]

    0800703C: 01 A9          add     r1, sp, #4              ; r1 = &initStruct
    0800703E: 20 46          mov     r0, r4                  ; r0 = USART2_BASE
    08007040: FB F7 3A FE    bl      USART_Init               ; USART_Init(USART2, &config)

    ; Enable NVIC interrupt
    08007044: 06 48          ldr     r0, [pc, #0x18]         ; NVIC interrupt config
    08007046: 01 22          movs    r2, #1                  ; Enable
    08007048: 40 21          movs    r1, #0x40               ; Priority
    0800704A: 00 1F          subs    r0, r0, #4
    0800704C: 00 F0 67 FC    bl      NVIC_Init

    ; Enable USART2
    08007050: 01 21          movs    r1, #1                  ; ENABLE
    08007052: 20 46          mov     r0, r4                  ; USART2_BASE
    08007054: FB F7 A6 FD    bl      USART_Cmd               ; USART_Cmd(USART2, ENABLE)

    08007058: 0C B0          add     sp, #0x30               ; Free stack
    0800705A: 10 BD          pop     {r4, pc}                ; return


; =============================================================================
; END OF ANNOTATED ASSEMBLY
; =============================================================================
;
; Summary of key addresses (DRV_1.6.13):
;
;   0x08002720  calculateChecksum(data, length) -> uint16_t
;   0x080036AC  buildPacket(src, dst, len, cmd, arg, payload, buf) -> size
;   0x08007128  parseProtocolByte_USART1(byte)
;   0x08007468  parseProtocolByte_USART2(byte)
;   0x080077AC  parseProtocolByte_USART3(byte)
;   0x080071F4  enqueuePacket_USART1(src, dst, len, cmd, arg, payload)
;   0x08007534  enqueuePacket_USART2(...)
;   0x08007878  enqueuePacket_USART3(...)
;   0x08007610  uartTransmitHandler_USART1()
;   0x08006F80  uartTransmitHandler_USART2()
;   0x080072D0  uartTransmitHandler_USART3()
;   0x08005468  dispatchReceivedPacket(channel, buffer)
;   0x0800700C  usart2_init()
;
; SRAM addresses:
;   0x200003AC  UartParserState for USART1
;   0x20000394  UartParserState for USART3
;   0x20000544  UartParserState for USART2
;   0x200010F8  RX buffer for USART1
;   0x200018CC  RX buffer for USART2 (approx)
;   0x20000B00  RX buffer for USART3 (approx)
;   0x20000BFC  TX buffers base (4 slots x 256 bytes per channel)
;
; =============================================================================
