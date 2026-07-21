/**
 * @file wire_finder.h
 * @brief Passive classifier for the two tapped dashboard-plug wires.
 *
 * Context: a NUCLEO-C542RC taps the two signal wires of the dashboard's internal
 * plug (one to the **BT** chip = nRF51822, one to the **dashboard** MCU = STM32F103)
 * via Arduino header pins **A0 = PA0** and **A2 = PA4**. We do not assume which wire
 * is which — this module *finds it out* by passively sniffing each line at 115200 8N1
 * and looking for the firmware-verified Ninebot frame:
 *
 *     5A A5 | LEN | SRC | DST | CMD | ARG | payload[LEN] | CK_lo CK_hi
 *     LEN = payload byte count;  CK = (sum(LEN..payload)) ^ 0xFFFF, little-endian.
 *
 * From the decoded SRC/DST it infers each line's role (0x21 = BLE/nRF board,
 * 0x3E = App, 0x3F = PC, 0x20 = ESC, 0x22 = BMS). The result tells the operator
 * which Arduino pin to bridge to the PC for the bootloader dump.
 *
 * Pure logic, no hardware — host-testable. The C542RC firmware feeds it the bytes
 * it captures on each pin.
 */
#ifndef WIRE_FINDER_H
#define WIRE_FINDER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_LINES        2          /**< line 0 = A0/PA0, line 1 = A2/PA4 */
#define WF_HDR1         0x5A
#define WF_HDR2         0xA5

/** What a sniffed line looks like after a capture window. */
typedef enum {
    WF_IDLE = 0,    /**< no bytes seen */
    WF_NOISE,       /**< bytes seen but no valid Ninebot frame */
    WF_NINEBOT,     /**< >=1 checksum-valid Ninebot frame decoded */
} wf_kind_t;

/** Per-line statistics + inference. */
typedef struct {
    wf_kind_t kind;
    uint32_t  bytes;        /**< total bytes observed */
    uint32_t  frames_ok;    /**< checksum-valid frames decoded */
    uint32_t  frames_bad;   /**< frames whose checksum failed */
    uint16_t  addr_mask;    /**< bitmask of distinct SRC addresses seen (bit = addr&0x1F semantics, see .c) */
    uint8_t   last_src;     /**< SRC of the last valid frame */
    uint8_t   last_dst;     /**< DST of the last valid frame */
} wf_line_t;

/** One line's running parse state (internal). */
typedef struct {
    uint8_t  buf[262];      /**< 2 hdr + LEN+7 body max (LEN<=255 -> body<=262) */
    uint8_t  state;         /**< 0 idle, 1 saw 5A, 2 collecting */
    uint16_t idx;
    uint16_t expected;
    wf_line_t stat;
} wf_lstate_t;

typedef struct {
    wf_lstate_t line[WF_LINES];
} wire_finder_t;

/** Reset all state (call before a capture window). */
void wf_reset(wire_finder_t *wf);

/** Feed one byte observed on a line (0 or 1). */
void wf_feed(wire_finder_t *wf, int line, uint8_t b);

/** Finalize classification into out[WF_LINES] (kind from counters). */
void wf_classify(const wire_finder_t *wf, wf_line_t out[WF_LINES]);

/**
 * Human-readable role for a line, given its stats. Returns a static string like
 * "BLE/nRF (BT) side", "dashboard/app side", "active (unidentified)", "noise",
 * or "idle". `arduino_pin` ("A0"/"A2") is only used by the caller for printing.
 */
const char *wf_role(const wf_line_t *st);

/** The Ninebot checksum, exposed for tests/senders. */
uint16_t wf_checksum(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* WIRE_FINDER_H */
