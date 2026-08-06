/**
 * @file bootloader_config.h
 * @brief nRF51822 bootloader configuration.
 *
 * The nRF51822 bootloader sits at the top of flash (0x0003C000)
 * and is invoked by the Nordic MBR (Master Boot Record) at 0x00000000.
 * The MBR reads UICR.BOOTLOADERADDR to find the bootloader.
 *
 * Update path: PC → USB-UART → VESC → BLE STM32 → UART0 → nRF51822
 * The BLE STM32 enters UART passthrough mode to relay NBU frames to the nRF51822.
 */

#ifndef NRF51_BOOTLOADER_CONFIG_H
#define NRF51_BOOTLOADER_CONFIG_H

#include <stdint.h>

/* ── Flash layout ──────────────────────────────────────────────────────── */

#define NRF_FLASH_BASE          0x00000000U
#define NRF_FLASH_SIZE          (256U * 1024U)   /**< 256 KB */
#define NRF_FLASH_PAGE_SIZE     1024U            /**< 1 KB pages */

/** MBR: 0x00000000–0x00000FFF (4 KB, factory-programmed). */
#define MBR_START               0x00000000U
#define MBR_SIZE                (4U * 1024U)

/** SoftDevice S110 v8.0: 0x00001000–0x00017FFF (92 KB). */
#define SOFTDEVICE_START        0x00001000U
#define SOFTDEVICE_SIZE         (92U * 1024U)
#define SOFTDEVICE_END          (SOFTDEVICE_START + SOFTDEVICE_SIZE)

/** Application: 0x00018000–0x0002BFFF (80 KB). */
#define APP_START_ADDR          0x00018000U
#define APP_MAX_SIZE            (80U * 1024U)

/** Bootloader: 0x0003C000–0x0003FFFF (16 KB). */
#define BOOTLOADER_START        0x0003C000U
#define BOOTLOADER_SIZE         (16U * 1024U)

/** Bootloader settings: stored in last page of bootloader region. */
#define BL_SETTINGS_ADDR        (BOOTLOADER_START + BOOTLOADER_SIZE - NRF_FLASH_PAGE_SIZE)
#define BL_SETTINGS_SIZE        1024U

/** UICR base address. */
#define NRF_UICR_BASE           0x10001000U
/** UICR bootloader address register. */
#define NRF_UICR_BOOTLOADERADDR (*(volatile uint32_t *)(NRF_UICR_BASE + 0x14U))

/* ── SRAM ──────────────────────────────────────────────────────────────── */

#define NRF_SRAM_BASE           0x20000000U
#define NRF_SRAM_SIZE           (16U * 1024U)   /**< 16 KB (QFAA variant) */
#define NRF_STACK_TOP           (NRF_SRAM_BASE + NRF_SRAM_SIZE)

/* ── UART configuration ───────────────────────────────────────────────── */

/**
 * nRF51822 UART0 drives the **Ninebot bus** directly — there is no STM32 on this board
 * (see boards/ble-dashboard/MCU_IDENTIFICATION.md). The pins below are recovered from the stock
 * firmware's uart_init() @0x0001FDB4, which is called two ways to turn the one-wire bus around:
 *
 *   mode A: PSELTXD = P0.15, PSELRXD = P0.20
 *   mode B: PSELTXD = P0.20, PSELRXD = P0.15
 *
 * The NBU update transport is framed request→ACK turn-taking, so the bootloader swaps direction
 * around every transmission exactly like the stock application does.
 */
#define UART_PIN_BUS_A          15   /**< P0.15 — Ninebot bus line A (firmware-confirmed) */
#define UART_PIN_BUS_B          20   /**< P0.20 — Ninebot bus line B (firmware-confirmed) */
#define UART_TX_PIN             UART_PIN_BUS_A  /**< default direction: transmit on A */
#define UART_RX_PIN             UART_PIN_BUS_B
#define UART_BAUD_RATE          115200U

/* ── Update trigger ────────────────────────────────────────────────────── */

/** GPREGRET register: general purpose retention register.
 *  Used to signal bootloader mode across reset.
 *  If GPREGRET == BOOT_MAGIC on startup, enter update mode. */
#define NRF_POWER_GPREGRET     (*(volatile uint32_t *)0x4000051CU)
#define BOOT_ENTER_MAGIC        0xB1    /**< Magic value to enter bootloader */

/** Update flag in bootloader settings page. */
#define UPDATE_FLAG_MAGIC       0xDEAD1234U

/* ── Boot record (persisted .sfw header) ───────────────────────────────────
 *
 * The settings page keeps the signed header of the image that is currently installed, so the
 * bootloader can verify the application on EVERY boot — not just at update time. Layout:
 *
 *   +0x000  update flag      (UPDATE_FLAG_MAGIC when an update is requested, 0 once consumed)
 *   +0x004  boot record magic
 *   +0x008  installed firmware version (for anti-rollback)
 *   +0x100  sfw_header_t (256 B) — magic, version, sizes, SHA-256, ECDSA signature
 *
 * Flash bits only go 1->0 without an erase, so the flag word can be cleared in place while the
 * boot record survives.
 */
#define BL_BOOT_RECORD_MAGIC    0x5242314EU               /**< "NB1R" */
#define BL_FLAG_OFFSET          0U
#define BL_RECORD_MAGIC_OFFSET  4U
#define BL_RECORD_VERSION_OFFSET 8U
#define BL_RECORD_HEADER_OFFSET 256U

/**
 * Boot policy when NO boot record exists (e.g. an image flashed over SWD during development).
 *   1 = fail closed: refuse to boot an image we cannot verify, enter update mode instead
 *   0 = fail open  : fall back to the structural vector-table check and warn over UART
 * A record that EXISTS is always fully verified regardless of this setting.
 */
#ifndef SECURE_BOOT_REQUIRE_RECORD
#define SECURE_BOOT_REQUIRE_RECORD 0
#endif

/* ── SFW target ────────────────────────────────────────────────────────── */

#define MY_TARGET_ID            SFW_TARGET_NRF51822

/** Ninebot bus address for framed (NBU) updates — reached via the BLE STM32 relay. */
#define MY_BUS_ADDR             0x21

/* ── ECDSA public key ──────────────────────────────────────────────────── */

/**
 * Same public key as STM32 bootloaders — single keypair for all boards.
 *
 * !!!! REPLACE WITH YOUR ACTUAL PUBLIC KEY !!!!
 */
#define ECDSA_PUBLIC_KEY { \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  \
}

#endif /* NRF51_BOOTLOADER_CONFIG_H */
