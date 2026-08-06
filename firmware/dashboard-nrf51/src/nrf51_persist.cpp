/**
 * @file nrf51_persist.cpp
 * @brief nRF51 NVMC backend for the shared OdometerStore (target build only).
 *
 * Obeys the NVMC rules from the nRF51 Reference Manual ch.6 (see docs/NRF51_FLASH_IAP.md):
 *   - only word-aligned 32-bit writes (a byte/half-word write HARD FAULTS)
 *   - bits can only go 1->0 without an erase
 *   - CONFIG is assigned, never OR-ed, and returned to REN
 * The CPU is halted for the duration of each operation, so running this from flash is fine.
 */
#include "nrf51_persist.h"

namespace dash {

namespace {

constexpr uint32_t kNvmcBase = 0x4001E000u;
inline volatile uint32_t& reg(uint32_t a)
{
    return *reinterpret_cast<volatile uint32_t*>(a);
}
inline volatile uint32_t& nvmcReady() { return reg(kNvmcBase + 0x400u); }
inline volatile uint32_t& nvmcConfig() { return reg(kNvmcBase + 0x504u); }
inline volatile uint32_t& nvmcErasePage() { return reg(kNvmcBase + 0x508u); }

constexpr uint32_t kRen = 0u;
constexpr uint32_t kWen = 1u;
constexpr uint32_t kEen = 2u;

inline void waitReady()
{
    while (nvmcReady() == 0u) { }
}

/** @return true when the region is inside the odometer area. */
inline bool inRange(uint32_t off, uint32_t len)
{
    const uint32_t limit = kOdoPages * kOdoPageBytes;
    return (off <= limit) && (len <= limit - off);
}

} // namespace

void nrf51_odo_read(void* /*ctx*/, uint32_t off, void* buf, uint32_t len)
{
    if (!inRange(off, len)) {
        return;
    }
    const volatile uint8_t* src = reinterpret_cast<const volatile uint8_t*>(kOdoBase + off);
    uint8_t* dst = static_cast<uint8_t*>(buf);
    for (uint32_t i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
}

void nrf51_odo_program(void* /*ctx*/, uint32_t off, const void* data, uint32_t len)
{
    /* Word-aligned writes only — anything else would hard fault. The OdometerStore always
     * writes whole 16-byte records, so this holds; refuse rather than fault if it ever changes. */
    if (!inRange(off, len) || ((kOdoBase + off) & 3u) != 0u || (len & 3u) != 0u) {
        return;
    }

    const uint8_t* src = static_cast<const uint8_t*>(data);
    waitReady();
    nvmcConfig() = kWen;
    waitReady();
    for (uint32_t i = 0; i < len; i += 4u) {
        const uint32_t word = static_cast<uint32_t>(src[i]) |
                              (static_cast<uint32_t>(src[i + 1]) << 8) |
                              (static_cast<uint32_t>(src[i + 2]) << 16) |
                              (static_cast<uint32_t>(src[i + 3]) << 24);
        reg(kOdoBase + off + i) = word;
        waitReady();
    }
    nvmcConfig() = kRen;
    waitReady();
}

void nrf51_odo_erase_page(void* /*ctx*/, uint32_t pageIdx)
{
    if (pageIdx >= kOdoPages) {
        return;
    }
    waitReady();
    nvmcConfig() = kEen;
    waitReady();
    nvmcErasePage() = kOdoBase + pageIdx * kOdoPageBytes;
    waitReady();
    nvmcConfig() = kRen;
    waitReady();
}

} // namespace dash
