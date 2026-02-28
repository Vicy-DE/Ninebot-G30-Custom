/**
 * =============================================================================
 * Ninebot G30 Max - Reconstructed Protocol Code
 * =============================================================================
 *
 * Reverse-engineered from stock firmware binaries:
 *   - DRV_1.6.13_Compat.bin (ESC Motor Controller, STM32F103CBT6)
 *   - BLE_1.1.7.bin         (BLE Dashboard, STM32F103C8T6)
 *   - BMS_1.7.4.5.bin       (Battery Management, STM32F103C8T6)
 *
 * Target:  ARM Cortex-M3 (STM32F103), Thumb instruction set
 * Toolchain: arm-none-eabi-gcc
 * Endianness: Little-endian
 *
 * This file reconstructs the core Ninebot serial protocol handling from
 * disassembled machine code. Each function includes the original ARM Thumb
 * assembly as inline comments for cross-reference and verification.
 *
 * WARNING: This is a research/documentation file. Do not use for safety-
 * critical modifications without thorough testing. BMS modifications can
 * cause battery fires.
 *
 * =============================================================================
 * Protocol Overview:
 *
 *   Packet format:
 *     [0x5A] [0xA5] [LEN] [SRC] [DST] [CMD] [ARG] [PAYLOAD...] [CHK_LO] [CHK_HI]
 *
 *   Header:    0x5A 0xA5 (magic bytes)
 *   LEN:       Number of bytes from SRC through end of PAYLOAD
 *              (i.e. 2 + 1 + 1 + payload_size = payload_size + 4)
 *   SRC/DST:   Device addresses (0x20=ESC, 0x21=BLE, 0x22=BMS, 0x3E=App, 0x3F=PC)
 *   CMD:       Command byte
 *   ARG:       Argument/register byte
 *   PAYLOAD:   Variable-length data (0..242 bytes)
 *   Checksum:  ~(sum of bytes from LEN through end of PAYLOAD) & 0xFFFF
 *
 *   UART: 115200 baud, 8N1
 *   Max packet payload: 242 bytes (0xF2), total max packet: 251 bytes (0xFB)
 * =============================================================================
 */

#include <cstdint>
#include <cstring>

/* ============================================================================
 * STM32F103 Peripheral Register Definitions
 * ============================================================================ */

/** USART peripheral base addresses (STM32F103) */
#define USART1_BASE  0x40013800U
#define USART2_BASE  0x40004400U
#define USART3_BASE  0x40004800U

/** USART register offsets */
#define USART_SR_OFFSET   0x00U  /**< Status Register */
#define USART_DR_OFFSET   0x04U  /**< Data Register */
#define USART_BRR_OFFSET  0x08U  /**< Baud Rate Register */
#define USART_CR1_OFFSET  0x0CU  /**< Control Register 1 */
#define USART_CR2_OFFSET  0x10U  /**< Control Register 2 */
#define USART_CR3_OFFSET  0x14U  /**< Control Register 3 */

/** USART Status Register bits */
#define USART_SR_TXE   (1U << 7)   /**< Transmit data register empty */
#define USART_SR_TC    (1U << 6)   /**< Transmission complete */
#define USART_SR_RXNE  (1U << 5)   /**< Read data register not empty */

/** BRR value for 115200 baud at 72 MHz APB clock */
#define USART_BRR_115200  0x0271U  /**< 72000000 / 115200 = 625 = 0x271 */


/* ============================================================================
 * Protocol Constants
 * ============================================================================ */

static constexpr uint8_t  HEADER_BYTE_1       = 0x5A;  /**< First magic header byte */
static constexpr uint8_t  HEADER_BYTE_2       = 0xA5;  /**< Second magic header byte */
static constexpr uint8_t  MAX_PAYLOAD_LENGTH   = 0xF2;  /**< Maximum payload size (242 bytes) */
static constexpr uint8_t  MAX_PACKET_DATA_SIZE = 0xF3;  /**< Max data after header (len + 7 bytes) */
static constexpr uint8_t  HEADER_OVERHEAD      = 7;     /**< Header fields beyond the length byte */
static constexpr uint8_t  PACKET_BUFFER_SIZE   = 0xFA;  /**< 250-byte buffer per UART channel */
static constexpr uint8_t  NUM_TX_SLOTS         = 4;     /**< Circular TX buffer slots */

/** Device addresses on the Ninebot bus */
enum class DeviceAddress : uint8_t {
    ESC = 0x20,   /**< Electronic Speed Controller (motor controller) */
    BLE = 0x21,   /**< Bluetooth/Dashboard board */
    BMS = 0x22,   /**< Battery Management System */
    APP = 0x3E,   /**< Phone application (via BLE) */
    PC  = 0x3F    /**< PC/debugger interface */
};

/** UART channel identifiers (maps to ESC's 3 UARTs) */
enum class UartChannel : uint8_t {
    CHANNEL_BLE  = 0,  /**< USART2 -> BLE dashboard */
    CHANNEL_ESC  = 1,  /**< USART1 -> External / debug */
    CHANNEL_BMS  = 2   /**< USART3 -> BMS battery board */
};


/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * Protocol parser state machine context.
 *
 * One instance exists per UART channel. Tracks the byte-by-byte reception
 * of Ninebot protocol packets. Located in SRAM at addresses like 0x200003AC.
 *
 * Memory layout (reconstructed from field access patterns):
 *   Offset 0x00: currentTxSlot       (int8_t)  - Current TX buffer slot index
 *   Offset 0x01: txWriteSlot         (int8_t)  - Next TX write slot index
 *   Offset 0x02: pendingTxCount      (int8_t)  - Number of TX packets pending
 *   Offset 0x03: txBusy              (uint8_t) - TX in progress flag
 *   Offset 0x04: txDraining          (uint8_t) - TX drain-complete flag
 *   Offset 0x05: rxActive            (uint8_t) - RX processing active flag
 *   Offset 0x06: txEnabled           (uint8_t) - TX interrupt enabled flag
 *   Offset 0x07: gotFirstHeader      (uint8_t) - Received 0x5A flag
 *   Offset 0x08: receivingPacket     (uint8_t) - Currently receiving packet data
 *   Offset 0x09: rxByteIndex         (uint8_t) - Current byte position in RX buffer
 *   Offset 0x0A: expectedRxLength    (uint8_t) - Total expected bytes after header
 *   Offset 0x0C: rxRunningChecksum   (uint16_t)- Running checksum accumulator
 *   Offset 0x10: txByteOffset        (uint32_t)- Current TX byte offset within slot
 *   Offset 0x14: rxByteOffset        (uint32_t)- Supplementary RX byte offset
 */
struct UartParserState {
    int8_t   currentTxSlot;      /**< [+0x00] Active TX buffer slot (0..3) */
    int8_t   txWriteSlot;        /**< [+0x01] Write pointer for TX ring (0..3) */
    int8_t   pendingTxCount;     /**< [+0x02] Packets queued for TX */
    uint8_t  txBusy;             /**< [+0x03] TX transaction in progress */
    uint8_t  txDraining;         /**< [+0x04] Draining TX buffer */
    uint8_t  rxActive;           /**< [+0x05] RX byte processing active */
    uint8_t  txEnabled;          /**< [+0x06] TX interrupt has been enabled */
    uint8_t  gotFirstHeader;     /**< [+0x07] Seen 0x5A, waiting for 0xA5 */
    uint8_t  receivingPacket;    /**< [+0x08] Receiving packet body */
    uint8_t  rxByteIndex;        /**< [+0x09] Next write position in RX buffer */
    uint8_t  expectedRxLength;   /**< [+0x0A] Total expected packet bytes (after header) */
    uint8_t  _reserved_0B;      /**< [+0x0B] padding */
    uint16_t rxRunningChecksum;  /**< [+0x0C] Accumulating checksum of received bytes */
    uint8_t  _reserved_0E;      /**< [+0x0E] padding */
    uint8_t  _reserved_0F;      /**< [+0x0F] padding */
    uint32_t txByteOffset;       /**< [+0x10] Current byte within TX slot being sent */
    uint32_t rxByteOffset;       /**< [+0x14] RX supplementary byte tracking */
};

/**
 * TX buffer slot.
 *
 * Each UART channel has NUM_TX_SLOTS (4) of these, arranged contiguously
 * in SRAM. Each slot holds one complete outgoing packet. The first 4 bytes
 * store the packet length, followed by the raw packet data.
 */
struct TxBufferSlot {
    uint32_t packetLength;                  /**< [+0x00] Total bytes in this packet */
    uint8_t  packetData[PACKET_BUFFER_SIZE]; /**< [+0x04] Raw packet bytes */
};

/**
 * Per-channel TX buffer array.
 * Located contiguously in SRAM, accessed as: base + (slot_index * 256) + offset
 */
struct UartTxBuffers {
    TxBufferSlot slots[NUM_TX_SLOTS];  /**< 4 TX slots per UART channel */
};


/* ============================================================================
 * Global State (SRAM)
 *
 * These represent the parser state and buffers located in SRAM.
 * Addresses are from DRV_1.6.13 firmware analysis.
 * ============================================================================ */

/**
 * Parser states for the three UART channels.
 * Each protocol_parser_N function references its own UartParserState instance.
 *
 * DRV_1.6.13 SRAM addresses (from PC-relative literal loads):
 *   USART1 parser state: 0x200003AC (protocol_parser_1, uart_handler_USART1)
 *   USART2 parser state: 0x20000544 (protocol_parser_2, uart_handler_USART2)
 *   USART3 parser state: 0x20000394 (protocol_parser_3, uart_handler_USART3)
 */
static UartParserState  g_uartParserState[3];

/** RX packet buffer per UART channel (receives raw bytes after 0x5A 0xA5 header) */
static uint8_t  g_rxPacketBuffer[3][256];

/** TX buffer arrays per UART channel */
static UartTxBuffers  g_txBuffers[3];


/* ============================================================================
 * Function Declarations
 * ============================================================================ */

static uint16_t calculateChecksum(const uint8_t* data, uint8_t length);
static uint8_t  buildPacket(uint8_t srcAddr, uint8_t dstAddr, uint8_t payloadLength,
                            uint8_t command, uint8_t argument,
                            const uint8_t* payload, uint8_t* outputBuffer);
static void     parseProtocolByte(uint8_t incomingByte, UartChannel channel);
static void     dispatchReceivedPacket(UartChannel channel, const uint8_t* buffer);
static void     uartTransmitHandler(UartChannel channel);
static void     enqueuePacket(uint8_t srcAddr, uint8_t dstAddr, uint8_t payloadLength,
                              uint8_t command, uint8_t argument,
                              const uint8_t* payload, UartChannel channel);


/* ============================================================================
 * FUNCTION: calculateChecksum
 * ============================================================================
 *
 * Source: DRV_1.6.13 @ 0x08002720 - 0x08002738
 *
 * Computes the Ninebot protocol checksum over a byte array.
 * The checksum is the bitwise complement of the 16-bit sum of all bytes.
 *
 * Parameters:
 *   data   - Pointer to start of data (typically &packet[2], the length field)
 *   length - Number of bytes to sum
 *
 * Returns:
 *   16-bit checksum value: ~(sum of bytes) & 0xFFFF
 *
 * ---- Original ARM Thumb Assembly (DRV_1.6.13 @ 0x08002720) ----
 *
 *   0x08002720: push   {r4, lr}          ; Save r4 and return address
 *   0x08002722: movs   r3, #0            ; uint16_t sum = 0
 *   0x08002724: movs   r2, #0            ; uint8_t  i = 0
 *   0x08002726: b      #0x8002730        ; Jump to loop condition
 *
 *   ; --- Loop body ---
 *   0x08002728: ldrb   r4, [r0, r2]      ; r4 = data[i]
 *   0x0800272A: add    r3, r4            ; sum += data[i]
 *   0x0800272C: uxth   r3, r3            ; sum &= 0xFFFF (keep 16-bit)
 *   0x0800272E: adds   r2, r2, #1        ; i++
 *
 *   ; --- Loop condition ---
 *   0x08002730: cmp    r2, r1            ; if (i < length)
 *   0x08002732: blo    #0x8002728        ;   goto loop body
 *
 *   ; --- Return ~sum ---
 *   0x08002734: mvns   r0, r3            ; r0 = ~sum
 *   0x08002736: uxth   r0, r0            ; r0 &= 0xFFFF
 *   0x08002738: pop    {r4, pc}          ; Return r0
 *
 * ----------------------------------------------------------------
 */
static uint16_t calculateChecksum(const uint8_t* data, uint8_t length)
{
    uint16_t sum = 0;

    for (uint8_t i = 0; i < length; i++) {
        sum += data[i];          // Accumulate each byte
        sum &= 0xFFFF;          // Keep as 16-bit (uxth in ARM)
    }

    return static_cast<uint16_t>(~sum);  // Bitwise NOT, masked to 16 bits
}


/* ============================================================================
 * FUNCTION: buildPacket
 * ============================================================================
 *
 * Source: DRV_1.6.13 @ 0x080036AC - 0x080036F6
 *
 * Constructs a complete Ninebot protocol packet in the output buffer.
 * Fills the header, addresses, command fields, copies payload data,
 * and appends the 16-bit checksum.
 *
 * Parameters:
 *   srcAddr       - Source device address (e.g., 0x20 for ESC)
 *   dstAddr       - Destination device address
 *   payloadLength - Number of payload bytes (also used as the LEN field)
 *   command       - Command byte
 *   argument      - Argument/register byte
 *   payload       - Pointer to payload data array
 *   outputBuffer  - Destination buffer (must be >= payloadLength + 9 bytes)
 *
 * Returns:
 *   Total number of bytes written to outputBuffer
 *
 * Calling convention (ARM EABI):
 *   r0 = srcAddr, r1 = dstAddr, r2 = payloadLength, r3 = command
 *   [sp+0x14] = argument (ip/r12), [sp+0x18] = payload (r7), [sp+0x1c] = outputBuffer (r4)
 *
 * ---- Original ARM Thumb Assembly (DRV_1.6.13 @ 0x080036AC) ----
 *
 *   0x080036AC: push   {r4, r5, r6, r7, lr}     ; Save registers
 *   0x080036AE: ldrd   ip, r7, [sp, #0x14]       ; ip = argument, r7 = payload ptr
 *   0x080036B2: ldr    r4, [sp, #0x1c]           ; r4 = outputBuffer
 *   0x080036B4: movs   r6, #0                    ; payloadIndex = 0
 *
 *   ; --- Write header bytes ---
 *   0x080036B6: movs   r5, #0x5a                 ; HEADER_BYTE_1
 *   0x080036B8: strb   r5, [r4]                  ; outputBuffer[0] = 0x5A
 *   0x080036BA: movs   r5, #0xa5                 ; HEADER_BYTE_2
 *   0x080036BC: strb   r5, [r4, #1]              ; outputBuffer[1] = 0xA5
 *
 *   ; --- Write packet fields ---
 *   0x080036BE: strb   r2, [r4, #2]              ; outputBuffer[2] = payloadLength (LEN)
 *   0x080036C0: strb   r0, [r4, #3]              ; outputBuffer[3] = srcAddr
 *   0x080036C2: strb   r1, [r4, #4]              ; outputBuffer[4] = dstAddr
 *   0x080036C4: strb   r3, [r4, #5]              ; outputBuffer[5] = command
 *   0x080036C6: movs   r5, #7                    ; bufferIndex = 7 (start of payload area)
 *   0x080036C8: strb.w ip, [r4, #6]              ; outputBuffer[6] = argument
 *   0x080036CC: b      #0x80036dc                ; Jump to loop condition
 *
 *   ; --- Payload copy loop ---
 *   0x080036CE: mov    r0, r5                    ; r0 = bufferIndex
 *   0x080036D0: ldrb   r1, [r7, r6]              ; r1 = payload[payloadIndex]
 *   0x080036D2: adds   r5, r5, #1                ; bufferIndex++
 *   0x080036D4: uxtb   r5, r5                    ; Mask to byte
 *   0x080036D6: strb   r1, [r4, r0]              ; outputBuffer[bufferIndex] = payload[payloadIndex]
 *   0x080036D8: adds   r6, r6, #1                ; payloadIndex++
 *   0x080036DA: uxtb   r6, r6                    ; Mask to byte
 *   0x080036DC: cmp    r6, r2                    ; while (payloadIndex < payloadLength)
 *   0x080036DE: blo    #0x80036ce                ;   continue loop
 *
 *   ; --- Calculate and append checksum ---
 *   0x080036E0: subs   r1, r5, #2                ; checksumLength = bufferIndex - 2
 *   0x080036E2: adds   r0, r4, #2                ; checksumStart = &outputBuffer[2]
 *   0x080036E4: bl     #0x8002720                ; checksum = calculateChecksum(checksumStart, checksumLength)
 *   0x080036E8: adds   r1, r5, #1                ; checksumHiPos = bufferIndex + 1
 *   0x080036EA: uxtb   r1, r1                    ; Mask to byte
 *   0x080036EC: strb   r0, [r4, r5]              ; outputBuffer[bufferIndex] = checksum & 0xFF
 *   0x080036EE: lsrs   r2, r0, #8                ; r2 = checksum >> 8
 *   0x080036F0: adds   r0, r1, #1                ; (unused, return value setup)
 *   0x080036F2: uxtb   r0, r0                    ; Mask to byte
 *   0x080036F4: strb   r2, [r4, r1]              ; outputBuffer[bufferIndex+1] = checksum >> 8
 *   0x080036F6: pop    {r4, r5, r6, r7, pc}      ; Return
 *
 * ----------------------------------------------------------------
 */
static uint8_t buildPacket(uint8_t srcAddr, uint8_t dstAddr, uint8_t payloadLength,
                           uint8_t command, uint8_t argument,
                           const uint8_t* payload, uint8_t* outputBuffer)
{
    uint8_t bufferIndex = 0;

    /* Write the two-byte magic header */
    outputBuffer[bufferIndex++] = HEADER_BYTE_1;   // 0x5A
    outputBuffer[bufferIndex++] = HEADER_BYTE_2;    // 0xA5

    /* Write the packet metadata fields */
    outputBuffer[bufferIndex++] = payloadLength;    // LEN - payload byte count
    outputBuffer[bufferIndex++] = srcAddr;          // Source address (e.g., 0x20 = ESC)
    outputBuffer[bufferIndex++] = dstAddr;          // Destination address
    outputBuffer[bufferIndex++] = command;          // Command byte
    outputBuffer[bufferIndex++] = argument;         // Argument/register byte

    /* Copy payload data into the packet buffer */
    for (uint8_t i = 0; i < payloadLength; i++) {
        outputBuffer[bufferIndex++] = payload[i];
    }

    /*
     * Calculate checksum over bytes [2..bufferIndex-1]
     * (covers: LEN, SRC, DST, CMD, ARG, PAYLOAD)
     *
     * checksumLength = bufferIndex - 2 = 5 + payloadLength
     *   (5 fixed fields + payload bytes, skipping the 2-byte header)
     */
    uint8_t checksumStartOffset = 2;                            // Skip 0x5A 0xA5
    uint8_t checksumLength      = bufferIndex - checksumStartOffset;
    uint16_t checksum = calculateChecksum(&outputBuffer[checksumStartOffset],
                                          checksumLength);

    /* Append the 16-bit checksum in little-endian order */
    outputBuffer[bufferIndex++] = static_cast<uint8_t>(checksum & 0xFF);        // CHK_LO
    outputBuffer[bufferIndex++] = static_cast<uint8_t>((checksum >> 8) & 0xFF); // CHK_HI

    return bufferIndex;  // Total packet size
}


/* ============================================================================
 * FUNCTION: parseProtocolByte
 * ============================================================================
 *
 * Source: DRV_1.6.13 @ 0x08007128 - 0x080071B4 (USART1 variant)
 *         DRV_1.6.13 @ 0x08007468 - 0x080074F4 (USART2 variant)
 *         DRV_1.6.13 @ 0x080077AC - 0x0800783A (USART3 variant)
 *
 * State machine that processes one incoming byte at a time from the UART.
 * Three identical copies exist in the firmware (one per UART channel),
 * differing only in the state struct pointer and channel ID passed to
 * the packet dispatch function.
 *
 * State machine flow:
 *   IDLE:
 *     -> Receive 0x5A: set gotFirstHeader, remain in IDLE
 *     -> Receive 0xA5 (if gotFirstHeader): transition to RECEIVING
 *     -> Any other byte: reset state
 *
 *   RECEIVING:
 *     -> Byte index 0: extract expectedRxLength = byte + 7 (HEADER_OVERHEAD)
 *        If expectedRxLength > MAX_PACKET_DATA_SIZE (0xF3): reset (overflow)
 *     -> Accumulate byte into rxRunningChecksum
 *     -> When rxByteIndex == expectedRxLength:
 *        * Extract received checksum from last 2 buffer bytes
 *        * Compute expected: ~(runningSum - chkLo) & 0xFFFF
 *        * If match: dispatch packet
 *        * Reset state regardless
 *
 * Parameters:
 *   incomingByte - The byte just received from the UART data register
 *   channel      - Which UART channel (determines state struct and dispatch ID)
 *
 * ---- Original ARM Thumb Assembly (DRV_1.6.13 @ 0x08007128, USART1) ----
 *
 *   ; ===== Function entry =====
 *   0x08007128: push   {r4, r5, r6, lr}          ; Save registers
 *   0x0800712A: ldr    r4, [pc, #0x8c]           ; r4 = &parserState (0x200003AC)
 *   0x0800712C: movs   r5, #0                    ; r5 = 0 (used for clearing fields)
 *   0x0800712E: ldrb   r1, [r4, #8]              ; r1 = parserState.receivingPacket
 *   0x08007130: cmp    r1, #0                    ; if (!receivingPacket)
 *   0x08007132: beq    #0x8007190                 ;   -> go to header detection
 *
 *   ; ===== RECEIVING STATE: Store byte and track progress =====
 *   0x08007134: ldr    r3, [pc, #0x84]           ; r3 = &rxBuffer (0x200010F8)
 *   0x08007136: ldrb   r1, [r4, #9]              ; r1 = rxByteIndex
 *   0x08007138: strb   r0, [r3, r1]              ; rxBuffer[rxByteIndex] = incomingByte
 *
 *   ; --- On first data byte (index 0): compute expected total length ---
 *   0x0800713A: cbnz   r1, #0x800714e            ; if (rxByteIndex != 0) skip
 *   0x0800713C: adds   r2, r0, #7                ; expectedLen = incomingByte + 7
 *   0x0800713E: uxtb   r2, r2                    ; Mask to 8-bit
 *   0x08007140: strb   r2, [r4, #0xa]            ; parserState.expectedRxLength = expectedLen
 *   0x08007142: cmp    r2, #0xf3                 ; if (expectedLen > MAX_PACKET_DATA_SIZE)
 *   0x08007144: bls    #0x800714e                ;   (unsigned compare: 0xF4..0xFF wraps >243)
 *   0x08007146: strb   r5, [r4, #8]              ;   receivingPacket = 0  (RESET - overflow)
 *   0x08007148: strb   r5, [r4, #7]              ;   gotFirstHeader = 0
 *   0x0800714A: strh   r5, [r4, #0xc]            ;   rxRunningChecksum = 0
 *   0x0800714C: pop    {r4, r5, r6, pc}          ;   return (packet too long)
 *
 *   ; --- Advance byte index ---
 *   0x0800714E: adds   r1, r1, #1                ; rxByteIndex++
 *   0x08007150: uxtb   r1, r1                    ; Mask to 8-bit
 *   0x08007152: strb   r1, [r4, #9]              ; Store updated index
 *
 *   ; --- Load running checksum ---
 *   0x08007154: ldr    r2, [pc, #0x60]           ; r2 = &parserState (same struct)
 *   0x08007156: ldrb   r6, [r4, #0xa]            ; r6 = expectedRxLength
 *   0x08007158: ldrh   r2, [r2, #0xc]            ; r2 = rxRunningChecksum
 *
 *   ; --- Check if packet is complete ---
 *   0x0800715A: cmp    r1, r6                    ; if (rxByteIndex != expectedRxLength)
 *   0x0800715C: bne    #0x800718a                ;   -> accumulate checksum
 *
 *   ; ===== PACKET COMPLETE: Verify checksum =====
 *   0x0800715E: adds   r0, r3, r1                ; r0 = &rxBuffer[rxByteIndex]
 *   0x08007160: ldrb   r1, [r0, #-0x2]!          ; r1 = rxBuffer[len-2] (chkLo), r0 -= 2
 *   0x08007164: subs   r2, r2, r1                ; runningSum -= chkLo
 *   0x08007166: mvns   r2, r2                    ; calculated = ~runningSum
 *   0x08007168: uxth   r2, r2                    ; calculated &= 0xFFFF
 *   0x0800716A: strh   r2, [r4, #0xc]            ; (store for debug)
 *   0x0800716C: ldrb   r0, [r0, #1]              ; r0 = rxBuffer[len-1] (chkHi)
 *   0x0800716E: add.w  r0, r1, r0, lsl #8        ; received = chkLo | (chkHi << 8)
 *   0x08007172: uxth   r0, r0                    ; received &= 0xFFFF
 *   0x08007174: cmp    r2, r0                    ; if (calculated == received)
 *   0x08007176: bne    #0x8007180                ;   (mismatch -> skip dispatch)
 *
 *   ; --- Checksum OK: dispatch the packet ---
 *   0x08007178: ldr    r1, [pc, #0x40]           ; r1 = &rxBuffer
 *   0x0800717A: movs   r0, #1                    ; r0 = channel_id (1 = USART1)
 *   0x0800717C: bl     #0x8005468                ; dispatchReceivedPacket(channel, buffer)
 *
 *   ; --- Reset parser state ---
 *   0x08007180: strb   r5, [r4, #8]              ; receivingPacket = 0
 *   0x08007182: strb   r5, [r4, #7]              ; gotFirstHeader = 0
 *   0x08007184: strb   r5, [r4, #9]              ; rxByteIndex = 0
 *   0x08007186: strh   r5, [r4, #0xc]            ; rxRunningChecksum = 0
 *   0x08007188: pop    {r4, r5, r6, pc}          ; return
 *
 *   ; ===== ACCUMULATE: Add byte to running checksum =====
 *   0x0800718A: add    r0, r2                    ; runningSum += incomingByte
 *   0x0800718C: strh   r0, [r4, #0xc]            ; rxRunningChecksum = updated sum
 *   0x0800718E: pop    {r4, r5, r6, pc}          ; return
 *
 *   ; ===== IDLE STATE: Header detection =====
 *   0x08007190: ldr    r1, [pc, #0x24]           ; r1 = &parserState
 *   0x08007192: movs   r2, #1                    ; r2 = 1 (flag value)
 *   0x08007194: cmp    r0, #0x5a                 ; if (incomingByte == 0x5A)
 *   0x08007196: ldrb   r1, [r1, #7]              ; r1 = gotFirstHeader
 *   0x08007198: bne    #0x800719c                ;   (not 0x5A -> check 0xA5)
 *   0x0800719A: cbz    r1, #0x80071a2            ;   if (!gotFirstHeader) -> set it
 *
 *   ; --- Check second header byte ---
 *   0x0800719C: cmp    r0, #0xa5                 ; if (incomingByte == 0xA5)
 *   0x0800719E: beq    #0x80071a6                ;   -> transition to receiving
 *   0x080071A0: b      #0x80071ae                ;   else -> reset
 *
 *   ; --- Set first header flag ---
 *   0x080071A2: strb   r2, [r4, #7]              ; gotFirstHeader = 1
 *   0x080071A4: pop    {r4, r5, r6, pc}          ; return (wait for 0xA5)
 *
 *   ; --- Transition to RECEIVING ---
 *   0x080071A6: cbz    r1, #0x80071ae            ; if (!gotFirstHeader) -> reset
 *   0x080071A8: strb   r2, [r4, #8]              ; receivingPacket = 1
 *   0x080071AA: strh   r5, [r4, #0xc]            ; rxRunningChecksum = 0
 *   0x080071AC: pop    {r4, r5, r6, pc}          ; return (ready for data bytes)
 *
 *   ; --- RESET: clear all state ---
 *   0x080071AE: strb   r5, [r4, #8]              ; receivingPacket = 0
 *   0x080071B0: strb   r5, [r4, #7]              ; gotFirstHeader = 0
 *   0x080071B2: strh   r5, [r4, #0xc]            ; rxRunningChecksum = 0
 *   0x080071B4: pop    {r4, r5, r6, pc}          ; return
 *
 * ----------------------------------------------------------------
 */
static void parseProtocolByte(uint8_t incomingByte, UartChannel channel)
{
    uint8_t channelIdx = static_cast<uint8_t>(channel);
    UartParserState& state  = g_uartParserState[channelIdx];
    uint8_t*         rxBuf  = g_rxPacketBuffer[channelIdx];

    /* ---- RECEIVING STATE: Currently assembling a packet ---- */
    if (state.receivingPacket) {

        /* Store the incoming byte in the RX buffer */
        rxBuf[state.rxByteIndex] = incomingByte;

        /* First data byte (index 0) is the LEN field -> compute expected total */
        if (state.rxByteIndex == 0) {
            state.expectedRxLength = incomingByte + HEADER_OVERHEAD;

            /* Sanity check: reject packets that would overflow the buffer */
            if (state.expectedRxLength > MAX_PACKET_DATA_SIZE) {
                state.receivingPacket   = 0;
                state.gotFirstHeader    = 0;
                state.rxRunningChecksum = 0;
                return;  // Packet too large, discard
            }
        }

        /* Advance the byte index */
        state.rxByteIndex++;

        /* Check if we've received the complete packet */
        if (state.rxByteIndex == state.expectedRxLength) {

            /*
             * Verify the checksum.
             *
             * The running checksum has accumulated all bytes from index 0
             * through index (expectedRxLength - 2). The byte at index
             * (expectedRxLength - 1) was stored but not yet accumulated.
             *
             * The last two bytes are the received checksum: [CHK_LO, CHK_HI]
             *   chkLo = rxBuf[expectedRxLength - 2]
             *   chkHi = rxBuf[expectedRxLength - 1]
             *
             * Verification formula:
             *   calculated = ~(runningSum - chkLo) & 0xFFFF
             *   received   = chkLo | (chkHi << 8)
             *   Match means: ~sum_of_data_bytes == received checksum
             */
            uint8_t  chkLo = rxBuf[state.expectedRxLength - 2];
            uint8_t  chkHi = rxBuf[state.expectedRxLength - 1];

            uint16_t calculatedChecksum = static_cast<uint16_t>(
                ~(state.rxRunningChecksum - chkLo) & 0xFFFF
            );
            uint16_t receivedChecksum = static_cast<uint16_t>(
                chkLo | (chkHi << 8)
            );

            /* If checksum matches, pass the complete packet to the dispatcher */
            if (calculatedChecksum == receivedChecksum) {
                dispatchReceivedPacket(channel, rxBuf);
            }

            /* Reset parser state regardless of checksum result */
            state.receivingPacket   = 0;
            state.gotFirstHeader    = 0;
            state.rxByteIndex       = 0;
            state.rxRunningChecksum = 0;

        } else {

            /* Packet not yet complete: accumulate byte into running checksum */
            state.rxRunningChecksum += incomingByte;
        }

        return;
    }

    /* ---- IDLE STATE: Looking for the 0x5A 0xA5 header sequence ---- */

    if (incomingByte == HEADER_BYTE_1) {

        /* Received 0x5A: mark first header byte seen (if not already in sequence) */
        if (!state.gotFirstHeader) {
            state.gotFirstHeader = 1;
            return;
        }
        /* If gotFirstHeader was already set (0x5A 0x5A), remain waiting */

    } else if (incomingByte == HEADER_BYTE_2) {

        /* Received 0xA5: if preceded by 0x5A, transition to RECEIVING */
        if (state.gotFirstHeader) {
            state.receivingPacket   = 1;
            state.rxRunningChecksum = 0;
            return;
        }
        /* 0xA5 without preceding 0x5A: fall through to reset */
    }

    /* Any unexpected byte: reset all state */
    state.receivingPacket   = 0;
    state.gotFirstHeader    = 0;
    state.rxRunningChecksum = 0;
}


/* ============================================================================
 * FUNCTION: enqueuePacket
 * ============================================================================
 *
 * Source: DRV_1.6.13 @ 0x080071F4 - 0x0800725A (USART1 variant)
 *         DRV_1.6.13 @ 0x08007534 - 0x0800759A (USART2 variant)
 *         DRV_1.6.13 @ 0x08007878 - 0x080078DE (USART3 variant)
 *
 * Queues a packet for transmission on the specified UART channel.
 * Uses a circular buffer of 4 TX slots. Validates that the payload
 * doesn't exceed maximum size and that there's room in the TX queue.
 *
 * If the queue is full or payload is too large, the packet is silently dropped.
 *
 * ---- Original ARM Thumb Assembly (DRV_1.6.13 @ 0x080071F4, USART1) ----
 *
 *   0x080071F4: push.w {r1,r2,r3,r4,r5,r6,r7,r8,sb,lr}  ; Save registers
 *   0x080071F8: ldrd   r7, ip, [sp, #0x28]       ; r7 = payload, ip = argument
 *   0x080071FC: ldr    r4, [pc, #0x5c]           ; r4 = &parserState
 *   0x080071FE: ldrb   r5, [r4, #3]              ; r5 = txBusy
 *   0x08007200: cmp    r5, #0                    ; if (txBusy)
 *   0x08007202: bne    #0x800721e                ;   return (TX in progress)
 *   0x08007204: ldrb   r5, [r4, #5]              ; r5 = rxActive
 *   0x08007206: cmp    r5, #0                    ; if (rxActive)
 *   0x08007208: bne    #0x800721e                ;   return (RX in progress)
 *   0x0800720A: movs   r5, #1
 *   0x0800720C: strb   r5, [r4, #3]              ; txBusy = 1
 *   0x0800720E: movs   r5, #0
 *   0x08007210: cmp    r2, #0xf2                 ; if (payloadLength > MAX_PAYLOAD)
 *   0x08007212: bhi    #0x800721c                ;   -> abort
 *   0x08007214: ldrsb  r6, [r4, #2]              ; r6 = pendingTxCount
 *   0x08007218: cmp    r6, #0                    ; if (pendingTxCount <= 0)
 *   0x0800721A: bgt    #0x8007222                ;   -> abort (queue full at max)
 *   0x0800721C: strb   r5, [r4, #3]              ; txBusy = 0 (release lock)
 *   0x0800721E: pop.w  {r1..sb,pc}               ; return
 *
 *   ; --- Build packet into TX slot ---
 *   0x08007222: ldr    r6, [pc, #0x3c]           ; r6 = &txBuffers base
 *   0x08007224: ldrsb  r8, [r4, #1]              ; r8 = txWriteSlot (signed)
 *   0x08007228: add.w  r8, r6, r8, lsl #8        ; r8 = &txBuffers[txWriteSlot * 256]
 *   0x0800722C: add.w  r8, r8, #4                ; r8 = &txBuffers[slot].packetData
 *   0x08007230: strd   ip, r8, [sp, #4]          ; Push argument, outputBuffer to stack
 *   0x08007234: str    r7, [sp]                   ; Push payload to stack
 *   0x08007236: bl     #0x80036ac                ; buildPacket(src,dst,len,cmd,arg,payload,buf)
 *
 *   ; --- Update slot metadata ---
 *   0x0800723A: ldrsb  r1, [r4, #1]              ; r1 = txWriteSlot
 *   0x0800723E: add.w  r1, r6, r1, lsl #8        ; r1 = &txBuffers[slot]
 *   0x08007242: str    r0, [r1]                   ; txBuffers[slot].packetLength = return value
 *
 *   ; --- Advance write slot (circular: 0..3) ---
 *   0x08007244: ldrb   r0, [r4, #1]              ; r0 = txWriteSlot
 *   0x08007246: adds   r0, #1                    ; txWriteSlot++
 *   0x08007248: strb   r0, [r4, #1]
 *   0x0800724A: ldrb   r0, [r4, #1]
 *   0x0800724C: cmp    r0, #4                    ; if (txWriteSlot >= 4)
 *   0x0800724E: bne    #0x8007252
 *   0x08007250: strb   r5, [r4, #1]              ;   txWriteSlot = 0 (wrap around)
 *
 *   ; --- Decrement available count and release lock ---
 *   0x08007252: ldrb   r0, [r4, #2]              ; pendingTxCount--
 *   0x08007254: subs   r0, #1
 *   0x08007256: strb   r0, [r4, #2]
 *   0x08007258: strb   r5, [r4, #3]              ; txBusy = 0
 *   0x0800725A: b      #0x800721e                ; return
 *
 * ----------------------------------------------------------------
 */
static void enqueuePacket(uint8_t srcAddr, uint8_t dstAddr, uint8_t payloadLength,
                          uint8_t command, uint8_t argument,
                          const uint8_t* payload, UartChannel channel)
{
    uint8_t channelIdx = static_cast<uint8_t>(channel);
    UartParserState& state = g_uartParserState[channelIdx];

    /* Acquire TX lock (mutual exclusion between TX and RX operations) */
    if (state.txBusy || state.rxActive) {
        return;  // Someone else is using the UART subsystem
    }
    state.txBusy = 1;

    /* Validate payload size */
    if (payloadLength > MAX_PAYLOAD_LENGTH) {
        state.txBusy = 0;
        return;  // Payload too large
    }

    /* Check if there's room in the TX queue */
    if (state.pendingTxCount <= 0) {
        state.txBusy = 0;
        return;  // Queue is full
    }

    /* Build the packet directly into the next available TX slot */
    TxBufferSlot& slot = g_txBuffers[channelIdx].slots[state.txWriteSlot];
    uint8_t packetSize = buildPacket(srcAddr, dstAddr, payloadLength,
                                     command, argument, payload,
                                     slot.packetData);
    slot.packetLength = packetSize;

    /* Advance the write slot index (circular: 0, 1, 2, 3, 0, 1, ...) */
    state.txWriteSlot++;
    if (state.txWriteSlot >= NUM_TX_SLOTS) {
        state.txWriteSlot = 0;
    }

    /* Decrement available slot count */
    state.pendingTxCount--;

    /* Release TX lock */
    state.txBusy = 0;
}


/* ============================================================================
 * FUNCTION: uartTransmitHandler
 * ============================================================================
 *
 * Source: DRV_1.6.13 @ 0x08007610 - 0x08007696 (USART1 variant)
 *
 * Called from the UART TX interrupt (or polled from main loop).
 * Sends one byte at a time from the current TX slot until the entire
 * packet has been transmitted, then advances to the next queued packet.
 *
 * Uses double-buffered approach: reads from the TX buffer slot while
 * the USART peripheral shifts out previously written bytes.
 *
 * ---- Original ARM Thumb Assembly (DRV_1.6.13 @ 0x08007610, USART1) ----
 *
 *   0x08007610: push   {r4, r5, lr}              ; Save registers
 *   0x08007612: ldr    r2, [pc, #0x84]           ; r2 = &parserState
 *   0x08007614: ldrb   r0, [r2, #3]              ; r0 = txBusy
 *   0x08007616: cmp    r0, #0                    ; if (txBusy) return
 *   0x08007618: bne    #0x8007632
 *   0x0800761A: ldrb   r0, [r2, #5]              ; r0 = rxActive
 *   0x0800761C: cmp    r0, #0                    ; if (rxActive) return
 *   0x0800761E: bne    #0x8007632
 *
 *   ; --- Acquire TX lock ---
 *   0x08007620: movs   r1, #1
 *   0x08007622: strb   r1, [r2, #5]              ; rxActive = 1 (TX uses rxActive as guard)
 *   0x08007624: ldr    r3, [pc, #0x74]           ; r3 = &USART_SR (status register)
 *   0x08007626: ldrh   r0, [r3]                  ; r0 = USART_SR
 *   0x08007628: lsls   r4, r0, #0x18             ; Test TXE bit (bit 7)
 *   0x0800762A: mov.w  r0, #0
 *   0x0800762E: bmi    #0x8007634                ; if (TXE set) -> send byte
 *   0x08007630: strb   r0, [r2, #5]              ;   else: rxActive = 0, return
 *   0x08007632: pop    {r4, r5, pc}              ; return
 *
 *   ; --- Check if current slot is exhausted ---
 *   0x08007634: ldrsb  r4, [r2, #1]              ; r4 = txWriteSlot (signed)
 *   0x08007638: ldrsb  r5, [r2]                  ; r5 = currentTxSlot (signed)
 *   0x0800763C: cmp    r4, r5                    ; if (txWriteSlot == currentTxSlot)
 *   0x0800763E: bne    #0x8007650                ;   -> all sent, handle completion
 *
 *   ; --- All packets sent - handle TC (Transmission Complete) ---
 *   0x08007640: ldrh   r1, [r3]                  ; r1 = USART_SR
 *   0x08007642: lsls   r1, r1, #0x19             ; Test TC bit (bit 6)
 *   0x08007644: bpl    #0x800764c                ; if (!TC) -> skip
 *   0x08007646: ldrb   r1, [r2, #6]              ; r1 = txEnabled
 *   0x08007648: cbz    r1, #0x800764c            ; if (!txEnabled) skip
 *   0x0800764A: strb   r0, [r2, #6]              ; txEnabled = 0 (disable TX IRQ)
 *   0x0800764C: strb   r0, [r2, #5]              ; rxActive = 0
 *   0x0800764E: pop    {r4, r5, pc}              ; return
 *
 *   ; --- Send next byte from current TX slot ---
 *   0x08007650: ldrb   r3, [r2, #6]              ; if (!txEnabled)
 *   0x08007652: cbnz   r3, #0x8007656
 *   0x08007654: strb   r1, [r2, #6]              ;   txEnabled = 1
 *
 *   0x08007656: ldr    r3, [pc, #0x48]           ; r3 = &txBuffers base
 *   0x08007658: ldrsb  r1, [r2]                  ; r1 = currentTxSlot
 *   0x0800765C: ldr    r5, [pc, #0x3c]           ; r5 = &USART_DR (data register)
 *   0x0800765E: add.w  r4, r3, r1, lsl #8        ; r4 = &txBuffers[slot * 256]
 *   0x08007662: ldr    r1, [r2, #0x14]           ; r1 = txByteOffset
 *   0x08007664: adds   r5, r5, #4                ; r5 = &USART_DR (base + 4)
 *   0x08007666: add    r4, r1                    ; r4 += txByteOffset
 *   0x08007668: ldrb   r4, [r4, #4]              ; r4 = txBuffers[slot].packetData[offset]
 *   0x0800766A: strh   r4, [r5]                  ; USART_DR = byte (triggers TX)
 *
 *   ; --- Advance byte offset ---
 *   0x0800766C: adds   r1, r1, #1                ; txByteOffset++
 *   0x0800766E: str    r1, [r2, #0x14]           ; Store updated offset
 *
 *   ; --- Check if slot is fully transmitted ---
 *   0x08007670: ldrsb  r4, [r2]                  ; r4 = currentTxSlot
 *   0x08007674: add.w  r3, r3, r4, lsl #8        ; r3 = &txBuffers[slot]
 *   0x08007678: ldr    r3, [r3]                  ; r3 = txBuffers[slot].packetLength
 *   0x0800767A: cmp    r3, r1                    ; if (packetLength > txByteOffset)
 *   0x0800767C: bgt    #0x8007694                ;   -> more bytes to send
 *
 *   ; --- Slot complete: advance to next slot ---
 *   0x0800767E: str    r0, [r2, #0x14]           ; txByteOffset = 0
 *   0x08007680: ldrb   r1, [r2]                  ; currentTxSlot++
 *   0x08007682: adds   r1, r1, #1
 *   0x08007684: strb   r1, [r2]
 *   0x08007686: ldrb   r1, [r2]
 *   0x08007688: cmp    r1, #4                    ; if (currentTxSlot >= 4)
 *   0x0800768A: bne    #0x800768e
 *   0x0800768C: strb   r0, [r2]                  ;   currentTxSlot = 0 (wrap)
 *   0x0800768E: ldrb   r1, [r2, #2]              ; pendingTxCount++
 *   0x08007690: adds   r1, r1, #1                ;   (slot now available for reuse)
 *   0x08007692: strb   r1, [r2, #2]
 *
 *   ; --- Release lock ---
 *   0x08007694: strb   r0, [r2, #5]              ; rxActive = 0
 *   0x08007696: pop    {r4, r5, pc}              ; return
 *
 * ----------------------------------------------------------------
 */
static void uartTransmitHandler(UartChannel channel)
{
    uint8_t channelIdx = static_cast<uint8_t>(channel);
    UartParserState& state = g_uartParserState[channelIdx];

    /* Don't interfere with ongoing TX or RX operations */
    if (state.txBusy || state.rxActive) {
        return;
    }

    /* Acquire the lock (uses rxActive flag as mutex) */
    state.rxActive = 1;

    /*
     * Check if the USART Transmit Data Register is Empty (TXE).
     * In real firmware, this reads the USART_SR register:
     *   volatile uint16_t* usartSR = (volatile uint16_t*)USART1_BASE;
     *   if (!((*usartSR) & USART_SR_TXE)) { state.rxActive = 0; return; }
     */
    // [Hardware register check omitted - platform specific]

    /* Check if there are packets queued for transmission */
    if (state.txWriteSlot == state.currentTxSlot) {

        /*
         * All packets have been sent. Check Transmission Complete (TC) flag
         * and disable TX interrupt if everything is finished.
         *
         * In firmware: reads USART_SR TC bit, clears txEnabled flag.
         */
        if (state.txEnabled) {
            state.txEnabled = 0;  // Disable TX interrupt
        }

        state.rxActive = 0;  // Release lock
        return;
    }

    /* Enable TX interrupt if not already enabled */
    if (!state.txEnabled) {
        state.txEnabled = 1;
    }

    /* Get the current TX slot and send the next byte */
    TxBufferSlot& slot = g_txBuffers[channelIdx].slots[state.currentTxSlot];
    uint8_t byteToSend = slot.packetData[state.txByteOffset];

    /*
     * Write the byte to the USART Data Register to trigger transmission.
     * In real firmware:
     *   volatile uint16_t* usartDR = (volatile uint16_t*)(USART1_BASE + USART_DR_OFFSET);
     *   *usartDR = byteToSend;
     */
    (void)byteToSend;  // Platform-specific: write to USART_DR

    /* Advance to the next byte */
    state.txByteOffset++;

    /* Check if the entire packet has been transmitted */
    if (state.txByteOffset >= slot.packetLength) {

        /* Reset byte offset for next packet */
        state.txByteOffset = 0;

        /* Advance to next TX slot (circular: 0, 1, 2, 3, 0, ...) */
        state.currentTxSlot++;
        if (state.currentTxSlot >= NUM_TX_SLOTS) {
            state.currentTxSlot = 0;
        }

        /* Mark slot as available for new packets */
        state.pendingTxCount++;
    }

    /* Release the lock */
    state.rxActive = 0;
}


/* ============================================================================
 * FUNCTION: dispatchReceivedPacket (stub)
 * ============================================================================
 *
 * Source: DRV_1.6.13 @ 0x08005468 (referenced by all 3 protocol parsers)
 *
 * Called when a complete, checksum-verified packet has been received.
 * Routes the packet to the appropriate command handler based on the
 * destination address and command byte.
 *
 * The actual dispatch function is complex and firmware-version-specific.
 * This stub shows the interface as called from the protocol parser.
 */
static void dispatchReceivedPacket(UartChannel channel, const uint8_t* buffer)
{
    /*
     * buffer layout (after 0x5A 0xA5 header, which was not stored):
     *   buffer[0] = LEN      (payload length indicator)
     *   buffer[1] = SRC      (source device address)
     *   buffer[2] = DST      (destination device address)
     *   buffer[3] = CMD      (command byte)
     *   buffer[4] = ARG      (argument/register byte)
     *   buffer[5..] = PAYLOAD (variable length)
     *
     * The parser calls this with:
     *   channel = 0 for USART2 (BLE bus)
     *   channel = 1 for USART1 (external bus)
     *   channel = 2 for USART3 (BMS bus)
     */

    uint8_t srcAddr     = buffer[1];
    uint8_t dstAddr     = buffer[2];
    uint8_t command     = buffer[3];
    uint8_t argument    = buffer[4];

    (void)channel;
    (void)srcAddr;
    (void)dstAddr;
    (void)command;
    (void)argument;

    // Actual command routing happens here in the real firmware.
    // Common commands include:
    //   CMD 0x01: Read register
    //   CMD 0x02: Write register
    //   CMD 0x03: Read register response
    //   CMD 0x05: Write register response
}


/* ============================================================================
 * UART INITIALIZATION (Partial Reconstruction)
 * ============================================================================
 *
 * Source: DRV_1.6.13 @ 0x0800700C - 0x0800705A (USART2 setup function)
 *
 * The UART initialization configures all three USART peripherals for
 * 115200 baud, 8N1. The actual implementation uses the STM32 HAL/LL
 * approach of writing to configuration structures.
 *
 * Key configuration values observed in the binary:
 *   - BRR = 0x0271 (115200 baud @ 72MHz PCLK)
 *   - Word length: 8 bits
 *   - Stop bits: 1
 *   - Parity: None
 *   - Flow control: None
 *   - Mode: TX and RX enabled
 *   - DMA: Used for USART2 (possible, based on configuration pattern)
 *
 * ---- Partial Assembly (DRV_1.6.13 @ 0x0800700C - USART2 init) ----
 *
 *   0x0800700C: push   {r4, lr}
 *   0x0800700E: sub    sp, #0x30                 ; Allocate init struct on stack
 *   0x08007010: ldr    r4, [pc, #0x48]           ; r4 = USART2_BASE
 *   0x08007012: mov    r0, r4
 *   0x08007014: bl     #0x8002bc0                ; USART_DeInit(USART2)
 *   ; ... (configuration struct filled on stack) ...
 *   0x08007024: movs   r2, #0xfa                 ; Buffer size = 250 bytes
 *   0x08007026: str    r2, [sp, #0x10]
 *   0x0800702A: movs   r0, #0x80                 ; RX buffer size / DMA config
 *   ; ... (more configuration) ...
 *   0x08007032: movs   r0, #0x20                 ; RXNE interrupt enable
 *   0x08007040: bl     #0x8002cb8                ; USART_Init(USART2, &config)
 *   ; ... (interrupt and DMA setup) ...
 *   0x08007054: bl     #0x8002ba4                ; USART_Cmd(USART2, ENABLE)
 *   0x08007058: add    sp, #0x30
 *   0x0800705A: pop    {r4, pc}
 *
 * ----------------------------------------------------------------
 */
static void initUart(void)
{
    /*
     * Configure all three USARTs for Ninebot protocol communication.
     *
     * Physical connections on the ESC (DRV) board:
     *   USART1 (PA9/PA10):  Debug / external connection
     *   USART2 (PA2/PA3):   BLE dashboard (front display board)
     *   USART3 (PB10/PB11): BMS battery management
     *
     * All configured for: 115200 baud, 8 data bits, no parity, 1 stop bit
     */

    /*
     * In the real firmware, this writes to STM32 peripheral registers:
     *
     * For each USART (1, 2, 3):
     *   1. Enable clock in RCC_APBxENR
     *   2. Configure GPIO pins for alternate function
     *   3. Set BRR = 0x0271 for 115200 baud
     *   4. Configure CR1: UE | TE | RE | RXNEIE (UART enable, TX/RX enable, RX interrupt)
     *   5. Configure CR2: 0 (1 stop bit)
     *   6. Configure CR3: 0 (no flow control)
     *   7. Enable NVIC interrupts for USARTx_IRQn
     */

    /* Initialize parser state for all channels */
    for (int ch = 0; ch < 3; ch++) {
        std::memset(&g_uartParserState[ch], 0, sizeof(UartParserState));
        g_uartParserState[ch].pendingTxCount = NUM_TX_SLOTS;  // All slots available
    }
}


/* ============================================================================
 * CROSS-FIRMWARE COMPARISON NOTES
 * ============================================================================
 *
 * The same protocol functions appear across all firmware binaries with
 * identical logic, differing only in addresses and channel assignments:
 *
 * +--------------------+--------------------+--------------------+
 * | DRV_1.6.13         | DRV_1.2.6          | BLE_1.1.7          |
 * +--------------------+--------------------+--------------------+
 * | buildPacket        | (same logic)       | 2 variants found   |
 * |   0x080036AC       |   Not found*       |   0x08002CC4       |
 * |                    |                    |   0x08002E2C       |
 * +--------------------+--------------------+--------------------+
 * | calculateChecksum  | (same logic)       | (same logic)       |
 * |   0x08002720       |   TBD              |   TBD              |
 * +--------------------+--------------------+--------------------+
 * | parseProtocolByte  | 3 instances        | 2 instances        |
 * |   0x08007128 (U1)  |   0x080058E8 (U1)  |   0x08004C40 (U1)  |
 * |   0x08007468 (U2)  |   0x08005C18 (U2)  |   0x08004F24 (U2)  |
 * |   0x080077AC (U3)  |   0x08005F48 (U3)  |                    |
 * +--------------------+--------------------+--------------------+
 * | dispatchPacket     | (shared)           | (shared)           |
 * |   0x08005468       |   0x08004378       |   TBD              |
 * +--------------------+--------------------+--------------------+
 * | txHandler          | (same logic)       | (similar logic)    |
 * |   0x08007610 (U1)  |   0x08005620 (U1)  |                    |
 * |   0x08006F80 (U2)  |   0x08004E90 (U2)  |                    |
 * |   0x080072D0 (U3)  |   0x080059B0 (U3)  |                    |
 * +--------------------+--------------------+--------------------+
 *
 * * DRV_1.2.6 buildPacket was not pattern-matched but uses identical logic;
 *   the function is called from the same relative offsets in enqueuePacket.
 *
 * BMS_1.7.4.5 protocol functions:
 *   - parseProtocolByte: 1 instance at 0x08004BD0 (USART2 only -> ESC bus)
 *   - UART handlers: USART1 (0x08005D40) and USART2 (0x08005DFC)
 *   - buildPacket: Present but not pattern-matched in first scan
 *   - BMS only has 2 UARTs (USART1 for debug, USART2 for ESC communication)
 *
 * BMS_1.3.4 firmware:
 *   - Appears to be TEA-encrypted (vector table addresses are nonsensical)
 *   - Cannot be analyzed without decryption key
 *
 * ============================================================================
 * PROTOCOL PARSER VARIANT: BLE_1.1.7 USART3 (protocol_parser_3)
 * ============================================================================
 *
 * The USART3 variant in DRV_1.6.13 @ 0x080077AC has a slightly different
 * length validation compared to USART1/USART2:
 *
 *   ; USART1/2 version (at 0x08007142):
 *   0x08007142: cmp    r2, #0xf3       ; if (expectedLen > 0xF3) reject
 *   0x08007144: bls    ...
 *
 *   ; USART3 version (at 0x080077C8):
 *   0x080077C6: subs   r2, r2, #7      ; r2 = expectedLen - 7
 *   0x080077C8: cmp    r2, #0xed       ; if (expectedLen - 7 >= 0xED) reject
 *   0x080077CA: blo    ...             ; (equivalent to expectedLen >= 0xF4)
 *
 * This is mathematically equivalent (0xED + 7 = 0xF4), just compiled differently.
 * Both reject packets where the total post-header data exceeds 243 bytes.
 *
 * ============================================================================
 */


/* ============================================================================
 * USAGE EXAMPLE
 * ============================================================================
 *
 * Demonstrates building and parsing Ninebot protocol packets using the
 * reconstructed functions.
 */

#ifdef EXAMPLE_USAGE

#include <cstdio>

/**
 * Example: Build a "read speed" request packet from ESC to BLE.
 */
void exampleBuildReadSpeedPacket(void)
{
    uint8_t packet[32];
    uint8_t payload[] = { 0x02 };  // Read 2 bytes from register

    uint8_t totalSize = buildPacket(
        static_cast<uint8_t>(DeviceAddress::ESC),  // Source: ESC (0x20)
        static_cast<uint8_t>(DeviceAddress::BLE),  // Destination: BLE (0x21)
        1,              // Payload length: 1 byte
        0x01,           // Command: Read register
        0x26,           // Argument: Speed register (0x26)
        payload,        // Payload data
        packet          // Output buffer
    );

    printf("Built packet (%d bytes): ", totalSize);
    for (int i = 0; i < totalSize; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");
    // Expected output: 5A A5 05 20 21 01 26 02 XX XX
    //   where XX XX is the checksum
}

/**
 * Example: Parse an incoming byte stream.
 */
void exampleParseIncomingStream(void)
{
    /* Simulated incoming packet: Read speed response from BLE */
    uint8_t stream[] = {
        0x5A, 0xA5,         // Header
        0x06,               // LEN = 6 (src + dst + cmd + arg + 2 payload bytes)
        0x21,               // SRC = BLE
        0x20,               // DST = ESC
        0x01,               // CMD = Read register response
        0x26,               // ARG = Speed register
        0xE8, 0x03,         // PAYLOAD = 1000 (0x03E8) = 10.00 km/h
        0x00, 0x00          // Checksum (placeholder - should be calculated)
    };

    /* Calculate the correct checksum for this example */
    uint16_t chk = calculateChecksum(&stream[2], 7);  // LEN through payload
    stream[9]  = chk & 0xFF;
    stream[10] = (chk >> 8) & 0xFF;

    /* Feed bytes through the parser */
    initUart();
    for (size_t i = 0; i < sizeof(stream); i++) {
        parseProtocolByte(stream[i], UartChannel::CHANNEL_ESC);
    }
    // dispatchReceivedPacket will be called if checksum is valid
}

#endif // EXAMPLE_USAGE
