/**
 * @file nrf51_main.cpp
 * @brief nRF51822 BLE firmware — initialization, main loop, and protocol handling.
 *
 * Reconstructed from BLE_1.1.7.bin Cortex-M0 disassembly analysis.
 *
 * The nRF51822 is the Bluetooth co-processor on the Ninebot G30 Max
 * BLE dashboard. It handles all BLE communication including:
 *   - Nordic UART Service (NUS) for Ninebot protocol relay
 *   - Xiaomi MiIO authentication and cloud binding
 *   - L2CAP channel management for OTA firmware data
 *   - Persistent storage of BLE bonds and MiIO tokens
 *
 * Main loop (event-driven):
 *   1. Poll SoftDevice for BLE events
 *   2. Process UART data from STM32 (protocol relay)
 *   3. Process BLE data from phone app (protocol relay)
 *   4. Drive MiIO state machine
 *   5. Feed watchdog
 *   6. Drain TX queues
 *
 * @see nrf51822_analysis_BLE_1.1.7.txt — 282 functions, 58 SVCs
 * @see Reset handler @ 0x00018155
 * @see UART0 ISR    @ 0x00019E71
 * @see SWI2 ISR     @ 0x00019CB1 (SoftDevice system events)
 */

#include "nrf51_firmware.h"

namespace ninebot {
namespace nrf51 {

/* =========================================================================
 * Constructor
 * ========================================================================= */

Nrf51Firmware::Nrf51Firmware(Nrf51Hal& hal)
    : hal_(hal)
{
}

/* =========================================================================
 * Initialization — Reset Handler Equivalent
 * =========================================================================
 * Reproduces the startup sequence from the reset handler @ 0x00018155.
 *
 * 1. Check GPREGRET (DFU trigger check) — reads but never writes
 * 2. Initialize SoftDevice (sd_ble_enable)
 * 3. Set up BLE services (NUS, MiIO)
 * 4. Configure UART for STM32 communication
 * 5. Load persistent storage (tokens, bonds)
 * 6. Start BLE advertising
 * 7. Start watchdog
 */

void Nrf51Firmware::init()
{
    /* Reset all state */
    bleConnected_ = false;
    connHandle_ = BLE_CONN_HANDLE_INVALID;
    miioState_ = MiioState::IDLE;
    miioReg_ = {};
    uartParser_.reset();
    bleParser_.reset();
    uartTxQueue_.clear();
    bleTxQueue_.clear();
    uartToBleBytes_ = 0;
    bleToUartBytes_ = 0;
    loopCount_ = 0;
    advertising_ = false;
    notifyEnabled_ = false;

    /* Step 1: Check GPREGRET — firmware reads this on boot
     * sd_power_gpregret_get @ 0x0001AABC and 0x0001F1A6
     * Checks for DFU magic value but never triggers DFU */
    if (hal_.softdevice) {
        uint32_t gpregret = 0;
        hal_.softdevice->gpregretGet(gpregret);
        /* CMP r0, #2 → just error checking, not DFU trigger */
    }

    /* Step 2: SoftDevice initialization */
    initSoftDevice();

    /* Step 3: BLE services */
    initBleServices();

    /* Step 4: MiIO init */
    initMiio();

    /* Step 5: UART init */
    initUart();

    /* Step 6: Load PSM data (tokens, bonds) */
    loadPsmData();

    /* Step 7: Start advertising */
    startAdvertising();

    /* Step 8: Watchdog */
    initWatchdog();
}

/* =========================================================================
 * SoftDevice Initialization
 * =========================================================================
 * SVC calls found: sd_ble_enable (0x60), sd_ble_uuid_vs_add (0x63),
 * sd_ble_opt_set (0x65), sd_power_dcdc_mode_set (0x37)
 */

void Nrf51Firmware::initSoftDevice()
{
    if (!hal_.softdevice) return;

    /* Enable BLE stack — SVC #96 (0x60) @ 0x0001C896 */
    hal_.softdevice->bleEnable();

    /* Enable DC-DC converter for power efficiency
     * SVC #55 (0x37) @ 0x0001AAF4 */
    hal_.softdevice->dcdcModeSet(1);
}

/* =========================================================================
 * BLE Service Setup
 * =========================================================================
 * Sets up the Nordic UART Service (NUS) which the Ninebot app uses
 * for bidirectional communication.
 *
 * SVC calls: sd_ble_gatts_service_add (0x90), sd_ble_gatts_char_add (0x92)
 * sd_ble_gap_adv_data_set (0x72), sd_ble_gap_ppcp_set (0x7A)
 */

void Nrf51Firmware::initBleServices()
{
    if (!hal_.softdevice) return;

    /* Add NUS service — custom 128-bit UUID */
    hal_.softdevice->addService(
        1, /* primary service */
        NUS_UUID_BASE,
        nusServiceHandle_);

    /* Add TX characteristic (phone → scooter) */
    uint8_t txProps = 0x08;  /* Write without response */
    hal_.softdevice->addCharacteristic(
        nusServiceHandle_, NUS_UUID_BASE,
        txProps, nusTxHandle_, nusTxCccdHandle_);

    /* Add RX characteristic (scooter → phone) — Notify */
    uint8_t rxProps = 0x10;  /* Notify */
    uint16_t rxCccd = 0;
    hal_.softdevice->addCharacteristic(
        nusServiceHandle_, NUS_UUID_BASE,
        rxProps, nusRxHandle_, rxCccd);

    /* Set connection parameters
     * SVC #122 (0x7A) @ 0x0001B076 */
    hal_.softdevice->setPPCP(
        16,    /* min interval: 20ms (16 * 1.25ms) */
        32,    /* max interval: 40ms */
        0,     /* slave latency: 0 */
        400    /* supervision timeout: 4s */
    );

    /* Set TX power */
    hal_.softdevice->setTxPower(0);  /* 0 dBm */
}

/* =========================================================================
 * MiIO Initialization
 * =========================================================================
 * From firmware string: "[E]: Mi Serivce Init fail, error code=%d"
 */

void Nrf51Firmware::initMiio()
{
    miioState_ = MiioState::IDLE;
    miioReg_ = {};

    /* Copy device model to registration */
    std::strncpy(miioReg_.serialNumber, DEVICE_NAME,
                 sizeof(miioReg_.serialNumber) - 1);
}

/* =========================================================================
 * UART Initialization
 * =========================================================================
 * From firmware analysis: UART0 register references at 0x0001FDC8 (ENABLE),
 * 0x0001FDD4 (TASKS_STARTRX), 0x0001FDDC, 0x0001FDF8, 0x0001FE04
 *
 * Pin config: P0.08 = TX, P0.09 = RX, 115200 baud
 */

void Nrf51Firmware::initUart()
{
    if (!hal_.uart) return;

    hal_.uart->enable();

    /* Configure UART pins via GPIO */
    if (hal_.gpio) {
        hal_.gpio->configureOutput(pin::UART_TX);
        hal_.gpio->configureInput(pin::UART_RX, 0);
    }
}

/* =========================================================================
 * Watchdog Initialization
 * =========================================================================
 * WDT ISR @ 0x0001A1B5
 */

void Nrf51Firmware::initWatchdog()
{
    if (hal_.wdt) {
        hal_.wdt->start(5000);  /* 5 second timeout */
    }
}

/* =========================================================================
 * PSM Data Loading
 * =========================================================================
 * From firmware strings:
 *   "psm retrive succ, "
 *   "retrive token, %02x %02x"
 *   "[E]: flash read error"
 */

void Nrf51Firmware::loadPsmData()
{
    if (!hal_.psm) return;

    /* Register PSM callback */
    hal_.psm->registerCallback(
        [this](PsmOpcode op, PsmResult res, uint16_t len) {
            psmCallback(op, res, len);
        });

    /* Register flash write function
     * Guards against "[W]: flash write func not setting" */
    hal_.psm->registerFlashWriteFunc();

    /* Load stored token */
    loadToken();
}

/* =========================================================================
 * Main Loop
 * =========================================================================
 * Event-driven processing. Called repeatedly.
 */

void Nrf51Firmware::mainLoopIteration()
{
    loopCount_++;

    /* 1. Poll SoftDevice for BLE events */
    if (hal_.softdevice) {
        BleEventType evtType;
        uint16_t evtConnHandle;
        while (hal_.softdevice->getEvent(evtType, evtConnHandle)) {
            switch (evtType) {
                case BleEventType::CONNECTED:
                    onBleConnected(evtConnHandle);
                    break;
                case BleEventType::DISCONNECTED:
                    onBleDisconnected(evtConnHandle);
                    break;
                case BleEventType::SYS_EVT_FLASH_OP:
                    /* Flash operation complete — handled via SWI2 */
                    swi2Isr();
                    break;
                default:
                    break;
            }
        }
    }

    /* 2. Drain UART TX queue (send queued packets to STM32) */
    drainUartTx();

    /* 3. Drain BLE TX queue (send notifications to phone) */
    drainBleTx();

    /* 4. Feed watchdog */
    if (hal_.wdt) {
        hal_.wdt->feed();
    }
}

/* =========================================================================
 * Interrupt Handlers
 * ========================================================================= */

/**
 * UART0 RX ISR — receives bytes from STM32.
 *
 * ISR @ 0x00019E71
 * Each byte is fed to the Ninebot protocol parser.
 */
void Nrf51Firmware::uart0RxIsr(uint8_t byte)
{
    parseUartByte(byte);
}

/**
 * GPIOTE ISR — GPIO events.
 * ISR @ 0x00018CE9
 */
void Nrf51Firmware::gpioteIsr()
{
    /* GPIO event — may trigger on external signal */
}

/**
 * TIMER1 ISR — application timer.
 * ISR @ 0x00019D1D
 */
void Nrf51Firmware::timer1Isr()
{
    /* Timer event */
}

/**
 * WDT ISR — watchdog timeout.
 * ISR @ 0x0001A1B5
 */
void Nrf51Firmware::wdtIsr()
{
    /* Watchdog timeout — system should reset */
}

/**
 * RTC1 ISR — real-time counter.
 * ISR @ 0x00019C3D
 */
void Nrf51Firmware::rtc1Isr()
{
    /* RTC1 event — BLE timing */
}

/**
 * SWI0 ISR — software interrupt 0.
 * ISR @ 0x00019C5D
 */
void Nrf51Firmware::swi0Isr()
{
    /* Software interrupt 0 */
}

/**
 * SWI2 ISR — SoftDevice system events (flash complete).
 * ISR @ 0x00019CB1
 *
 * This is the callback path for sd_flash_write/sd_flash_page_erase.
 * The PSM uses this to know when flash operations are done.
 */
void Nrf51Firmware::swi2Isr()
{
    /* Flash operation complete notification.
     * PSM callback was already registered in loadPsmData(). */
}


/* =========================================================================
 * BLE Event Handlers
 * =========================================================================
 * From firmware strings: "On Connected", "On Token Written", etc.
 */

void Nrf51Firmware::onBleConnected(uint16_t connHandle)
{
    bleConnected_ = true;
    connHandle_ = connHandle;

    /* If we were advertising, stop now */
    advertising_ = false;

    /* Start MiIO auth flow */
    miioAdvance(MiioState::WAIT_AUTH);
}

void Nrf51Firmware::onBleDisconnected(uint16_t /*connHandle*/)
{
    bleConnected_ = false;
    connHandle_ = BLE_CONN_HANDLE_INVALID;
    notifyEnabled_ = false;

    /* Reset MiIO state */
    miioState_ = MiioState::IDLE;

    /* Restart advertising */
    startAdvertising();
}

/**
 * Handle GATT write from phone app.
 *
 * Two paths:
 *   1. NUS TX characteristic — Ninebot protocol data
 *   2. MiIO characteristics — authentication flow
 */
void Nrf51Firmware::onGattsWrite(uint16_t handle, const uint8_t* data, uint16_t len)
{
    if (handle == nusTxHandle_) {
        /* Ninebot protocol data from phone app */
        for (uint16_t i = 0; i < len; i++) {
            parseBleByte(data[i]);
        }
    } else if (handle == nusTxCccdHandle_) {
        /* CCCD write — enable/disable notifications */
        if (len >= 2) {
            notifyEnabled_ = (data[0] | (data[1] << 8)) != 0;
        }
    } else {
        /* MiIO characteristic writes */
        if (miioState_ == MiioState::WAIT_AUTH) {
            miioHandleAuth(data, len);
        } else if (miioState_ == MiioState::WAIT_TOKEN) {
            miioHandleToken(data, len);
        } else if (miioState_ == MiioState::WAIT_SN) {
            miioHandleSN(data, len);
        }
    }
}

/**
 * Handle L2CAP RX event.
 * From firmware analysis: 14 L2CAP SVC calls for MiIO OTA data.
 */
void Nrf51Firmware::onL2capRx(uint16_t /*cid*/, const uint8_t* data, uint16_t len)
{
    /* L2CAP data — MiIO OTA firmware data transfer
     * The MiIO SDK processes this internally through the PSM layer */
    if (miioReg_.flashRegistered && hal_.psm && len > 0) {
        /* Write received firmware data to flash via PSM */
        hal_.psm->store(0x0100, data, len);
    }
}


/* =========================================================================
 * Ninebot Protocol Parsing — UART (from STM32)
 * =========================================================================
 * Same state machine as STM32 side.
 * From firmware: CMP R0, #0x5A at 0x0001854E, 0x1A030
 *                CMP R0, #0xA5 at 0x18554, 0x1A034
 */

void Nrf51Firmware::parseUartByte(uint8_t byte)
{
    if (uartParser_.receiving) {
        uartParser_.rxBuffer[uartParser_.rxIndex] = byte;

        /* First byte = LEN */
        if (uartParser_.rxIndex == 0) {
            uartParser_.expectedLength = byte + 1;  /* +1 for header overhead */
            if (uartParser_.expectedLength > 249) {
                uartParser_.reset();
                return;
            }
        }

        uartParser_.rxIndex++;

        /* Packet complete? */
        if (uartParser_.rxIndex == uartParser_.expectedLength) {
            verifyAndDispatch(uartParser_,
                              &Nrf51Firmware::handleUartPacket);
            uartParser_.reset();
        } else {
            uartParser_.runningChecksum += byte;
        }
        return;
    }

    /* Header detection */
    if (byte == NB_HEADER_1) {
        if (!uartParser_.gotHeader1) {
            uartParser_.gotHeader1 = true;
            return;
        }
    } else if (byte == NB_HEADER_2) {
        if (uartParser_.gotHeader1) {
            uartParser_.receiving = true;
            uartParser_.runningChecksum = 0;
            uartParser_.rxIndex = 0;
            return;
        }
    }
    uartParser_.reset();
}

/* =========================================================================
 * Ninebot Protocol Parsing — BLE (from phone app)
 * =========================================================================
 * Second protocol parser instance.
 * From firmware: CMP R0, #0x5A at 0x1A030; CMP R0, #0xA5 at 0x1A034
 */

void Nrf51Firmware::parseBleByte(uint8_t byte)
{
    if (bleParser_.receiving) {
        bleParser_.rxBuffer[bleParser_.rxIndex] = byte;

        if (bleParser_.rxIndex == 0) {
            bleParser_.expectedLength = byte + 1;
            if (bleParser_.expectedLength > 249) {
                bleParser_.reset();
                return;
            }
        }

        bleParser_.rxIndex++;

        if (bleParser_.rxIndex == bleParser_.expectedLength) {
            verifyAndDispatch(bleParser_,
                              &Nrf51Firmware::handleBlePacket);
            bleParser_.reset();
        } else {
            bleParser_.runningChecksum += byte;
        }
        return;
    }

    if (byte == NB_HEADER_1) {
        if (!bleParser_.gotHeader1) {
            bleParser_.gotHeader1 = true;
            return;
        }
    } else if (byte == NB_HEADER_2) {
        if (bleParser_.gotHeader1) {
            bleParser_.receiving = true;
            bleParser_.runningChecksum = 0;
            bleParser_.rxIndex = 0;
            return;
        }
    }
    bleParser_.reset();
}

/* =========================================================================
 * Checksum Calculation
 * =========================================================================
 * Same algorithm as STM32 side: sum bytes, bitwise NOT, mask 16-bit.
 */

uint16_t Nrf51Firmware::calcChecksum(const uint8_t* data, size_t length)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return static_cast<uint16_t>(~sum);
}

/* =========================================================================
 * Verify and Dispatch
 * ========================================================================= */

void Nrf51Firmware::verifyAndDispatch(
    NbParserState& parser,
    void (Nrf51Firmware::*handler)(const NbPacket&))
{
    uint8_t chkLo = parser.rxBuffer[parser.expectedLength - 2];
    uint8_t chkHi = parser.rxBuffer[parser.expectedLength - 1];
    uint16_t calc = static_cast<uint16_t>(
        ~(parser.runningChecksum - chkLo) & 0xFFFF);
    uint16_t recv = uint16_t(chkLo) | (uint16_t(chkHi) << 8);

    if (calc == recv) {
        NbPacket pkt{};
        pkt.length        = parser.rxBuffer[0];
        pkt.source        = parser.rxBuffer[1];
        pkt.destination   = parser.rxBuffer[2];
        pkt.command       = parser.rxBuffer[3];
        pkt.argument      = parser.rxBuffer[4];
        pkt.payloadLength = (pkt.length > 6) ? pkt.length - 6 : 0;
        pkt.checksum      = recv;
        if (pkt.payloadLength > 0 && pkt.payloadLength <= NB_MAX_PAYLOAD) {
            std::memcpy(pkt.payload, &parser.rxBuffer[5], pkt.payloadLength);
        }
        (this->*handler)(pkt);
    }
    /* Invalid checksum: silently drop (matches firmware behavior) */
}

/* =========================================================================
 * Packet Handlers
 * =========================================================================
 * The nRF51822 acts as a transparent protocol bridge:
 *   - Packets from UART (STM32) → relay to BLE (phone app)
 *   - Packets from BLE (phone) → relay to UART (STM32)
 *
 * Packets addressed to BLE (0x21) go both directions because the
 * STM32 on the BLE board handles BLE-addressed packets.
 *
 * The nRF51822 does NOT process Ninebot register read/write itself.
 * It's a pure relay.
 */

void Nrf51Firmware::handleUartPacket(const NbPacket& pkt)
{
    /* Everything from STM32 → relay to phone app via BLE */
    enqueuePacket(bleTxQueue_,
                  pkt.source, pkt.destination,
                  pkt.command, pkt.argument,
                  pkt.payload, pkt.payloadLength);
    uartToBleBytes_ += pkt.payloadLength + 9;  /* approximate raw size */
}

void Nrf51Firmware::handleBlePacket(const NbPacket& pkt)
{
    /* Everything from phone app → relay to STM32 via UART */
    enqueuePacket(uartTxQueue_,
                  pkt.source, pkt.destination,
                  pkt.command, pkt.argument,
                  pkt.payload, pkt.payloadLength);
    bleToUartBytes_ += pkt.payloadLength + 9;
}

/* =========================================================================
 * Packet Building & Enqueuing
 * =========================================================================
 * Builds a complete Ninebot protocol packet and adds to TX queue.
 * From firmware: MOVS R0, #0x5A at 0x184CC; MOVS R0, #0xA5 at 0x184D0
 */

void Nrf51Firmware::enqueuePacket(
    std::vector<std::vector<uint8_t>>& queue,
    uint8_t src, uint8_t dst, uint8_t cmd, uint8_t arg,
    const uint8_t* payload, uint8_t payloadLen)
{
    std::vector<uint8_t> pkt;
    pkt.reserve(payloadLen + 9);

    pkt.push_back(NB_HEADER_1);   /* 0x5A */
    pkt.push_back(NB_HEADER_2);   /* 0xA5 */
    pkt.push_back(payloadLen + 6); /* LEN */
    pkt.push_back(src);            /* SRC */
    pkt.push_back(dst);            /* DST */
    pkt.push_back(cmd);            /* CMD */
    pkt.push_back(arg);            /* ARG */

    for (uint8_t i = 0; i < payloadLen; i++) {
        pkt.push_back(payload[i]);
    }

    /* Checksum covers bytes [2..end) = LEN through payload */
    uint16_t chk = calcChecksum(&pkt[2], pkt.size() - 2);
    pkt.push_back(static_cast<uint8_t>(chk & 0xFF));
    pkt.push_back(static_cast<uint8_t>((chk >> 8) & 0xFF));

    queue.push_back(std::move(pkt));
}

/* =========================================================================
 * TX Queue Drain
 * ========================================================================= */

size_t Nrf51Firmware::drainUartTx()
{
    size_t count = 0;
    if (!hal_.uart) return 0;

    for (auto& pkt : uartTxQueue_) {
        for (uint8_t b : pkt) {
            hal_.uart->sendByte(b);
            count++;
        }
    }
    uartTxQueue_.clear();
    return count;
}

size_t Nrf51Firmware::drainBleTx()
{
    size_t count = 0;
    if (!hal_.softdevice || !bleConnected_ || !notifyEnabled_) {
        /* Can't send if not connected or notifications disabled.
         * Keep packets queued if disconnected — they'll be sent on reconnect.
         * Actually, clear them: phone will re-request on reconnect. */
        bleTxQueue_.clear();
        return 0;
    }

    for (auto& pkt : bleTxQueue_) {
        sendBleNotification(pkt.data(), static_cast<uint16_t>(pkt.size()));
        count++;
    }
    bleTxQueue_.clear();
    return count;
}

/* =========================================================================
 * BLE Operations
 * ========================================================================= */

void Nrf51Firmware::startAdvertising()
{
    if (!hal_.softdevice || advertising_) return;

    /* Set advertising data with device name.
     * Advertising data format:
     *   [len] [type] [data...]
     * Type 0x09 = Complete Local Name */
    uint8_t advData[31];
    size_t nameLen = std::strlen(DEVICE_NAME);
    if (nameLen > 28) nameLen = 28;

    advData[0] = static_cast<uint8_t>(nameLen + 1);  /* Length of this AD struct */
    advData[1] = 0x09;  /* Complete Local Name */
    std::memcpy(&advData[2], DEVICE_NAME, nameLen);

    /* Flags AD struct */
    uint8_t flagsLen = 2;
    advData[nameLen + 2] = flagsLen;
    advData[nameLen + 3] = 0x01;  /* AD type: Flags */
    advData[nameLen + 4] = 0x06;  /* LE General Discoverable + BR/EDR Not Supported */

    uint8_t totalAdvLen = static_cast<uint8_t>(nameLen + 5);

    hal_.softdevice->setAdvData(advData, totalAdvLen, nullptr, 0);
    hal_.softdevice->startAdvertising();
    advertising_ = true;
}

void Nrf51Firmware::sendBleNotification(const uint8_t* data, uint16_t len)
{
    if (!hal_.softdevice || connHandle_ == BLE_CONN_HANDLE_INVALID) return;

    hal_.softdevice->hvx(connHandle_, nusRxHandle_, data, len);
}

/* =========================================================================
 * BLE Data Feed (for testing)
 * ========================================================================= */

void Nrf51Firmware::feedBleBytes(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        parseBleByte(data[i]);
    }
}


/* =========================================================================
 * MiIO State Machine
 * =========================================================================
 * Reconstructed from firmware strings. This implements the Xiaomi MiIO
 * BLE authentication and registration flow.
 *
 * Flow:
 *   IDLE → WAIT_AUTH → AUTH_DECRYPT → WAIT_TOKEN → LOGIN_CONFIRM
 *        → WAIT_CLOUD_BIND → WAIT_APP_BOND → WAIT_SN
 *        → SN_ARRIVED → REGISTERED → FLASH_REGISTERED
 */

void Nrf51Firmware::miioAdvance(MiioState nextState)
{
    miioState_ = nextState;
}

/**
 * Handle auth write from phone.
 * From firmware: "On Auth Written", "decrypted auth, %x"
 */
void Nrf51Firmware::miioHandleAuth(const uint8_t* data, uint16_t len)
{
    if (len < 4) {
        miioAdvance(MiioState::ERROR);
        return;
    }

    /* Decrypt authentication data (simplified — real firmware uses
     * MD5-based challenge-response with Xiaomi cloud credentials) */
    (void)data;  /* Auth data processing */

    /* Advance to token exchange */
    miioAdvance(MiioState::WAIT_TOKEN);
}

/**
 * Handle token write from phone.
 * From firmware: "On Token Written", "login cfm handler, token: %02x %02x",
 *                "decrypted %x", "login cfm succ" / "login cfm fail"
 */
void Nrf51Firmware::miioHandleToken(const uint8_t* data, uint16_t len)
{
    if (len < 16) {
        miioAdvance(MiioState::ERROR);
        return;
    }

    /* Store token */
    std::memcpy(miioReg_.token.data, data, 16);
    miioReg_.token.valid = true;

    /* Login confirm */
    miioHandleLoginConfirm();
}

void Nrf51Firmware::miioHandleLoginConfirm()
{
    /* Verify token (simplified) */
    if (miioReg_.token.valid) {
        /* "login cfm succ" */
        miioHandleCloudBind(true);
    } else {
        /* "login cfm fail" */
        miioAdvance(MiioState::ERROR);
    }
}

/**
 * Handle cloud bind result.
 * From firmware: "cloud bind succ" / "cloud bind fail"
 */
void Nrf51Firmware::miioHandleCloudBind(bool success)
{
    if (success) {
        miioReg_.cloudBound = true;
        miioHandleAppBond(true);
    } else {
        miioAdvance(MiioState::ERROR);
    }
}

/**
 * Handle app bond result.
 * From firmware: "app bond succ" / "app bond fail", "mi_service, bond succ"
 */
void Nrf51Firmware::miioHandleAppBond(bool success)
{
    if (success) {
        miioReg_.appBonded = true;
        miioAdvance(MiioState::WAIT_SN);
    } else {
        miioAdvance(MiioState::ERROR);
    }
}

/**
 * Handle serial number arrival.
 * From firmware: "SN Arrived", "sn: %02x",
 *                "[E]: SN timeout, clear token", "[W]: Weak SN timeout."
 */
void Nrf51Firmware::miioHandleSN(const uint8_t* data, uint16_t len)
{
    if (len == 0) {
        /* SN timeout */
        miioAdvance(MiioState::ERROR);
        return;
    }

    uint16_t copyLen = (len < sizeof(miioReg_.serialNumber) - 1)
                       ? len : sizeof(miioReg_.serialNumber) - 1;
    std::memcpy(miioReg_.serialNumber, data, copyLen);
    miioReg_.serialNumber[copyLen] = '\0';

    miioAdvance(MiioState::SN_ARRIVED);
    miioRegister();
}

/**
 * Complete device registration.
 * From firmware: "Register succ, new token, encrypt sn, beaconkey."
 *                "new token: %02x %02x"
 */
void Nrf51Firmware::miioRegister()
{
    /* Generate beacon key (simplified) */
    std::memset(miioReg_.beaconKey, 0xAA, sizeof(miioReg_.beaconKey));

    miioAdvance(MiioState::REGISTERED);

    /* Save token to PSM */
    saveToken();

    /* Register MiIO flash capability */
    miioFlashRegister();
}

/**
 * Register MiIO flash capability.
 * From firmware: "[D]: miio ble flash register succ"
 *                "[E]: miio ble flash register fail"
 */
void Nrf51Firmware::miioFlashRegister()
{
    if (hal_.psm && hal_.psm->isFlashWriteRegistered()) {
        miioReg_.flashRegistered = true;
        miioAdvance(MiioState::FLASH_REGISTERED);
    } else {
        /* "[W]: flash write func not setting" */
        miioReg_.flashRegistered = false;
    }
}


/* =========================================================================
 * PSM Operations
 * =========================================================================
 * From firmware strings:
 *   "psm callback"
 *   "[D]: update succ. len = %d"
 *   "[D]: load succ, len = %d"
 *   "[D]: store succ. len = %d"
 *   "[D]: clear succ. len = %d"
 */

void Nrf51Firmware::psmCallback(PsmOpcode opcode, PsmResult result, uint16_t /*len*/)
{
    if (result != PsmResult::SUCCESS) {
        /* "[E]: flash operation error, opcode:%d" */
        return;
    }

    switch (opcode) {
        case PsmOpcode::LOAD:
            /* "[D]: load succ, len = %d" */
            break;
        case PsmOpcode::STORE:
            /* "[D]: store succ. len = %d" */
            break;
        case PsmOpcode::UPDATE:
            /* "[D]: update succ. len = %d" */
            break;
        case PsmOpcode::CLEAR:
            /* "[D]: clear succ. len = %d" */
            break;
    }
}

void Nrf51Firmware::saveToken()
{
    if (!hal_.psm || !miioReg_.token.valid) return;

    hal_.psm->store(0x0001, miioReg_.token.data, 16);
}

void Nrf51Firmware::loadToken()
{
    if (!hal_.psm) return;

    uint16_t actualLen = 0;
    if (hal_.psm->load(0x0001, storedToken_.data, 16, actualLen)) {
        storedToken_.valid = (actualLen == 16);
        if (storedToken_.valid) {
            /* "psm retrive succ, " and "retrive token, %02x %02x" */
            std::memcpy(miioReg_.token.data, storedToken_.data, 16);
            miioReg_.token.valid = true;
        }
    }
}


} // namespace nrf51
} // namespace ninebot
