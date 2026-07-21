# Dashboard ⇄ VESC Protocol — Lifetime Odometer + Save-on-Power-off

**Date:** 2026-06-14
**Builds on:** NB+ ([`PROTOCOL_V2.md`](PROTOCOL_V2.md), header `5A A6`) and the
always-on-keeper energy concept ([`POWER_LATCH_SCHEMATIC.md`](POWER_LATCH_SCHEMATIC.md),
Solution D). Persistence: [`odometer_store.h`](../firmware/decompiled/ble/include/odometer_store.h)
in the 4 KB user-data page (`0x0800F000`, [`BOOTLOADER_V2_CONCEPT.md`](BOOTLOADER_V2_CONCEPT.md)).

---

## 1. The concept
The **dashboard is the always-on keeper**: it sleeps in STOP (~µA) on the standby
rail and owns power on/off (it cuts VESC power via the Daly on long-press). Because
it is the one part that never loses power, it is the **right place to hold the
scooter's lifetime counters** — total **operating hours** and **distance (km)** —
exactly like the stock dashboard does. The VESC accumulates the trip while running;
the dashboard reads it and, **on every power-off, persists the lifetime totals to
flash** so they survive the Daly cutting VESC power (and survive app updates — the
user-data page is outside the app region).

Two values are tracked for the scooter's life: `seconds` (→ hours) and `meters`
(→ km). They are **monotonic** (only ever increase).

---

## 2. New NB+ frames (TYPE/REG additions)
On the NB+ wire (`5A A6 | VER | FLAGS | SRC | DST | TYPE | REG | LEN | payload | CRC16`):

| TYPE | REG | Dir | Name | Payload |
|------|-----|-----|------|---------|
| `0x01` READ | `0x90` | dash→VESC | **ODO_GET** | — → resp = `odo_trip_t` |
| `0x06` STREAM | `0x90` | VESC→dash | **ODO_STREAM** | `odo_trip_t` (pushed ~1 Hz while running) |
| `0x02` WRITE | `0x91` | dash→VESC | **ODO_SEED** | `odo_life_t` — dash gives the VESC the lifetime base at wake |
| `0x02` WRITE+ACK | `0x92` | dash→VESC | **PREPARE_OFF** | — → VESC flushes final trip, replies ODO_GET, stops motor |

```c
typedef struct __attribute__((packed)) {     // odo_trip_t (8 B) — this-session totals
    uint32_t trip_seconds;   // seconds powered this session
    uint32_t trip_meters;    // meters travelled this session
} odo_trip_t;

typedef struct __attribute__((packed)) {     // odo_life_t (8 B) — lifetime totals
    uint32_t life_seconds;   // = persisted lifetime seconds
    uint32_t life_meters;
} odo_life_t;
```

The VESC keeps only the **trip** counters (it has no non-volatile store it can
trust across a hard power-cut); the **dashboard** owns the lifetime totals and
does `lifetime += trip`.

---

## 3. The save-on-power-off handshake (the important part)
Power-off is a **long-press** detected by the dashboard. Before it tells the Daly
to cut VESC power, it must capture the final trip — there's a small window:

```
 RUN
  │  (dashboard has lifetime base in RAM, loaded from flash at boot)
  │  VESC ── ODO_STREAM (trip_seconds, trip_meters) ──► dashboard   (~1 Hz)
  │
long-press OFF
  ▼
 dashboard ── PREPARE_OFF (reliable, ACK_REQ) ──► VESC
 VESC: stop motor output, freeze + reply final trip
 dashboard ◄── ODO_GET resp (final trip) ── VESC
  ▼
 lifetime_seconds += trip_seconds ; lifetime_meters += trip_meters
 OdometerStore.save(lifetime_seconds, lifetime_meters)   ← append to user-data flash
  ▼
 dashboard ── Daly 0xD9 OFF ──► cut VESC power ; enter STOP
```

- **PREPARE_OFF is reliable** (NB+ `FLAGS.ACK_REQ` + seq) so the final-trip exchange
  can't be silently lost. If the VESC doesn't answer within ~150 ms, the dashboard
  falls back to the **last ODO_STREAM** it received (at most ~1 s of distance lost) —
  it still saves, then cuts power. **It never cuts power without saving.**
- On **wake**, the dashboard reads its lifetime totals from flash and may **ODO_SEED**
  the VESC (so a richer dash can show lifetime live); the VESC resets its trip to 0.
- A periodic background save (e.g. every few minutes of riding) bounds worst-case
  loss if power is yanked physically (battery disconnect) with no long-press.

---

## 4. Persistence (flash) — `odometer_store.h`
Wear-leveled append-only ring over the 4 KB user-data page (4×1 KB → 256 × 16-byte
records). Each `save()` appends `{seq, seconds, meters, crc32}`; a page is erased
only when the ring wraps onto it (≈ every 64 saves), so flash lasts effectively
forever. `load()` returns the highest-seq valid-CRC record — a power-loss
mid-write leaves a bad-CRC partial record that is **ignored**, so the previous
lifetime value always survives. Host-tested (`test_new_modules.cpp` `Odometer.*`):
fresh-reads-zero, save/load round-trip, latest-wins-across-page-boundary,
power-loss-partial-write-ignored.

---

## 5. App compatibility
The phone app still reads stock registers; the dashboard exposes lifetime totals
on the existing stock fields it already synthesizes (`docs/REGISTER_MAP.md`,
`docs/APP_COMPATIBILITY.md`): total operating time `0x32` (s) and total distance
`0x29` (m). So the lifetime hours/km show in the original app with no app change.

## 6. Status
- ✅ Persistence implemented + host-tested (`odometer_store.h`, 4 tests).
- ⬜ Wire ODO frames into the dashboard `DashBridge` + the VESC lisp (`g30_dash.lisp`:
  accumulate trip, answer PREPARE_OFF, ODO_STREAM). ⬜ Hook `OdometerStore.save()` into the
  `dash_keeper` power-off path (`EFF_DALY_OFF` → save first, then cut).
