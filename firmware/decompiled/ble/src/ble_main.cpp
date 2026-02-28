/**
 * @file ble_main.cpp
 * @brief BLE dashboard firmware — initialization, main loop, and input handling.
 *
 * Reconstructed from BLE_1.1.7.bin analysis.
 *
 * The BLE board's STM32 is the "brain" of the dashboard. It reads the
 * throttle and brake ADC, processes button presses, drives the dashboard
 * LEDs, and relays protocol packets between the nRF51822 BLE module
 * and the ESC mainboard.
 *
 * Main loop (approx. 10ms cycle time):
 *   1. Read throttle & brake ADC
 *   2. Process button debouncing
 *   3. Update dashboard LEDs
 *   4. Send throttle/brake values to ESC (every 50ms)
 *   5. Pump protocol TX on both UART channels
 */

#include "ble_firmware.h"

namespace ninebot {
namespace ble {

/* =========================================================================
 * Constructor
 * ========================================================================= */

BleFirmware::BleFirmware(hal::BleHal& hal)
    : hal_(hal)
    , chNrf_(hal.uartNrf)
    , chEsc_(hal.uartEsc)
{
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void BleFirmware::init()
{
    initRegisters();
    initProtocol();

    tickMs_ = 0;
    lastThrottleSendMs_ = 0;
    buttonPressMs_ = 0;
    throttleAdc_ = 0;
    brakeAdc_ = 0;
    throttlePct_ = 0;
    brakePct_ = 0;
    buttonState_ = false;
    lastButtonState_ = false;
    currentMode_ = 1;  // Default to D mode

    if (hal_.watchdog) {
        hal_.watchdog->init(500);
    }
}

void BleFirmware::initRegisters()
{
    const char* serial = "N2GWX000000000";
    regs_.writeBytes(ble_reg::SERIAL_NUMBER,
                     reinterpret_cast<const uint8_t*>(serial), 14);
    regs_.writeU16(ble_reg::FIRMWARE_VERSION, 0x0117);  // BLE 1.1.7
    regs_.writeU16(ble_reg::ERROR_CODE, 0x0000);
    regs_.writeU8(ble_reg::BLE_STATUS, 0x00);           // Not connected
    regs_.writeU16(ble_reg::BUTTON_STATE, 0x0000);
    regs_.writeU8(ble_reg::RIDING_MODE, currentMode_);  // Sync mode to register
}

void BleFirmware::initProtocol()
{
    /* USART1: nRF51822 BLE module */
    chNrf_.setCallback([this](const Packet& pkt) {
        handlePacketFromNrf(pkt, chNrf_);
    });

    /* USART2: ESC mainboard */
    chEsc_.setCallback([this](const Packet& pkt) {
        handlePacketFromEsc(pkt, chEsc_);
    });
}

/* =========================================================================
 * Main Loop
 * ========================================================================= */

void BleFirmware::mainLoopIteration()
{
    readInputs();
    processThrottle();
    processBrake();
    processButtons();
    updateDashboard();
    sendThrottleToEsc();
    processProtocol();

    if (hal_.watchdog) {
        hal_.watchdog->feed();
    }
}

/* =========================================================================
 * SysTick Handler
 * ========================================================================= */

void BleFirmware::sysTickHandler()
{
    tickMs_++;
}

/* =========================================================================
 * UART ISRs
 * ========================================================================= */

void BleFirmware::usart1RxIsr(uint8_t byte) { chNrf_.receiveByte(byte); }
void BleFirmware::usart2RxIsr(uint8_t byte) { chEsc_.receiveByte(byte); }

/* =========================================================================
 * Input Reading
 * =========================================================================
 * Reads the throttle and brake ADC channels and the mode button GPIO.
 *
 * ADC channels (from BLE board schematic):
 *   Channel 0 (PA0): Throttle potentiometer
 *   Channel 1 (PA1): Brake lever pressure sensor
 *
 * Mode button: PB12 (active-low, with internal pull-up)
 */
void BleFirmware::readInputs()
{
    if (hal_.adc) {
        throttleAdc_ = hal_.adc->readChannel(0);
        brakeAdc_    = hal_.adc->readChannel(1);
    }

    if (hal_.gpioB) {
        buttonState_ = !hal_.gpioB->readPin(12);  // Active-low
    }
}

/* =========================================================================
 * Throttle Processing
 * =========================================================================
 * Converts raw ADC value to a 0-100% throttle percentage with deadband
 * and clamping.
 *
 * The throttle uses a linear hall-effect sensor or potentiometer.
 * Typical range: ~400 (idle) to ~3400 (full throttle).
 */
void BleFirmware::processThrottle()
{
    uint16_t adc = throttleAdc_;

    if (adc < THROTTLE_MIN_ADC + THROTTLE_DEADBAND) {
        throttlePct_ = 0;
        return;
    }

    if (adc > THROTTLE_MAX_ADC) {
        adc = THROTTLE_MAX_ADC;
    }

    uint32_t range = THROTTLE_MAX_ADC - (THROTTLE_MIN_ADC + THROTTLE_DEADBAND);
    uint32_t val  = adc - (THROTTLE_MIN_ADC + THROTTLE_DEADBAND);
    throttlePct_ = static_cast<uint8_t>((val * 100) / range);
}

/* =========================================================================
 * Brake Processing
 * =========================================================================
 * Converts raw brake ADC to 0-100% brake percentage.
 * When brake is active, throttle is overridden to 0.
 */
void BleFirmware::processBrake()
{
    uint16_t adc = brakeAdc_;

    if (adc < BRAKE_MIN_ADC) {
        brakePct_ = 0;
        return;
    }

    if (adc > BRAKE_MAX_ADC) {
        adc = BRAKE_MAX_ADC;
    }

    uint32_t range = BRAKE_MAX_ADC - BRAKE_MIN_ADC;
    uint32_t val  = adc - BRAKE_MIN_ADC;
    brakePct_ = static_cast<uint8_t>((val * 100) / range);

    /* Brake overrides throttle */
    if (brakePct_ > 5) {
        throttlePct_ = 0;
    }
}

/* =========================================================================
 * Button Processing
 * =========================================================================
 * Handles the mode/power button with debouncing.
 *
 * Short press: cycle riding mode (Eco → D → Sport → Eco)
 * Long press (2s): power off
 */
void BleFirmware::processButtons()
{
    /* Rising edge detection (button just pressed) */
    if (buttonState_ && !lastButtonState_) {
        buttonPressMs_ = tickMs_;
    }

    /* Falling edge detection (button just released) */
    if (!buttonState_ && lastButtonState_) {
        uint32_t pressDuration = tickMs_ - buttonPressMs_;

        if (pressDuration >= BUTTON_DEBOUNCE_MS && pressDuration < LONG_PRESS_MS) {
            /* Short press: cycle riding mode */
            currentMode_ = (currentMode_ + 1) % 3;
            regs_.writeU8(ble_reg::RIDING_MODE, currentMode_);

            /* Send mode change to ESC via register write */
            uint8_t modePayload = currentMode_;
            chEsc_.enqueuePacket(
                DevAddr::BLE, DevAddr::ESC,
                Cmd::WRITE, esc_reg::RIDING_MODE,
                &modePayload, 1);
        }
        /* Long press power-off would set a shutdown flag */
    }

    lastButtonState_ = buttonState_;
}

/* =========================================================================
 * Dashboard LED Update
 * =========================================================================
 * Sets LED states based on current riding mode and system status.
 *
 * LED mapping on GPIOB:
 *   PB0: Power LED (always on when running)
 *   PB1: BLE connection indicator
 *   PB2: Eco mode LED
 *   PB3: Drive mode LED
 *   PB4: Sport mode LED
 *   PB5: Error indicator
 */
void BleFirmware::updateDashboard()
{
    if (!hal_.gpioB) return;

    /* Power LED: always on */
    hal_.gpioB->setPin(LED_POWER);

    /* BLE connection status */
    uint8_t bleStatus = regs_.readU8(ble_reg::BLE_STATUS);
    if (bleStatus) {
        hal_.gpioB->setPin(LED_BLE_CONN);
    } else {
        hal_.gpioB->resetPin(LED_BLE_CONN);
    }

    /* Mode LEDs: illuminate the active mode */
    hal_.gpioB->resetPin(LED_ECO);
    hal_.gpioB->resetPin(LED_DRIVE);
    hal_.gpioB->resetPin(LED_SPORT);

    switch (currentMode_) {
        case 0: hal_.gpioB->setPin(LED_ECO);   break;
        case 1: hal_.gpioB->setPin(LED_DRIVE);  break;
        case 2: hal_.gpioB->setPin(LED_SPORT);  break;
        default: break;
    }

    /* Error LED */
    uint16_t errCode = regs_.readU16(ble_reg::ERROR_CODE);
    if (errCode) {
        hal_.gpioB->setPin(LED_ERROR);
    } else {
        hal_.gpioB->resetPin(LED_ERROR);
    }
}

/* =========================================================================
 * Send Throttle/Brake to ESC
 * =========================================================================
 * Sends the current throttle and brake percentages to the ESC every 50ms.
 * This is the primary control path for motor speed.
 *
 * The ESC interprets throttle as a speed setpoint based on the current
 * riding mode's speed limit.
 */
void BleFirmware::sendThrottleToEsc()
{
    static constexpr uint32_t THROTTLE_SEND_INTERVAL_MS = 50;

    if (tickMs_ - lastThrottleSendMs_ < THROTTLE_SEND_INTERVAL_MS) return;
    lastThrottleSendMs_ = tickMs_;

    /* Pack throttle and brake into a 2-byte payload */
    uint8_t payload[2] = { throttlePct_, brakePct_ };

    chEsc_.enqueuePacket(
        DevAddr::BLE, DevAddr::ESC,
        Cmd::WRITE, 0x26,   // Throttle/brake register
        payload, 2);
}

/* =========================================================================
 * Protocol Pump
 * ========================================================================= */

void BleFirmware::processProtocol()
{
    chNrf_.drainTx();
    chEsc_.drainTx();
}

/* =========================================================================
 * Packet Handlers
 * =========================================================================
 * The BLE board acts primarily as a relay between the nRF and ESC.
 * Packets addressed to the BLE (0x21) are handled locally.
 * All other packets are forwarded.
 */

void BleFirmware::handlePacketFromEsc(const Packet& pkt, ProtocolChannel& respondOn)
{
    if (pkt.isFor(DevAddr::BLE)) {
        switch (pkt.command) {
            case static_cast<uint8_t>(Cmd::READ):
                handleRead(pkt, respondOn);
                break;
            case static_cast<uint8_t>(Cmd::WRITE):
                handleWrite(pkt, respondOn);
                break;
            default:
                break;
        }
    } else {
        /* Forward to nRF (toward phone app) */
        forwardToNrf(pkt);
    }
}

void BleFirmware::handlePacketFromNrf(const Packet& pkt, ProtocolChannel& respondOn)
{
    if (pkt.isFor(DevAddr::BLE)) {
        switch (pkt.command) {
            case static_cast<uint8_t>(Cmd::READ):
                handleRead(pkt, respondOn);
                break;
            case static_cast<uint8_t>(Cmd::WRITE):
                handleWrite(pkt, respondOn);
                break;
            default:
                break;
        }
    } else {
        /* Forward to ESC (toward mainboard or BMS) */
        forwardToEsc(pkt);
    }
}

void BleFirmware::handleRead(const Packet& pkt, ProtocolChannel& respondOn)
{
    uint8_t reg = pkt.argument;
    uint8_t readLen = (pkt.payloadLength > 0) ? pkt.payload[0] : 2;
    if (readLen > 16) readLen = 16;

    uint8_t data[16] = {};
    regs_.readBytes(reg, data, readLen);

    respondOn.enqueuePacket(
        DevAddr::BLE, static_cast<DevAddr>(pkt.source),
        Cmd::READ_RESPONSE, reg, data, readLen);
}

void BleFirmware::handleWrite(const Packet& pkt, ProtocolChannel& respondOn)
{
    uint8_t reg = pkt.argument;
    uint8_t success = 0x01;

    if (pkt.payloadLength > 0) {
        regs_.writeBytes(reg, pkt.payload, pkt.payloadLength);
    } else {
        success = 0x00;
    }

    respondOn.enqueuePacket(
        DevAddr::BLE, static_cast<DevAddr>(pkt.source),
        Cmd::WRITE_ACK, reg, &success, 1);
}

void BleFirmware::forwardToEsc(const Packet& pkt)
{
    chEsc_.enqueuePacket(pkt.source, pkt.destination,
                         pkt.command, pkt.argument,
                         pkt.payload, pkt.payloadLength);
}

void BleFirmware::forwardToNrf(const Packet& pkt)
{
    chNrf_.enqueuePacket(pkt.source, pkt.destination,
                         pkt.command, pkt.argument,
                         pkt.payload, pkt.payloadLength);
}

void BleFirmware::feedBytes(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        chEsc_.receiveByte(data[i]);
    }
}


} // namespace ble
} // namespace ninebot
