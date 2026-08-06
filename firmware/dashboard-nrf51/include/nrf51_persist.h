/**
 * @file nrf51_persist.h
 * @brief nRF51 backends for the shared dashboard modules that were written against the STM32.
 *
 * The dashboard modules in `firmware/decompiled/` (OdometerStore, DashKeeper,
 * WatchdogSupervisor, DashBridge, DalyClient) are deliberately chip-agnostic — they take
 * function-pointer flash hooks and return "effect" bitmasks instead of touching registers. Porting
 * them to the real dashboard MCU therefore means supplying nRF51 backends, not rewriting logic.
 *
 * This header provides the two pieces that genuinely were STM32-specific:
 *   1. `nrf51_odo_flash()` — an `OdoFlash` driven by the nRF51 **NVMC** (the original comment
 *      referenced the STM32 page `0x0800F000`).
 *   2. `nrf51_wdt_crv()`   — the nRF51 **WDT** reload value, replacing the STM32 **IWDG**
 *      prescaler/reload math in `watchdog_supervisor.h` (`iwdg_params()`).
 */
#ifndef NRF51_PERSIST_H
#define NRF51_PERSIST_H

#include <stdint.h>
#include <stddef.h>

namespace dash {

/* ── Odometer storage region ───────────────────────────────────────────────
 *
 * Placed at APP_START + 80 KB, which is exactly where the bootloader's
 * `nrf_flash_erase_app_region()` stops (APP_MAX_SIZE = 80 KB). A firmware update therefore
 * erases the application but NOT the odometer — lifetime distance/hours survive reflashing.
 *
 *   0x00018000  application (erased on update)
 *   0x0002C000  odometer: 4 x 1 KB pages   <- here
 *   0x0003C000  bootloader
 */
static constexpr uint32_t kOdoBase = 0x0002C000u;
static constexpr uint32_t kOdoPages = 4u;
static constexpr uint32_t kOdoPageBytes = 1024u;   /* nRF51 flash page size */

/** @brief Read from the odometer region. @p off is relative to kOdoBase. */
void nrf51_odo_read(void* ctx, uint32_t off, void* buf, uint32_t len);

/** @brief Program words into the odometer region (word aligned only). @sideeffects */
void nrf51_odo_program(void* ctx, uint32_t off, const void* data, uint32_t len);

/** @brief Erase one 1 KB odometer page by index (0..kOdoPages-1). @sideeffects */
void nrf51_odo_erase_page(void* ctx, uint32_t pageIdx);

/**
 * @brief WDT reload value for a target timeout.
 *
 * The nRF51 watchdog counts the 32.768 kHz LFCLK, so `CRV = timeout_ms * 32768 / 1000 - 1`
 * (there is no prescaler to choose, unlike the STM32 IWDG).
 * @param timeout_ms desired timeout, e.g. 5000 to match the documented dashboard requirement
 */
constexpr uint32_t nrf51_wdt_crv(uint32_t timeout_ms)
{
    return (static_cast<uint64_t>(timeout_ms) * 32768u / 1000u) - 1u;
}

/** Dashboard watchdog timeout — same 5 s requirement the STM32 version used. */
static constexpr uint32_t kWdtTimeoutMs = 5000u;

} // namespace dash

#endif // NRF51_PERSIST_H
