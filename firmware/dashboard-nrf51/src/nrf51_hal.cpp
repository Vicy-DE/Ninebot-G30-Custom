/**
 * @file nrf51_hal.cpp
 * @brief nRF51822 implementation of dash::Hal, using the register layout recovered from the stock
 *        firmware (see firmware/decompiled/nrf51822/RE_NRF51_DASHBOARD.md).
 *
 * Only built for the target; the host tests use SimHal instead.
 */
#include "nrf51_hal.h"

namespace dash {

/* ---- register map (nRF51822) ---- */
static constexpr uint32_t GPIO_BASE   = 0x50000000u;
static constexpr uint32_t GPIO_OUT    = GPIO_BASE + 0x504u;
static constexpr uint32_t GPIO_OUTSET = GPIO_BASE + 0x508u;
static constexpr uint32_t GPIO_OUTCLR = GPIO_BASE + 0x50Cu;
static constexpr uint32_t GPIO_IN     = GPIO_BASE + 0x510u;
static constexpr uint32_t GPIO_PIN_CNF = GPIO_BASE + 0x700u;

static constexpr uint32_t UART_BASE       = 0x40002000u;
static constexpr uint32_t UART_STARTRX    = UART_BASE + 0x000u;
static constexpr uint32_t UART_STARTTX    = UART_BASE + 0x008u;
static constexpr uint32_t UART_STOPRX     = UART_BASE + 0x004u;
static constexpr uint32_t UART_STOPTX     = UART_BASE + 0x00Cu;
static constexpr uint32_t UART_EVT_RXDRDY = UART_BASE + 0x108u;
static constexpr uint32_t UART_EVT_TXDRDY = UART_BASE + 0x11Cu;
static constexpr uint32_t UART_ENABLE     = UART_BASE + 0x500u;
static constexpr uint32_t UART_PSELTXD    = UART_BASE + 0x50Cu;
static constexpr uint32_t UART_PSELRXD    = UART_BASE + 0x514u;
static constexpr uint32_t UART_RXD        = UART_BASE + 0x518u;
static constexpr uint32_t UART_TXD        = UART_BASE + 0x51Cu;
static constexpr uint32_t UART_BAUDRATE   = UART_BASE + 0x524u;

static constexpr uint32_t BAUD_115200 = 0x01D7E000u; /**< literal lifted from the stock image */

static constexpr uint32_t RTC1_BASE     = 0x40011000u;
static constexpr uint32_t RTC1_START    = RTC1_BASE + 0x000u;
static constexpr uint32_t RTC1_PRESCALER = RTC1_BASE + 0x508u;
static constexpr uint32_t RTC1_COUNTER  = RTC1_BASE + 0x504u;

static constexpr uint32_t WDT_BASE   = 0x40010000u;
static constexpr uint32_t WDT_RR0    = WDT_BASE + 0x600u;
static constexpr uint32_t WDT_RELOAD_KEY = 0x6E524635u;

static constexpr uint32_t CLOCK_BASE       = 0x40000000u;
static constexpr uint32_t CLOCK_LFCLKSTART = CLOCK_BASE + 0x008u;
static constexpr uint32_t CLOCK_LFCLKSRC   = CLOCK_BASE + 0x518u;
static constexpr uint32_t CLOCK_EVT_LFCLKSTARTED = CLOCK_BASE + 0x104u;

/* PIN_CNF field values, exactly as the stock firmware writes them. */
static constexpr uint32_t PINCNF_OUTPUT      = 3u;    /**< DIR=out, INPUT=disconnect */
static constexpr uint32_t PINCNF_INPUT_PULLD = 4u;    /**< DIR=in,  connect, pull-down (stock RX) */
static constexpr uint32_t PINCNF_INPUT_PULLUP = 0x0Cu;/**< DIR=in,  connect, pull-up (stock strap) */

static inline volatile uint32_t& reg(uint32_t a)
{
    return *reinterpret_cast<volatile uint32_t*>(a);
}

void Nrf51Hal::begin()
{
    /* 32.768 kHz LFCLK -> RTC1 as the millisecond timebase. */
    reg(CLOCK_LFCLKSRC) = 1u;                 /* XTAL */
    reg(CLOCK_EVT_LFCLKSTARTED) = 0u;
    reg(CLOCK_LFCLKSTART) = 1u;
    while (reg(CLOCK_EVT_LFCLKSTARTED) == 0u) { }
    reg(RTC1_PRESCALER) = 32u - 1u;           /* ~1.024 kHz tick */
    reg(RTC1_START) = 1u;
}

void Nrf51Hal::pinDir(uint8_t pin, bool output)
{
    reg(GPIO_PIN_CNF + 4u * pin) = output ? PINCNF_OUTPUT : PINCNF_INPUT_PULLUP;
}

void Nrf51Hal::pinWrite(uint8_t pin, bool high)
{
    if (high) {
        reg(GPIO_OUTSET) = (1u << pin);
    } else {
        reg(GPIO_OUTCLR) = (1u << pin);
    }
}

bool Nrf51Hal::pinRead(uint8_t pin)
{
    return ((reg(GPIO_IN) >> pin) & 1u) != 0u;
}

void Nrf51Hal::delayUs(uint32_t us)
{
    /* 16 MHz core: ~4 cycles per iteration of this loop. Good enough for the TM1637. */
    volatile uint32_t n = us * 4u;
    while (n-- > 0u) { __asm__ volatile("nop"); }
}

uint32_t Nrf51Hal::millis()
{
    /* RTC1 ticks at ~1.024 kHz; close enough to ms for UI timing. */
    return reg(RTC1_COUNTER);
}

void Nrf51Hal::uartSetDir(BusDir dir)
{
    const uint8_t tx = (dir == BusDir::TxOnA) ? PIN_BUS_A : PIN_BUS_B;
    const uint8_t rx = (dir == BusDir::TxOnA) ? PIN_BUS_B : PIN_BUS_A;
    if (dir == dir_ && configured_) {
        return;
    }

    /* Mirrors uart_init() @0x0001FDB4 in the stock image. */
    reg(UART_ENABLE) = 0u;
    reg(GPIO_PIN_CNF + 4u * tx) = PINCNF_OUTPUT;
    reg(GPIO_PIN_CNF + 4u * rx) = PINCNF_INPUT_PULLD;
    reg(UART_PSELTXD) = tx;
    reg(UART_PSELRXD) = rx;
    reg(UART_BAUDRATE) = BAUD_115200;
    reg(UART_ENABLE) = 4u;
    reg(UART_STARTTX) = 1u;
    reg(UART_STARTRX) = 1u;
    reg(UART_EVT_RXDRDY) = 0u;
    dir_ = dir;
    configured_ = true;
}

void Nrf51Hal::uartWrite(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        reg(UART_EVT_TXDRDY) = 0u;
        reg(UART_TXD) = data[i];
        while (reg(UART_EVT_TXDRDY) == 0u) { }
    }
    reg(UART_EVT_TXDRDY) = 0u;
}

bool Nrf51Hal::uartRead(uint8_t& out)
{
    if (reg(UART_EVT_RXDRDY) == 0u) {
        return false;
    }
    reg(UART_EVT_RXDRDY) = 0u;
    out = static_cast<uint8_t>(reg(UART_RXD));
    return true;
}

void Nrf51Hal::feedWatchdog()
{
    reg(WDT_RR0) = WDT_RELOAD_KEY;
}

} // namespace dash
