/**
 * @file bootloader_config.h
 * @brief Board-specific configuration for STM32F103 bootloaders.
 *
 * This header selects the target board at compile time via -DTARGET_BOARD=x.
 * Both the BLE STM32 and BMS STM32 share the same bootloader code with
 * different UART and GPIO configurations.
 */

#ifndef BOOTLOADER_CONFIG_H
#define BOOTLOADER_CONFIG_H

#include <stdint.h>

/* ── Target board selection ────────────────────────────────────────────── */

#define BOARD_BLE_STM32   1   /**< BLE Dashboard STM32F103C8T6 */
#define BOARD_BMS_STM32   2   /**< BMS Battery STM32F103C8T6 */

#ifndef TARGET_BOARD
#error "Define TARGET_BOARD as BOARD_BLE_STM32 or BOARD_BMS_STM32"
#endif

/* ── Flash layout (same for both 64KB STM32F103C8T6 boards) ───────────── */

#define FLASH_BASE_ADDR        0x08000000U
#define FLASH_SIZE             (64U * 1024U)
#define FLASH_PAGE_SIZE        1024U          /**< 1 KB pages (medium density) */

/** Bootloader occupies 16 KB (pages 0-15). */
#define BOOTLOADER_START       FLASH_BASE_ADDR
#define BOOTLOADER_SIZE        (16U * 1024U)
#define BOOTLOADER_PAGES       16U

/** Application starts at 0x08004000. */
#define APP_START_ADDR         (FLASH_BASE_ADDR + BOOTLOADER_SIZE)
#define APP_MAX_SIZE           (46U * 1024U)
#define APP_PAGES              46U

/** Config/flags block: last 2 KB. */
#define CONFIG_START_ADDR      0x0800F800U
#define CONFIG_SIZE            (2U * 1024U)

/** Update request flag magic value (stored in config page). */
#define UPDATE_FLAG_MAGIC      0xDEAD1234U
#define UPDATE_FLAG_ADDR       CONFIG_START_ADDR

/* ── UART configuration (board-specific) ───────────────────────────────── */

#if TARGET_BOARD == BOARD_BLE_STM32
    /*
     * BLE STM32: USART2 on PB6(TX)/PB7(RX) connects to VESC.
     * Used for firmware updates and runtime Ninebot protocol.
     *
     * Note: Stock firmware uses USART2 with remap to PB6/PB7.
     * USART1 on PA9/PA10 is used for nRF51822 internal communication.
     */
    #define UPDATE_USART_BASE      0x40004400U   /**< USART2 base */
    #define UPDATE_USART_IRQn      38            /**< USART2 IRQ number */

    /* USART2 remapped to PB6/PB7 */
    #define UPDATE_UART_TX_PORT    'B'
    #define UPDATE_UART_TX_PIN     6
    #define UPDATE_UART_RX_PORT    'B'
    #define UPDATE_UART_RX_PIN     7
    #define UPDATE_UART_REMAP      1             /**< AFIO remap needed */

    /** SFW target ID for this board */
    #define MY_TARGET_ID           SFW_TARGET_BLE_STM32

    /** Power button on PB12 (active-low) — used as update trigger */
    #define UPDATE_BUTTON_PORT     'B'
    #define UPDATE_BUTTON_PIN      12

#elif TARGET_BOARD == BOARD_BMS_STM32
    /*
     * BMS STM32: USART2 on PA2(TX)/PA3(RX) connects to VESC.
     * USART1 on PA9/PA10 is debug/factory (alternative update port).
     */
    #define UPDATE_USART_BASE      0x40004400U   /**< USART2 base */
    #define UPDATE_USART_IRQn      38

    #define UPDATE_UART_TX_PORT    'A'
    #define UPDATE_UART_TX_PIN     2
    #define UPDATE_UART_RX_PORT    'A'
    #define UPDATE_UART_RX_PIN     3
    #define UPDATE_UART_REMAP      0             /**< No remap needed */

    #define MY_TARGET_ID           SFW_TARGET_BMS_STM32

    /** No easily accessible button on BMS — use flag-only update trigger */
    #define UPDATE_BUTTON_PORT     0
    #define UPDATE_BUTTON_PIN      0

#else
    #error "Invalid TARGET_BOARD"
#endif

/* ── UART baud rate ────────────────────────────────────────────────────── */

#define SYSTEM_CLOCK           72000000U   /**< 72 MHz (HSE 8MHz × PLL 9) */
#define APB1_CLOCK             36000000U   /**< APB1 = HCLK/2 = 36 MHz */
#define UART_BAUD_RATE         115200U
#define UART_BRR_VALUE         (APB1_CLOCK / UART_BAUD_RATE)  /**< 0x0139 = 313 */

/* ── Timing ────────────────────────────────────────────────────────────── */

/** SysTick reload for 1 ms tick at 72 MHz. */
#define SYSTICK_RELOAD         (SYSTEM_CLOCK / 1000U - 1U)

/** Maximum time to wait for XMODEM transfer start (seconds). */
#define UPDATE_TIMEOUT_SEC     60U

/* ── ECDSA public key ──────────────────────────────────────────────────── */

/**
 * Developer's ECDSA-P256 public key (raw X‖Y, 64 bytes).
 *
 * !!!! REPLACE THIS WITH YOUR ACTUAL PUBLIC KEY !!!!
 * Generate with: python tools/generate_keys.py
 * The corresponding private key stays on the developer's PC.
 */
#define ECDSA_PUBLIC_KEY { \
    /* X (32 bytes) */ \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    /* Y (32 bytes) */ \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  \
}

#endif /* BOOTLOADER_CONFIG_H */
