/**
 * @file ble_firmware.h
 * @brief Decompiled BLE dashboard firmware — main interface header.
 *
 * Reconstructed from BLE_1.1.7.bin.
 * Target: STM32F103C8T6 (64KB Flash, 20KB SRAM, 72MHz Cortex-M3)
 *         + nRF51822 (Bluetooth Low Energy co-processor).
 *
 * The BLE dashboard board handles:
 *   1. Throttle input via ADC (thumb throttle potentiometer)
 *   2. Brake input via ADC (electronic brake lever)
 *   3. Dashboard display (LED indicators for speed mode, battery, errors)
 *   4. Button input (power button, mode button)
 *   5. Bluetooth communication via nRF51822 (SPI/UART)
 *   6. Protocol bridge: App↔nRF↔STM32↔ESC (USART2)
 *
 * Architecture:
 * @code
 *   ┌───────────────────────────────────────────────┐
 *   │              BLE Dashboard Board               │
 *   │                                               │
 *   │  STM32F103C8T6 (main MCU):                   │
 *   │    ├── USART1 ↔ nRF51822 (BLE comms)         │
 *   │    ├── USART2 ↔ ESC (Ninebot protocol)       │
 *   │    ├── ADC1 ch0: Throttle potentiometer       │
 *   │    ├── ADC1 ch1: Brake pressure sensor        │
 *   │    ├── GPIO: Dashboard LEDs, mode button      │
 *   │    └── SysTick: 1ms timing                   │
 *   │                                               │
 *   │  nRF51822 (BLE co-processor):                 │
 *   │    ├── SoftDevice S110/S130 BLE stack          │
 *   │    ├── BLE GATT service for Ninebot app       │
 *   │    └── UART ↔ STM32 for packet relay          │
 *   └───────────────────────────────────────────────┘
 * @endcode
 *
 * @see BLE_1.1.7 firmware analysis
 * @see BLE_1.1.7 parseProtocolByte @ 0x08004C40 (USART1)
 * @see BLE_1.1.7 parseProtocolByte @ 0x08004F24 (USART2)
 * @see BLE_1.1.7 buildPacket       @ 0x08002CC4
 */

#ifndef NINEBOT_BLE_FIRMWARE_H
#define NINEBOT_BLE_FIRMWARE_H

#include "hal.h"
#include "protocol.h"
#include "registers.h"
#include <cstdint>
#include <functional>

namespace ninebot {
namespace ble {

/* =========================================================================
 * Dashboard Constants
 * ========================================================================= */

/** ADC thresholds for throttle input. */
static constexpr uint16_t THROTTLE_MIN_ADC = 400;   ///< ADC value at idle
static constexpr uint16_t THROTTLE_MAX_ADC = 3400;  ///< ADC value at max
static constexpr uint16_t THROTTLE_DEADBAND = 50;   ///< Deadband near idle

/** ADC thresholds for brake input. */
static constexpr uint16_t BRAKE_MIN_ADC    = 500;   ///< ADC value when released
static constexpr uint16_t BRAKE_MAX_ADC    = 3200;  ///< ADC value at full brake

/** Dashboard LED bit positions (GPIOB output register). */
static constexpr uint8_t LED_POWER      = 0;  ///< Power indicator LED
static constexpr uint8_t LED_BLE_CONN   = 1;  ///< BLE connected indicator
static constexpr uint8_t LED_ECO        = 2;  ///< Eco mode LED
static constexpr uint8_t LED_DRIVE      = 3;  ///< Drive mode LED
static constexpr uint8_t LED_SPORT      = 4;  ///< Sport mode LED
static constexpr uint8_t LED_ERROR      = 5;  ///< Error indicator LED

/** Button debounce time in milliseconds. */
static constexpr uint16_t BUTTON_DEBOUNCE_MS = 50;

/** Long press threshold for mode button (ms). */
static constexpr uint16_t LONG_PRESS_MS = 2000;


/* =========================================================================
 * BLE Firmware Class
 * ========================================================================= */

/**
 * Complete decompiled BLE dashboard firmware.
 *
 * Handles user input (throttle, brake, buttons), dashboard display,
 * and acts as a protocol bridge between the phone app and the ESC.
 */
class BleFirmware {
public:
    explicit BleFirmware(hal::BleHal& hal);

    /** Initialize peripherals and set defaults. */
    void init();

    /** Execute one main loop iteration. */
    void mainLoopIteration();

    /** SysTick handler (1ms). */
    void sysTickHandler();

    /** USART1 RX ISR (nRF51822 data). */
    void usart1RxIsr(uint8_t byte);

    /** USART2 RX ISR (ESC data). */
    void usart2RxIsr(uint8_t byte);

    /* ── Register Access ──────────────────────────────────── */
    RegisterFile& registers() { return regs_; }
    const RegisterFile& registers() const { return regs_; }

    /* ── Status ───────────────────────────────────────────── */
    uint16_t throttleRaw() const    { return throttleAdc_; }
    uint16_t brakeRaw() const       { return brakeAdc_; }
    uint8_t  throttlePercent() const { return throttlePct_; }
    uint8_t  brakePercent() const   { return brakePct_; }
    bool     isButtonPressed() const { return buttonState_; }
    uint32_t tickCount() const      { return tickMs_; }

    /** Get protocol channel for nRF (USART1). */
    ProtocolChannel& channelNrf() { return chNrf_; }

    /** Get protocol channel for ESC (USART2). */
    ProtocolChannel& channelEsc() { return chEsc_; }

    /** Feed raw bytes into ESC-facing channel. */
    void feedBytes(const uint8_t* data, size_t len);

    /** Set simulated throttle ADC value. */
    void setThrottleAdc(uint16_t val) { throttleAdc_ = val; }

    /** Set simulated brake ADC value. */
    void setBrakeAdc(uint16_t val) { brakeAdc_ = val; }

    /** Set simulated button state. */
    void setButtonState(bool pressed) { buttonState_ = pressed; }

private:
    void initRegisters();
    void initProtocol();

    void readInputs();            ///< Read ADC and buttons
    void processThrottle();       ///< Convert throttle ADC to speed request
    void processBrake();          ///< Convert brake ADC to brake request
    void processButtons();        ///< Handle mode/power button presses
    void updateDashboard();       ///< Update LED indicators
    void processProtocol();       ///< Pump TX on both channels
    void sendThrottleToEsc();     ///< Periodic throttle/brake update to ESC
    void handlePacketFromEsc(const Packet& pkt, ProtocolChannel& respondOn);
    void handlePacketFromNrf(const Packet& pkt, ProtocolChannel& respondOn);
    void handleRead(const Packet& pkt, ProtocolChannel& respondOn);
    void handleWrite(const Packet& pkt, ProtocolChannel& respondOn);
    void forwardToEsc(const Packet& pkt);
    void forwardToNrf(const Packet& pkt);

    hal::BleHal&    hal_;
    RegisterFile    regs_;
    ProtocolChannel chNrf_;       ///< USART1: nRF51822
    ProtocolChannel chEsc_;       ///< USART2: ESC mainboard

    uint32_t tickMs_             = 0;
    uint32_t lastThrottleSendMs_ = 0;
    uint32_t buttonPressMs_      = 0;

    uint16_t throttleAdc_        = 0;
    uint16_t brakeAdc_           = 0;
    uint8_t  throttlePct_        = 0;
    uint8_t  brakePct_           = 0;
    bool     buttonState_        = false;
    bool     lastButtonState_    = false;
    uint8_t  currentMode_        = 1;     ///< Current riding mode (0=Eco, 1=D, 2=Sport)
};


} // namespace ble
} // namespace ninebot

#endif // NINEBOT_BLE_FIRMWARE_H
