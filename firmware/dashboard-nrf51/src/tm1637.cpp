/**
 * @file tm1637.cpp
 * @brief TM1637 driver mirroring the stock G30 firmware's bit-banged sequence.
 */
#include "tm1637.h"

namespace dash {

/* Exactly the table found at VMA 0x0002046F in BLE_1.1.7. */
const uint8_t Tm1637::kFont[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, /* 0-7 */
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71, /* 8-9, A-F */
};

void Tm1637::bitDelay()
{
    /* The stock code calls a short delay helper (@0x000180F4) between edges; a few microseconds
       is comfortably inside the TM1637's 250 kHz limit. */
    hal_.delayUs(5);
}

void Tm1637::begin()
{
    hal_.pinDir(PIN_TM1637_CLK, true);
    hal_.pinDir(PIN_TM1637_DIO, true);
    hal_.pinWrite(PIN_TM1637_CLK, true);
    hal_.pinWrite(PIN_TM1637_DIO, true);
    displayOff();
}

void Tm1637::start()
{
    /* Stock: PIN_CNF[4]=PIN_CNF[5]=3 (output), then set(CLK) set(DIO) ... clear(DIO) ... clear(CLK) */
    hal_.pinDir(PIN_TM1637_CLK, true);
    hal_.pinDir(PIN_TM1637_DIO, true);
    hal_.pinWrite(PIN_TM1637_CLK, true);
    hal_.pinWrite(PIN_TM1637_DIO, true);
    bitDelay();
    hal_.pinWrite(PIN_TM1637_DIO, false);   /* DIO falls while CLK high == START */
    bitDelay();
    hal_.pinWrite(PIN_TM1637_CLK, false);
}

void Tm1637::stop()
{
    /* Stock: clear(CLK) clear(DIO) ... set(CLK) ... set(DIO) */
    hal_.pinDir(PIN_TM1637_CLK, true);
    hal_.pinDir(PIN_TM1637_DIO, true);
    hal_.pinWrite(PIN_TM1637_CLK, false);
    hal_.pinWrite(PIN_TM1637_DIO, false);
    bitDelay();
    hal_.pinWrite(PIN_TM1637_CLK, true);
    bitDelay();
    hal_.pinWrite(PIN_TM1637_DIO, true);    /* DIO rises while CLK high == STOP */
    bitDelay();
}

bool Tm1637::writeByte(uint8_t b)
{
    /* Mirrors tm1637_write_byte @0x00018E20: per bit -> CLK low, DIO = bit0, data >>= 1, CLK high. */
    hal_.pinDir(PIN_TM1637_DIO, true);
    for (uint8_t i = 0; i < 8; ++i) {
        hal_.pinWrite(PIN_TM1637_CLK, false);
        hal_.pinWrite(PIN_TM1637_DIO, (b & 0x01) != 0);
        b >>= 1;                             /* LSB first */
        bitDelay();
        hal_.pinWrite(PIN_TM1637_CLK, true);
        bitDelay();
    }

    /* ACK: release DIO, clock once, sample. The device pulls DIO low to acknowledge. */
    hal_.pinWrite(PIN_TM1637_CLK, false);
    hal_.pinDir(PIN_TM1637_DIO, false);
    bitDelay();
    hal_.pinWrite(PIN_TM1637_CLK, true);
    bitDelay();
    const bool acked = (hal_.pinRead(PIN_TM1637_DIO) == false);
    hal_.pinWrite(PIN_TM1637_CLK, false);
    hal_.pinDir(PIN_TM1637_DIO, true);
    return acked;
}

void Tm1637::update(const uint8_t seg[kGrids], uint8_t brightness)
{
    if (brightness > kMaxBrightness) {
        brightness = kMaxBrightness;
    }

    /* Phase 1 — data command: write, auto-increment address. */
    start();
    writeByte(kCmdDataAuto);
    stop();

    /* Phase 2 — address command then the six grid bytes. */
    start();
    writeByte(kCmdAddr0);
    for (uint8_t i = 0; i < kGrids; ++i) {
        writeByte(seg[i]);
    }
    stop();

    /* Phase 3 — display control: ON | brightness. */
    start();
    writeByte(static_cast<uint8_t>(kCmdDisplayOn | brightness));
    stop();
}

void Tm1637::displayOff()
{
    start();
    writeByte(kCmdDisplayOff);
    stop();
}

uint8_t Tm1637::digit(uint8_t value)
{
    return (value < 16) ? kFont[value] : 0x00;
}

void Tm1637::renderNumber(uint8_t seg[kGrids], uint32_t value, uint8_t width,
                          uint8_t first, bool leadingZeros)
{
    if (first >= kGrids) {
        return;
    }
    if (width > kGrids - first) {
        width = static_cast<uint8_t>(kGrids - first);
    }

    for (int8_t i = static_cast<int8_t>(width) - 1; i >= 0; --i) {
        const uint8_t pos = static_cast<uint8_t>(first + i);
        if (value == 0 && i != static_cast<int8_t>(width) - 1 && !leadingZeros) {
            seg[pos] = 0x00;                 /* blank the remaining leading grids */
        } else {
            seg[pos] = kFont[value % 10];
            value /= 10;
        }
    }
}

} // namespace dash
