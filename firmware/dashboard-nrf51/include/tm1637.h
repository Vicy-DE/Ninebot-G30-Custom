/**
 * @file tm1637.h
 * @brief TM1637 6-digit display driver — a faithful re-implementation of the stock G30 routine.
 *
 * Recovered byte-exact from the stock nRF51 firmware (`BLE_1.1.7`):
 *   tm1637_start()      @0x00018DA0   tm1637_stop()       @0x00018DD8
 *   tm1637_write_byte() @0x00018E20   tm1637_update()     @0x00019DAA
 *   7-segment font                    @VMA 0x0002046F
 * See firmware/decompiled/nrf51822/RE_NRF51_DASHBOARD.md.
 *
 * Wire protocol: 2-wire, **LSB-first**, 8 bits then an ACK bit, on P0.04 (DIO) / P0.05 (CLK).
 */
#ifndef TM1637_H
#define TM1637_H

#include "dash_hal.h"

namespace dash {

/** @brief Driver for the dashboard's TM1637 segment display. */
class Tm1637 {
public:
    static constexpr uint8_t kGrids = 6;          /**< stock writes exactly 6 segment bytes */
    static constexpr uint8_t kCmdDataAuto = 0x40; /**< data cmd, auto-increment address */
    static constexpr uint8_t kCmdDataFixed = 0x44;/**< data cmd, fixed address */
    static constexpr uint8_t kCmdAddr0 = 0xC0;    /**< address cmd, digit 0 */
    static constexpr uint8_t kCmdDisplayOn = 0x88;/**< display ON; OR in brightness 0-7 */
    static constexpr uint8_t kCmdDisplayOff = 0x80;
    static constexpr uint8_t kMaxBrightness = 7;
    static constexpr uint8_t kSegDot = 0x80;      /**< segment DP bit */

    /** @brief Common-cathode 7-segment font for 0-9 then A-F (exactly the stock table). */
    static const uint8_t kFont[16];

    explicit Tm1637(Hal& hal) : hal_(hal) {}

    /** @brief Configure DIO/CLK as outputs and blank the display. @sideeffects */
    void begin();

    /**
     * @brief Push a full 6-grid frame, then set brightness — the stock 3-phase sequence.
     * @param seg        six raw segment bytes (grid 0 first)
     * @param brightness 0-7; clamped
     * @sideeffects drives P0.04/P0.05
     */
    void update(const uint8_t seg[kGrids], uint8_t brightness);

    /** @brief Turn the display off (writes 0x80). @sideeffects */
    void displayOff();

    /** @brief Map a digit 0-15 to its segment byte; returns 0 (blank) when out of range. */
    static uint8_t digit(uint8_t value);

    /**
     * @brief Render an unsigned number right-aligned into a grid buffer.
     * @param seg    destination, kGrids bytes (untouched grids are blanked)
     * @param value  number to show
     * @param width  how many grids to use, starting at @p first
     * @param first  index of the leftmost grid to write
     * @param leadingZeros pad with '0' instead of blanks
     */
    static void renderNumber(uint8_t seg[kGrids], uint32_t value, uint8_t width,
                             uint8_t first = 0, bool leadingZeros = false);

private:
    void start();      /**< START: DIO falls while CLK is high. @sideeffects */
    void stop();       /**< STOP: DIO rises while CLK is high.  @sideeffects */
    bool writeByte(uint8_t b); /**< 8 bits LSB-first + ACK; true when acknowledged. @sideeffects */
    void bitDelay();

    Hal& hal_;
};

} // namespace dash

#endif // TM1637_H
