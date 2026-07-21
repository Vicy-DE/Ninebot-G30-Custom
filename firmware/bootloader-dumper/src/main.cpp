/**
 * @file main.cpp
 * @brief Stock-bootloader dumper — a tiny app that reads the otherwise-undumpable
 *        4 KB stock bootloader (0x08000000-0x08000FFF) and emits it over the
 *        dashboard cable UART (USART2, half-duplex, 115200) as framed hex + CRC32.
 *
 * **Flash it via the stock bootloader / IAP** at the app base 0x08001000
 * (no soldering — see docs/DASHBOARD_NO_SOLDER_FLASH.md). On boot it loops,
 * re-emitting the dump every ~1 s so a USB-TTL on the cable catches a full frame.
 * Receive + verify + save with tools/dump_bootloader.py.
 *
 * Output format (ASCII, marker-framed so raw bytes can't collide):
 * @code
 *   ==NBDUMP== base=08000000 len=4096 crc32=XXXXXXXX
 *   <8192 hex chars, newline every 32 bytes>
 *   ==NBDUMPEND==
 * @endcode
 *
 * Self-contained (only stm32f103.h); reuses the dashboard startup + linker.
 * Reads only — it never writes flash, so it cannot brick anything.
 */
#include "stm32f103.h"
#include <cstdint>

namespace {

constexpr uint32_t BL_BASE = 0x08000000;   ///< stock bootloader start
constexpr uint32_t BL_LEN  = 0x1000;       ///< 4 KB (pages 0-3)

/** HSE 8 MHz -> PLL x9 -> 72 MHz; enable GPIOA + USART2 clocks. */
void clock_init() {
    RCC_CR |= RCC_CR_HSEON;
    while (!(RCC_CR & RCC_CR_HSERDY)) {}
    FLASH_ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC_CFGR = RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL9 | RCC_CFGR_PPRE1_DIV2;
    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY)) {}
    RCC_CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC_CFGR & 0x0Cu) != RCC_CFGR_SWS_PLL) {}
    RCC_APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;
}

/** USART2 on PA2, single-wire half-duplex (the dashboard cable DATA line), 115200. */
void usart2_init() {
    // PA2 = AF open-drain 50 MHz (CNF=0b11, MODE=0b11 -> nibble 0xF)
    uint32_t v = GPIOA->CRL;
    v &= ~(0xFu << (2 * 4));
    v |= (0xFu << (2 * 4));
    GPIOA->CRL = v;
    USART2->BRR = USART2_BRR_115200;
    USART2->CR3 = USART_CR3_HDSEL;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;
}

void tx(uint8_t b) {
    while (!(USART2->SR & USART_SR_TXE)) {}
    USART2->DR = b;
}
void puts_(const char* s) { while (*s) tx(uint8_t(*s++)); }

const char HEX[] = "0123456789ABCDEF";
void put_nib(uint8_t n) { tx(uint8_t(HEX[n & 0xF])); }
void put_hex8(uint8_t v) { put_nib(v >> 4); put_nib(v); }
void put_hex32(uint32_t v) { for (int i = 28; i >= 0; i -= 4) put_nib(uint8_t(v >> i)); }

/** Standard CRC-32 (zlib): reflected poly 0xEDB88320, init/xorout 0xFFFFFFFF. */
uint32_t crc32(const uint8_t* d, uint32_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

} // namespace

extern "C" int main() {
    clock_init();
    usart2_init();

    const uint8_t* bl = reinterpret_cast<const uint8_t*>(BL_BASE);

    for (;;) {
        uint32_t crc = crc32(bl, BL_LEN);
        puts_("\r\n==NBDUMP== base=08000000 len=4096 crc32=");
        put_hex32(crc);
        puts_("\r\n");
        for (uint32_t i = 0; i < BL_LEN; ++i) {
            put_hex8(bl[i]);
            if ((i & 31u) == 31u) puts_("\r\n");
        }
        puts_("==NBDUMPEND==\r\n");

        for (volatile uint32_t d = 0; d < 8000000u; ++d) {}  // ~1 s gap @72 MHz
    }
}
