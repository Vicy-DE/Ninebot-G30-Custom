/**
 * @file migration.h
 * @brief Shared constants for the bootloader-migration apps (old 4K → new 16K).
 *
 * Memory map (RC-Servo-aligned; fills the F103C8 64 KB exactly):
 *   0x08000000  16 KB  bootloader   (new secure BL goes here)
 *   0x08004000  40 KB  application  (new app — and where the bl_updater runs)
 *   0x0800E000   4 KB  factory data
 *   0x0800F000   4 KB  user data    (lifetime hours/km — see PROTOCOL_ODOMETER)
 *
 * The two migration apps:
 *   trampoline @ 0x08001000  — booted by the OLD 4K bootloader; jumps to 0x08004000
 *   bl_updater @ 0x08004000  — runs ABOVE the BL region, so it can erase+write the
 *                              16 KB bootloader at 0x08000000 without self-overlap.
 * Packed into one image (pack.py) flashed once via the stock IAP at 0x08001000.
 */
#ifndef MIGRATION_H
#define MIGRATION_H

#include <stdint.h>

/* MIG_BL_BASE/MIG_APP_BASE are overridable at compile time so the same updater
 * source builds the 0x08000000 migration *and* the 16/32-offset installer test
 * (installer runs at 0x08008000, writes the BL to 0x08004000 — firmware/migration16). */
#ifndef MIG_BL_BASE
#define MIG_BL_BASE      0x08000000u   /* bootloader region the updater writes  */
#endif
#define MIG_BL_SIZE      0x00004000u   /* 16 KB                                 */
#ifndef MIG_APP_BASE
#define MIG_APP_BASE     0x08004000u   /* application / where bl_updater runs   */
#endif
#define MIG_TRAMP_BASE   0x08001000u   /* old-bootloader app slot (trampoline)  */

/* Result cookie the apps write so the simulator can confirm the path taken.
 * Placed near the top of the 20 KB SRAM, below the stack. */
#define MIG_COOKIE_ADDR  0x20004F00u
#define MIG_COOKIE_MAGIC 0x4D494752u   /* "MIGR" */

enum {
    MIG_STATUS_TRAMPOLINE   = 1,   /* trampoline about to jump to 0x08004000 */
    MIG_STATUS_BL_INSTALLED = 2,   /* bl_updater wrote + verified the new BL */
    MIG_STATUS_BL_BADIMAGE  = 3,   /* embedded BL failed its CRC check       */
    MIG_STATUS_BL_WRITEFAIL = 4,   /* flash program/verify failed            */
};

/* .ramfunc flash primitives (flash_rt.c). */
#ifdef __cplusplus
extern "C" {
#endif
void flash_rt_unlock(void);
void flash_rt_lock(void);
int  flash_rt_erase_page(uint32_t addr);
int  flash_rt_erase_region(uint32_t addr, uint32_t len);
int  flash_rt_write(uint32_t addr, const uint8_t *data, uint32_t len);
uint32_t crc32_compute(const uint8_t *data, uint32_t length);   /* bootloader/common */

/* Embedded new bootloader image (newbl.s, .incbin). */
extern const uint8_t bl_image_start[];
extern const uint8_t bl_image_end[];
#ifdef __cplusplus
}
#endif

#endif /* MIGRATION_H */
