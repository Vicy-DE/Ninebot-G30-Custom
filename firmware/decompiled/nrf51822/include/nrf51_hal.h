/**
 * @file nrf51_hal.h
 * @brief Hardware Abstraction Layer for nRF51822 BLE co-processor.
 *
 * Provides platform-independent interfaces for the nRF51822 peripherals
 * used by the BLE application firmware. When compiled for the host
 * (simulation mode), these are backed by software implementations.
 *
 * Unlike the STM32 HAL, the nRF51822 HAL models the Nordic SoftDevice
 * interaction pattern where BLE operations are performed via SVC calls
 * and flash operations are asynchronous with event callbacks.
 *
 * Peripheral mapping (from nRF51822 firmware analysis):
 *   ┌───────────────────────────────────────────────────────────┐
 *   │  nRF51822 Peripherals                                    │
 *   ├──────────────┬──────────────────────────────────────────┤
 *   │ UART0        │ Communication with STM32 (115200 8N1)    │
 *   │ GPIOTE       │ GPIO events (button, external trigger)   │
 *   │ TIMER1       │ Application timer                        │
 *   │ RTC1         │ Low-power real-time counter (BLE timing) │
 *   │ WDT          │ Watchdog timer                           │
 *   │ SWI0         │ Software interrupt 0                     │
 *   │ SWI2         │ SoftDevice system events (flash done)    │
 *   └──────────────┴──────────────────────────────────────────┘
 *
 * Memory Map (nRF51822-QFAA, 256KB Flash, 16KB SRAM):
 *   0x00000000 – 0x00017FFF : SoftDevice S110 v8.0 (96 KB)
 *   0x00018000 – 0x000205CC : Application (~34 KB)
 *   0x0003C000 – 0x0003FFFF : DFU Bootloader (if present)
 *   0x10001000 – 0x10001FFF : UICR
 *   0x20000000 – 0x20003FFF : SRAM (16 KB)
 *
 * @see nrf51822_analysis_BLE_1.1.7.txt for disassembly findings
 */

#ifndef NINEBOT_NRF51_HAL_H
#define NINEBOT_NRF51_HAL_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <vector>
#include <array>

namespace ninebot {
namespace nrf51 {

/* =========================================================================
 * nRF51822 Memory Map Constants
 * =========================================================================
 * From nRF51822 Product Specification v3.3
 */

static constexpr uint32_t FLASH_BASE           = 0x00000000;
static constexpr uint32_t SOFTDEVICE_SIZE      = 0x00018000;  ///< 96 KB (S110 v8.0)
static constexpr uint32_t APP_BASE             = 0x00018000;  ///< Application start
static constexpr uint32_t APP_MAX_SIZE         = 0x00024000;  ///< 144 KB max
static constexpr uint32_t BOOTLOADER_BASE      = 0x0003C000;  ///< DFU bootloader
static constexpr uint32_t FLASH_SIZE           = 256 * 1024;  ///< Total 256 KB
static constexpr uint32_t FLASH_PAGE_SIZE      = 1024;        ///< 1 KB pages

static constexpr uint32_t SRAM_BASE            = 0x20000000;
static constexpr uint32_t SRAM_SIZE            = 16 * 1024;   ///< 16 KB (QFAA)
static constexpr uint32_t SOFTDEVICE_RAM       = 0x20002000;  ///< App RAM start
static constexpr uint32_t APP_RAM_SIZE         = 0x00002000;  ///< ~8 KB for app

static constexpr uint32_t UICR_BASE            = 0x10001000;

/* =========================================================================
 * nRF51822 Peripheral Base Addresses
 * ========================================================================= */

static constexpr uint32_t UART0_BASE           = 0x40002000;
static constexpr uint32_t GPIOTE_BASE          = 0x40006000;
static constexpr uint32_t TIMER0_BASE          = 0x40008000;
static constexpr uint32_t TIMER1_BASE          = 0x40009000;
static constexpr uint32_t TIMER2_BASE          = 0x4000A000;
static constexpr uint32_t RTC0_BASE            = 0x4000B000;
static constexpr uint32_t RTC1_BASE            = 0x40011000;
static constexpr uint32_t WDT_BASE             = 0x40010000;
static constexpr uint32_t NVMC_BASE            = 0x4001E000;
static constexpr uint32_t GPIO_BASE            = 0x50000000;

/* UART0 register offsets */
static constexpr uint32_t UART0_TASKS_STARTTX  = 0x008;
static constexpr uint32_t UART0_TASKS_STARTRX  = 0x000;
static constexpr uint32_t UART0_EVENTS_RXDRDY  = 0x108;
static constexpr uint32_t UART0_EVENTS_TXDRDY  = 0x110;
static constexpr uint32_t UART0_INTEN          = 0x300;
static constexpr uint32_t UART0_ENABLE         = 0x500;
static constexpr uint32_t UART0_PSELTXD        = 0x50C;
static constexpr uint32_t UART0_PSELRXD        = 0x514;
static constexpr uint32_t UART0_RXD            = 0x518;
static constexpr uint32_t UART0_TXD            = 0x51C;
static constexpr uint32_t UART0_BAUDRATE       = 0x524;

/* UART baud rate register values */
static constexpr uint32_t UART_BAUDRATE_115200 = 0x01D7E000;

/* =========================================================================
 * BLE SoftDevice SVC Numbers (S110 v8.0)
 * =========================================================================
 * Mapped from the 58 SVC calls found in firmware analysis.
 */

namespace svc {
    /* SoftDevice enable / event */
    static constexpr uint8_t SD_BLE_ENABLE             = 0x60;
    static constexpr uint8_t SD_BLE_EVT_GET            = 0x61;
    static constexpr uint8_t SD_BLE_UUID_VS_ADD        = 0x63;
    static constexpr uint8_t SD_BLE_OPT_SET            = 0x65;

    /* GAP */
    static constexpr uint8_t SD_BLE_GAP_ADV_DATA_SET   = 0x72;
    static constexpr uint8_t SD_BLE_GAP_ADV_START      = 0x73;
    static constexpr uint8_t SD_BLE_GAP_ADV_STOP       = 0x74;
    static constexpr uint8_t SD_BLE_GAP_DISCONNECT     = 0x76;
    static constexpr uint8_t SD_BLE_GAP_TX_POWER_SET   = 0x77;
    static constexpr uint8_t SD_BLE_GAP_PPCP_SET       = 0x7A;
    static constexpr uint8_t SD_BLE_GAP_APPEARANCE_SET = 0x79;

    /* GATTS */
    static constexpr uint8_t SD_BLE_GATTS_SERVICE_ADD   = 0x90;
    static constexpr uint8_t SD_BLE_GATTS_CHAR_ADD      = 0x92;
    static constexpr uint8_t SD_BLE_GATTS_HVX           = 0x97;

    /* L2CAP */
    static constexpr uint8_t SD_BLE_L2CAP_CID_REGISTER  = 0xA0;
    static constexpr uint8_t SD_BLE_L2CAP_CID_UNREGISTER= 0xA1;
    static constexpr uint8_t SD_BLE_L2CAP_TX             = 0xA2;
    static constexpr uint8_t SD_BLE_L2CAP_REPLY          = 0xA6;

    /* Power */
    static constexpr uint8_t SD_POWER_GPREGRET_GET      = 0x36;
    static constexpr uint8_t SD_POWER_DCDC_MODE_SET     = 0x37;
    static constexpr uint8_t SD_POWER_POF_ENABLE        = 0x39;

    /* PPI */
    static constexpr uint8_t SD_PPI_CHANNEL_ASSIGN      = 0x28;
    static constexpr uint8_t SD_PPI_CHANNEL_ENABLE_SET   = 0x29;

    /* NVIC */
    static constexpr uint8_t SD_NVIC_ENABLEIRQ          = 0x10;
}


/* =========================================================================
 * BLE Event Types
 * ========================================================================= */

enum class BleEventType : uint16_t {
    CONNECTED            = 0x10,
    DISCONNECTED         = 0x11,
    ADV_TIMEOUT          = 0x12,
    CONN_PARAM_UPDATE    = 0x13,
    GATTS_WRITE          = 0x50,
    GATTS_HVC            = 0x55,
    L2CAP_RX             = 0x70,
    SYS_EVT_FLASH_OP     = 0x02,  ///< Flash operation complete
};

/* =========================================================================
 * BLE Connection Handle
 * ========================================================================= */

static constexpr uint16_t BLE_CONN_HANDLE_INVALID = 0xFFFF;


/* =========================================================================
 * SoftDevice Interface
 * =========================================================================
 * Abstract interface representing the Nordic SoftDevice BLE stack.
 * In simulation, this is backed by SimSoftDevice.
 * On hardware, these map to actual SVC calls.
 */

class ISoftDevice {
public:
    virtual ~ISoftDevice() = default;

    /* ─── BLE Enable & Events ─────────────────────────────────── */

    /** Enable the BLE stack (sd_ble_enable). */
    virtual uint32_t bleEnable() = 0;

    /** Get next BLE event (sd_ble_evt_get). Returns event type, 0 if none. */
    virtual bool getEvent(BleEventType& type, uint16_t& connHandle) = 0;

    /* ─── GAP Advertising ─────────────────────────────────────── */

    /** Set advertising data (sd_ble_gap_adv_data_set). */
    virtual uint32_t setAdvData(const uint8_t* data, uint8_t len,
                                const uint8_t* srData = nullptr, uint8_t srLen = 0) = 0;

    /** Start advertising (sd_ble_gap_adv_start). */
    virtual uint32_t startAdvertising() = 0;

    /** Stop advertising (sd_ble_gap_adv_stop). */
    virtual uint32_t stopAdvertising() = 0;

    /** Set TX power (sd_ble_gap_tx_power_set). */
    virtual uint32_t setTxPower(int8_t txPower) = 0;

    /* ─── GAP Connection ──────────────────────────────────────── */

    /** Disconnect (sd_ble_gap_disconnect). */
    virtual uint32_t disconnect(uint16_t connHandle, uint8_t reason) = 0;

    /** Set connection parameters (sd_ble_gap_ppcp_set). */
    virtual uint32_t setPPCP(uint16_t minInterval, uint16_t maxInterval,
                             uint16_t slaveLatency, uint16_t supTimeout) = 0;

    /* ─── GATTS ───────────────────────────────────────────────── */

    /** Add a GATT service. Returns service handle. */
    virtual uint32_t addService(uint8_t type, const uint8_t* uuid128,
                                uint16_t& handle) = 0;

    /** Add a characteristic. Returns char handles. */
    virtual uint32_t addCharacteristic(uint16_t serviceHandle,
                                       const uint8_t* uuid128,
                                       uint8_t properties,
                                       uint16_t& valueHandle,
                                       uint16_t& cccdHandle) = 0;

    /** Send a notification/indication (sd_ble_gatts_hvx). */
    virtual uint32_t hvx(uint16_t connHandle, uint16_t valueHandle,
                         const uint8_t* data, uint16_t len) = 0;

    /* ─── L2CAP ───────────────────────────────────────────────── */

    /** Register an L2CAP CID. */
    virtual uint32_t l2capRegister(uint16_t cid) = 0;

    /** Send data on L2CAP channel (sd_ble_l2cap_tx). */
    virtual uint32_t l2capTx(uint16_t connHandle, uint16_t cid,
                             const uint8_t* data, uint16_t len) = 0;

    /* ─── Power ───────────────────────────────────────────────── */

    /** Read GPREGRET register (sd_power_gpregret_get). */
    virtual uint32_t gpregretGet(uint32_t& value) = 0;

    /** Set DC-DC converter mode (sd_power_dcdc_mode_set). */
    virtual uint32_t dcdcModeSet(uint8_t mode) = 0;

    /* ─── Flash (via SoftDevice) ──────────────────────────────── */

    /** Write flash page (sd_flash_write). Async — triggers SWI2 callback. */
    virtual uint32_t flashWrite(uint32_t* dst, const uint32_t* src, uint32_t wordCount) = 0;

    /** Erase flash page (sd_flash_page_erase). Async — triggers SWI2 callback. */
    virtual uint32_t flashPageErase(uint32_t pageNumber) = 0;

    /* ─── Connection status ───────────────────────────────────── */

    /** Check if a BLE connection is active. */
    virtual bool isConnected() const = 0;

    /** Get current connection handle. */
    virtual uint16_t connHandle() const = 0;
};


/* =========================================================================
 * UART Interface (nRF51822 has 1 UART)
 * =========================================================================
 * Same concept as STM32 IUart — byte-level TX/RX.
 * Connected to STM32 on the BLE dashboard board.
 */

class INrfUart {
public:
    virtual ~INrfUart() = default;

    /** Send one byte (writes to UART0.TXD). */
    virtual void sendByte(uint8_t byte) = 0;

    /** Receive one byte (reads from UART0.RXD). Returns 0 if none. */
    virtual uint8_t receiveByte() = 0;

    /** Check if TX register is ready. */
    virtual bool isTxReady() const = 0;

    /** Check if RX data is available. */
    virtual bool isRxReady() const = 0;

    /** Enable UART. */
    virtual void enable() = 0;

    /** Disable UART. */
    virtual void disable() = 0;
};


/* =========================================================================
 * GPIO Interface (nRF51822 uses P0.00–P0.31)
 * ========================================================================= */

class INrfGpio {
public:
    virtual ~INrfGpio() = default;

    /** Set a pin high. */
    virtual void setPin(uint8_t pin) = 0;

    /** Clear a pin low. */
    virtual void clearPin(uint8_t pin) = 0;

    /** Read a pin state. */
    virtual bool readPin(uint8_t pin) const = 0;

    /** Configure pin as output. */
    virtual void configureOutput(uint8_t pin) = 0;

    /** Configure pin as input (with optional pull). */
    virtual void configureInput(uint8_t pin, uint8_t pull = 0) = 0;
};


/* =========================================================================
 * Timer Interface (nRF51822 TIMER peripheral)
 * ========================================================================= */

class INrfTimer {
public:
    virtual ~INrfTimer() = default;

    /** Start the timer. */
    virtual void start() = 0;

    /** Stop the timer. */
    virtual void stop() = 0;

    /** Get current counter value. */
    virtual uint32_t getCounter() const = 0;

    /** Set compare value for a CC register. */
    virtual void setCompare(uint8_t cc, uint32_t value) = 0;

    /** Clear the counter. */
    virtual void clear() = 0;
};


/* =========================================================================
 * Watchdog Interface
 * ========================================================================= */

class INrfWdt {
public:
    virtual ~INrfWdt() = default;

    /** Start the watchdog. */
    virtual void start(uint32_t timeoutMs) = 0;

    /** Reload/feed the watchdog. */
    virtual void feed() = 0;
};


/* =========================================================================
 * Persistent Storage Manager (PSM)
 * =========================================================================
 * Abstraction for the Nordic SDK's flash-based persistent storage.
 * Used for storing BLE bonds, MiIO tokens, and configuration.
 *
 * From firmware strings:
 *   "psm callback"
 *   "[D]: update succ. len = %d"
 *   "[D]: load succ, len = %d"
 *   "[D]: store succ. len = %d"
 *   "[D]: clear succ. len = %d"
 */

enum class PsmOpcode : uint8_t {
    LOAD   = 0x01,
    STORE  = 0x02,
    UPDATE = 0x03,
    CLEAR  = 0x04,
};

enum class PsmResult : uint8_t {
    SUCCESS = 0x00,
    ERROR   = 0x01,
};

using PsmCallback = std::function<void(PsmOpcode opcode, PsmResult result, uint16_t len)>;

class IPsm {
public:
    virtual ~IPsm() = default;

    /** Store data to a named block. */
    virtual bool store(uint16_t blockId, const uint8_t* data, uint16_t len) = 0;

    /** Load data from a named block. */
    virtual bool load(uint16_t blockId, uint8_t* data, uint16_t maxLen, uint16_t& actualLen) = 0;

    /** Update data in a named block. */
    virtual bool update(uint16_t blockId, const uint8_t* data, uint16_t len) = 0;

    /** Clear a named block. */
    virtual bool clear(uint16_t blockId) = 0;

    /** Register flash write callback. */
    virtual void registerCallback(PsmCallback cb) = 0;

    /** Register flash write function (corresponds to "flash write func not setting" check). */
    virtual void registerFlashWriteFunc() = 0;

    /** Check if flash write function is registered. */
    virtual bool isFlashWriteRegistered() const = 0;
};


/* =========================================================================
 * Complete nRF51822 Board HAL
 * =========================================================================
 * Bundles all peripheral interfaces for the nRF51822 application.
 */

struct Nrf51Hal {
    ISoftDevice*  softdevice;    ///< Nordic SoftDevice (BLE stack + system calls)
    INrfUart*     uart;          ///< UART0: STM32 communication
    INrfGpio*     gpio;          ///< GPIO P0
    INrfTimer*    timer1;        ///< TIMER1: Application timing
    INrfWdt*      wdt;           ///< Watchdog timer
    IPsm*         psm;           ///< Persistent Storage Manager
};


/* =========================================================================
 * nRF51822 Pin Assignments
 * =========================================================================
 * Estimated from Nordic reference design and firmware UART register analysis.
 */

namespace pin {
    static constexpr uint8_t UART_TX      = 8;   ///< P0.08 → STM32 PA10 (USART1_RX)
    static constexpr uint8_t UART_RX      = 9;   ///< P0.09 ← STM32 PA9  (USART1_TX)
    static constexpr uint8_t UART_CTS     = 10;  ///< P0.10 (may not be connected)
    static constexpr uint8_t UART_RTS     = 11;  ///< P0.11 (may not be connected)
    static constexpr uint8_t RESET        = 21;  ///< P0.21 Reset input
}


} // namespace nrf51
} // namespace ninebot

#endif // NINEBOT_NRF51_HAL_H
