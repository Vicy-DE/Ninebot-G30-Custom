/**
 * @file nrf51_firmware.h
 * @brief Decompiled nRF51822 BLE firmware — main interface header.
 *
 * Reconstructed from BLE_1.1.7.bin disassembly analysis.
 * Target: nRF51822-QFAA (256KB Flash, 16KB SRAM, 16MHz Cortex-M0)
 *         with Nordic SoftDevice S110 v8.0
 *
 * The nRF51822 firmware handles:
 *   1. BLE advertising and connection management
 *   2. Xiaomi MiIO authentication (token, auth, cloud bind)
 *   3. BLE GATT service for Nordic UART Service (NUS)
 *   4. L2CAP channel management for MiIO OTA data transfer
 *   5. Ninebot protocol parsing and relay to/from STM32
 *   6. Persistent storage via PSM (bonds, tokens, config)
 *   7. OTA firmware update support via MiIO flash registration
 *   8. Watchdog management
 *
 * Architecture:
 * @code
 *   ┌─────────────────────────────────────────────────────────┐
 *   │              nRF51822 BLE SoC                           │
 *   │                                                         │
 *   │  SoftDevice S110 v8.0 (96 KB):                        │
 *   │    ├── BLE 4.0 radio management                        │
 *   │    ├── GAP / GATT / L2CAP                              │
 *   │    └── SVC interface for application                    │
 *   │                                                         │
 *   │  Application (0x00018000+, ~34 KB):                    │
 *   │    ├── UART0 ↔ STM32 (Ninebot protocol relay)         │
 *   │    ├── BLE GATT server (NUS + MiIO services)           │
 *   │    ├── MiIO authentication state machine               │
 *   │    ├── L2CAP data channels (14 SVC calls)              │
 *   │    ├── PSM persistent storage (tokens, bonds)          │
 *   │    └── WDT watchdog feed                               │
 *   └─────────────────────────────────────────────────────────┘
 * @endcode
 *
 * Key firmware analysis findings (BLE_1.1.7.bin):
 *   - 282 functions, 554 strings, 58 SVC calls
 *   - Reset handler @ 0x00018155
 *   - Active IRQs: UART0, GPIOTE, TIMER1, WDT, RTC1, SWI0, SWI2
 *   - No direct sd_flash_write/sd_flash_page_erase SVCs
 *   - Flash operations through PSM indirect function pointers
 *   - GPREGRET read (not written) — no programmatic DFU trigger
 *   - Device names: "Scooter_G30_SAT", "NBScooter0001"
 *
 * @see nrf51822_analysis_BLE_1.1.7.txt
 * @see nrf51822-reprogramming.md
 */

#ifndef NINEBOT_NRF51_FIRMWARE_H
#define NINEBOT_NRF51_FIRMWARE_H

#include "nrf51_hal.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <array>
#include <vector>

namespace ninebot {
namespace nrf51 {

/* =========================================================================
 * BLE Service UUIDs
 * =========================================================================
 * Nordic UART Service (NUS) — primary BLE service.
 */

/** Nordic UART Service base UUID. */
static constexpr uint8_t NUS_UUID_BASE[16] = {
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x00, 0x00, 0x40, 0x6E
};

static constexpr uint16_t NUS_UUID_SERVICE = 0x0001;  ///< 6E400001-...
static constexpr uint16_t NUS_UUID_TX_CHAR = 0x0002;  ///< 6E400002-... (Phone→Scooter)
static constexpr uint16_t NUS_UUID_RX_CHAR = 0x0003;  ///< 6E400003-... (Scooter→Phone)


/* =========================================================================
 * MiIO Authentication
 * =========================================================================
 * Xiaomi MiIO BLE protocol state machine.
 * Reconstructed from firmware strings and function call patterns.
 */

/**
 * MiIO authentication state machine states.
 * From firmware string: "Enter state machine"
 */
enum class MiioState : uint8_t {
    IDLE              = 0x00,   ///< Not started
    WAIT_AUTH         = 0x01,   ///< "On Auth Written"
    AUTH_DECRYPT      = 0x02,   ///< "decrypted auth, %x"
    WAIT_TOKEN        = 0x03,   ///< "On Token Written"
    LOGIN_CONFIRM     = 0x04,   ///< "login cfm handler, token: %02x %02x"
    WAIT_CLOUD_BIND   = 0x05,   ///< "cloud bind succ" / "cloud bind fail"
    WAIT_APP_BOND     = 0x06,   ///< "app bond succ" / "app bond fail"
    WAIT_SN           = 0x07,   ///< "mi_service, waiting SN"
    SN_ARRIVED        = 0x08,   ///< "SN Arrived" / "sn: %02x"
    REGISTERED        = 0x09,   ///< "Register succ, new token, encrypt sn, beaconkey."
    FLASH_REGISTERED  = 0x0A,   ///< "miio ble flash register succ"
    ERROR             = 0xFF,   ///< Error state
};


/**
 * MiIO token storage (16 bytes).
 * From firmware: "retrive token, %02x %02x"
 */
struct MiioToken {
    uint8_t data[16] = {};
    bool    valid = false;
};


/**
 * MiIO device registration info.
 * From firmware: "Register succ, new token, encrypt sn, beaconkey."
 */
struct MiioRegistration {
    MiioToken   token;           ///< Current session token
    uint8_t     beaconKey[16] = {};  ///< Beacon encryption key
    char        serialNumber[20] = {};
    bool        cloudBound = false;
    bool        appBonded = false;
    bool        flashRegistered = false;
};


/* =========================================================================
 * Ninebot Protocol Parser (nRF51822 side)
 * =========================================================================
 * Same 5A A5 protocol as STM32, but simpler: relay-only for most packets.
 * Implemented from firmware analysis showing CMP #0x5A / CMP #0xA5 patterns.
 *
 * @see nrf51822_analysis: 0x1854E CMP R0,#0x5A; 0x18554 CMP R0,#0xA5
 */

/** Protocol frame constants (same as STM32 side). */
static constexpr uint8_t NB_HEADER_1     = 0x5A;
static constexpr uint8_t NB_HEADER_2     = 0xA5;
static constexpr uint8_t NB_MAX_PAYLOAD  = 242;
static constexpr uint8_t NB_ADDR_ESC     = 0x20;
static constexpr uint8_t NB_ADDR_BLE     = 0x21;
static constexpr uint8_t NB_ADDR_BMS     = 0x22;
static constexpr uint8_t NB_ADDR_APP     = 0x3E;

/** Parsed Ninebot protocol packet (nRF51822 side). */
struct NbPacket {
    uint8_t  length;
    uint8_t  source;
    uint8_t  destination;
    uint8_t  command;
    uint8_t  argument;
    uint8_t  payload[NB_MAX_PAYLOAD];
    uint8_t  payloadLength;
    uint16_t checksum;
};

/** Ninebot protocol parser state for the UART side. */
struct NbParserState {
    bool     gotHeader1      = false;
    bool     receiving       = false;
    uint8_t  rxIndex         = 0;
    uint8_t  expectedLength  = 0;
    uint16_t runningChecksum = 0;
    uint8_t  rxBuffer[256]   = {};

    void reset() {
        gotHeader1 = false;
        receiving = false;
        rxIndex = 0;
        expectedLength = 0;
        runningChecksum = 0;
    }
};


/* =========================================================================
 * Firmware Configuration
 * ========================================================================= */

/** Device identity strings found in firmware. */
static constexpr const char* DEVICE_MODEL   = "Scooter_G30_SAT";
static constexpr const char* DEVICE_NAME    = "NBScooter0001";
static constexpr const char* ALT_MODEL_NAME = "N3M-Ninebot-Mini0001";

/** Firmware version (BLE_1.1.7). */
static constexpr uint16_t FW_VERSION = 0x0117;


/* =========================================================================
 * nRF51822 Firmware Class
 * =========================================================================
 * Complete decompiled firmware for the nRF51822 BLE co-processor.
 *
 * This is the firmware that runs on the nRF51822 chip inside the
 * Ninebot G30 Max BLE dashboard. It handles all Bluetooth communication
 * and acts as a bridge between the phone app and the STM32 MCU.
 */

class Nrf51Firmware {
public:
    explicit Nrf51Firmware(Nrf51Hal& hal);

    /* ─── Lifecycle ───────────────────────────────────────────── */

    /** Initialize the firmware (reset handler equivalent). */
    void init();

    /** Execute one main loop iteration. */
    void mainLoopIteration();

    /* ─── Interrupt Handlers ──────────────────────────────────── */

    /** UART0 RX interrupt handler (from STM32). ISR @ 0x00019E71. */
    void uart0RxIsr(uint8_t byte);

    /** GPIOTE interrupt handler. ISR @ 0x00018CE9. */
    void gpioteIsr();

    /** TIMER1 interrupt handler. ISR @ 0x00019D1D. */
    void timer1Isr();

    /** WDT interrupt handler. ISR @ 0x0001A1B5. */
    void wdtIsr();

    /** RTC1 interrupt handler. ISR @ 0x00019C3D. */
    void rtc1Isr();

    /** SWI0 interrupt handler. ISR @ 0x00019C5D. */
    void swi0Isr();

    /** SWI2 interrupt handler (SoftDevice system events). ISR @ 0x00019CB1. */
    void swi2Isr();

    /* ─── BLE Event Processing ────────────────────────────────── */

    /** Process a BLE connection event. */
    void onBleConnected(uint16_t connHandle);

    /** Process a BLE disconnection event. */
    void onBleDisconnected(uint16_t connHandle);

    /** Process a GATT write event (from phone app). */
    void onGattsWrite(uint16_t handle, const uint8_t* data, uint16_t len);

    /** Process an L2CAP RX event. */
    void onL2capRx(uint16_t cid, const uint8_t* data, uint16_t len);

    /* ─── Status Accessors ────────────────────────────────────── */

    /** Get the current MiIO state. */
    MiioState miioState() const { return miioState_; }

    /** Get MiIO registration info. */
    const MiioRegistration& miioReg() const { return miioReg_; }

    /** Check if BLE is connected. */
    bool isBleConnected() const { return bleConnected_; }

    /** Get the current BLE connection handle. */
    uint16_t bleConnHandle() const { return connHandle_; }

    /** Check if MiIO flash is registered (ready for OTA). */
    bool isMiioFlashRegistered() const { return miioReg_.flashRegistered; }

    /** Get total bytes relayed UART→BLE. */
    uint32_t uartToBleBytes() const { return uartToBleBytes_; }

    /** Get total bytes relayed BLE→UART. */
    uint32_t bleToUartBytes() const { return bleToUartBytes_; }

    /** Get UART parser state (for testing). */
    const NbParserState& uartParserState() const { return uartParser_; }

    /** Get BLE parser state (for testing). */
    const NbParserState& bleParserState() const { return bleParser_; }

    /** Get the TX queue for UART (packets to send to STM32). */
    const std::vector<std::vector<uint8_t>>& uartTxQueue() const { return uartTxQueue_; }

    /** Get the TX queue for BLE (packets to send to phone app). */
    const std::vector<std::vector<uint8_t>>& bleTxQueue() const { return bleTxQueue_; }

    /** Inject raw bytes from BLE side (simulates GATTS write / NUS RX). */
    void feedBleBytes(const uint8_t* data, size_t len);

    /** Drain UART TX queue (send all queued packets to STM32). */
    size_t drainUartTx();

    /** Drain BLE TX queue (send all queued packets to phone). */
    size_t drainBleTx();

    /* ─── PSM Accessors ───────────────────────────────────────── */

    /** Get stored token (from PSM). */
    const MiioToken& storedToken() const { return storedToken_; }

private:
    /* ─── Initialization sub-routines ─────────────────────────── */
    void initSoftDevice();
    void initBleServices();
    void initMiio();
    void initUart();
    void initWatchdog();
    void loadPsmData();

    /* ─── Protocol Handling ───────────────────────────────────── */

    /** Parse one byte from UART (Ninebot protocol). */
    void parseUartByte(uint8_t byte);

    /** Parse one byte from BLE (Ninebot protocol). */
    void parseBleByte(uint8_t byte);

    /** Handle a complete packet from UART (STM32). */
    void handleUartPacket(const NbPacket& pkt);

    /** Handle a complete packet from BLE (phone app). */
    void handleBlePacket(const NbPacket& pkt);

    /** Build and enqueue a Ninebot protocol packet. */
    void enqueuePacket(std::vector<std::vector<uint8_t>>& queue,
                       uint8_t src, uint8_t dst, uint8_t cmd, uint8_t arg,
                       const uint8_t* payload, uint8_t payloadLen);

public:
    /** Calculate Ninebot protocol checksum (public for test cross-validation). */
    static uint16_t calcChecksum(const uint8_t* data, size_t length);

private:
    /** Verify and dispatch a parsed packet. */
    void verifyAndDispatch(NbParserState& parser,
                           void (Nrf51Firmware::*handler)(const NbPacket&));

    /* ─── MiIO State Machine ──────────────────────────────────── */
    void miioAdvance(MiioState nextState);
    void miioHandleAuth(const uint8_t* data, uint16_t len);
    void miioHandleToken(const uint8_t* data, uint16_t len);
    void miioHandleLoginConfirm();
    void miioHandleCloudBind(bool success);
    void miioHandleAppBond(bool success);
    void miioHandleSN(const uint8_t* data, uint16_t len);
    void miioRegister();
    void miioFlashRegister();

    /* ─── BLE Operations ──────────────────────────────────────── */
    void startAdvertising();
    void sendBleNotification(const uint8_t* data, uint16_t len);

    /* ─── PSM Operations ──────────────────────────────────────── */
    void psmCallback(PsmOpcode opcode, PsmResult result, uint16_t len);
    void saveToken();
    void loadToken();

    /* ─── Hardware ────────────────────────────────────────────── */
    Nrf51Hal&           hal_;

    /* ─── BLE State ───────────────────────────────────────────── */
    bool                bleConnected_    = false;
    uint16_t            connHandle_      = BLE_CONN_HANDLE_INVALID;
    uint16_t            nusServiceHandle_ = 0;
    uint16_t            nusTxHandle_     = 0;  ///< TX char value handle
    uint16_t            nusRxHandle_     = 0;  ///< RX char value handle
    uint16_t            nusTxCccdHandle_ = 0;  ///< TX char CCCD handle
    bool                notifyEnabled_   = false;
    bool                advertising_     = false;

    /* ─── MiIO State ──────────────────────────────────────────── */
    MiioState           miioState_       = MiioState::IDLE;
    MiioRegistration    miioReg_;
    MiioToken           storedToken_;

    /* ─── Protocol State ──────────────────────────────────────── */
    NbParserState       uartParser_;     ///< UART RX parser (from STM32)
    NbParserState       bleParser_;      ///< BLE RX parser  (from phone app)

    std::vector<std::vector<uint8_t>> uartTxQueue_;  ///< Packets to send to STM32
    std::vector<std::vector<uint8_t>> bleTxQueue_;   ///< Packets to send to phone

    /* ─── Statistics ──────────────────────────────────────────── */
    uint32_t            uartToBleBytes_  = 0;
    uint32_t            bleToUartBytes_  = 0;
    uint32_t            loopCount_       = 0;
};


} // namespace nrf51
} // namespace ninebot

#endif // NINEBOT_NRF51_FIRMWARE_H
