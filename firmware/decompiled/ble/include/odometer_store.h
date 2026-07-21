/**
 * @file odometer_store.h
 * @brief Wear-leveled lifetime-odometer persistence for the always-on dashboard.
 *        Header-only, HAL-free, host-testable.
 *
 * The dashboard is the always-on keeper (Solution D, `docs/POWER_LATCH_SCHEMATIC.md`).
 * It tracks the scooter's **lifetime operating seconds and distance (meters)** and,
 * on **every power-off**, appends the latest values to the 4 KB **user-data** flash
 * page (`0x0800F000`, RC-Servo-aligned — see `docs/BOOTLOADER_V2_CONCEPT.md`).
 *
 * Wear-leveling: the region is treated as a ring of fixed 16-byte records across
 * its flash pages. Each save **appends** one record (no per-save erase); a page is
 * erased only when the ring wraps onto it. On boot, `load()` scans for the record
 * with the highest sequence and a valid CRC — so a power-loss mid-write (partial,
 * bad-CRC record) is ignored and the previous value survives.
 *
 * With a 4 KB region (4×1 KB pages → 256 records) only every 64th save erases a
 * page, so even at one save per power-cycle the flash lasts effectively forever.
 */
#ifndef NINEBOT_ODOMETER_STORE_H
#define NINEBOT_ODOMETER_STORE_H

#include <cstdint>
#include <cstddef>

namespace ninebot {
namespace ble {

/** One persisted lifetime sample (16 bytes, flash-record). */
struct OdoRecord {
    uint32_t seq;       ///< monotonic sequence; 0xFFFFFFFF == empty (erased) slot
    uint32_t seconds;   ///< lifetime operating seconds (hours = seconds/3600)
    uint32_t meters;    ///< lifetime distance meters    (km    = meters/1000)
    uint32_t crc;       ///< CRC-32 over seq|seconds|meters
};

/** Decoded lifetime value handed to the dashboard UI / app. */
struct OdoValue {
    uint32_t seconds = 0;
    uint32_t meters  = 0;
    uint32_t hours()  const { return seconds / 3600u; }
    uint32_t km_x10() const { return meters / 100u; }   ///< 0.1 km units
};

/** Flash access for the user-data region (target driver or host sim). */
struct OdoFlash {
    void* ctx;
    /** read `len` bytes at byte-offset `off` (0..region size) into `buf`. */
    void (*read)(void* ctx, uint32_t off, void* buf, uint32_t len);
    /** program `len` bytes at `off` (flash: target must be erased first). */
    void (*program)(void* ctx, uint32_t off, const void* data, uint32_t len);
    /** erase the page at `pageIdx` (sets it to 0xFF). */
    void (*erasePage)(void* ctx, uint32_t pageIdx);
};

/** Standard CRC-32 (zlib), inline so the module is dependency-free. */
inline uint32_t odo_crc32(const void* data, size_t n) {
    const uint8_t* d = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

/**
 * Append-only, wear-leveled odometer store over `pages` flash pages of
 * `pageBytes` each (default 4×1024 = the 4 KB user-data region).
 */
class OdometerStore {
public:
    OdometerStore(OdoFlash flash, uint32_t pages = 4, uint32_t pageBytes = 1024)
        : f_(flash), pages_(pages), pageBytes_(pageBytes),
          slots_(pages * pageBytes / sizeof(OdoRecord)),
          slotsPerPage_(pageBytes / sizeof(OdoRecord)) {}

    /** Scan for the latest valid record. @return its value, or {0,0} if none. */
    OdoValue load() const {
        OdoValue out;
        bool found = false;
        uint32_t best = 0, bestIdx = 0;
        for (uint32_t i = 0; i < slots_; ++i) {
            OdoRecord r;
            readSlot(i, r);
            if (r.seq == 0xFFFFFFFFu) continue;
            if (odo_crc32(&r, 12) != r.crc) continue;
            if (!found || seqNewer(r.seq, best)) { found = true; best = r.seq; bestIdx = i; }
        }
        if (found) {
            OdoRecord r; readSlot(bestIdx, r);
            out.seconds = r.seconds; out.meters = r.meters;
        }
        return out;
    }

    /**
     * Append a new lifetime sample (call on every power-off, and periodically).
     * Erases the next page only when the ring wraps onto it. @return true on write.
     */
    bool save(uint32_t seconds, uint32_t meters) {
        // Find the latest record to derive seq + the next slot.
        bool found = false;
        uint32_t best = 0, bestIdx = 0;
        for (uint32_t i = 0; i < slots_; ++i) {
            OdoRecord r; readSlot(i, r);
            if (r.seq == 0xFFFFFFFFu || odo_crc32(&r, 12) != r.crc) continue;
            if (!found || seqNewer(r.seq, best)) { found = true; best = r.seq; bestIdx = i; }
        }
        uint32_t idx = found ? (bestIdx + 1) % slots_ : 0;
        uint32_t seq = found ? best + 1 : 1;

        // Erasing rule: if this slot is the first of its page, erase that page
        // (it holds stale records from a previous ring pass).
        if (idx % slotsPerPage_ == 0)
            f_.erasePage(f_.ctx, idx / slotsPerPage_);

        OdoRecord r{seq, seconds, meters, 0};
        r.crc = odo_crc32(&r, 12);
        f_.program(f_.ctx, idx * sizeof(OdoRecord), &r, sizeof(OdoRecord));
        return true;
    }

    uint32_t slotCount() const { return slots_; }

private:
    void readSlot(uint32_t i, OdoRecord& r) const {
        f_.read(f_.ctx, i * sizeof(OdoRecord), &r, sizeof(OdoRecord));
    }
    /** seq compare tolerant of 32-bit wrap. */
    static bool seqNewer(uint32_t a, uint32_t b) {
        return (int32_t)(a - b) > 0;
    }

    OdoFlash f_;
    uint32_t pages_, pageBytes_, slots_, slotsPerPage_;
};

} // namespace ble
} // namespace ninebot

#endif // NINEBOT_ODOMETER_STORE_H
