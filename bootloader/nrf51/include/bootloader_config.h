/**
 * @file bootloader_config.h
 * @brief nRF51822 bootloader configuration.
 *
 * The nRF51822 bootloader sits at the top of flash (0x0003C000)
 * and is invoked by the Nordic MBR (Master Boot Record) at 0x00000000.
 * The MBR reads UICR.BOOTLOADERADDR to find the bootloader.
 *
 * Update path: PC → USB-UART → VESC → BLE STM32 → UART0 → nRF51822
 * The BLE STM32 enters UART passthrough mode to relay XMODEM to the nRF51822.
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
 * nRF51822 UART0 connects to BLE STM32 on the dashboard PCB.
 * Pin assignments from nRF51822 analysis:
 *   TX → STM32 USART1_RX (PA10)
 *   RX → STM32 USART1_TX (PA9)
 *
 * Actual GPIO pins depend on the PCB layout. From firmware analysis,
 * the nRF51822 GPIO config for UART is set at runtime.
 * Common nRF51822 UART pins on Ninebot BLE board:
 */
#define UART_TX_PIN             9    /**< P0.09 → STM32 PA10 (USART1_RX) */
#define UART_RX_PIN             11   /**< P0.11 → STM32 PA9  (USART1_TX) */
#define UART_BAUD_RATE          115200U

/* ── Update trigger ────────────────────────────────────────────────────── */

/** GPREGRET register: general purpose retention register.
 *  Used to signal bootloader mode across reset.
 *  If GPREGRET == BOOT_MAGIC on startup, enter update mode. */
#define NRF_POWER_GPREGRET     (*(volatile uint32_t *)0x4000051CU)
#define BOOT_ENTER_MAGIC        0xB1    /**< Magic value to enter bootloader */

/** Update flag in bootloader settings page. */
#define UPDATE_FLAG_MAGIC       0xDEAD1234U

/* ── SFW target ────────────────────────────────────────────────────────── */

#define MY_TARGET_ID            SFW_TARGET_NRF51822

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
