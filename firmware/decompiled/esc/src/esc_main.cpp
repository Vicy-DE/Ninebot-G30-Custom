/**
 * @file esc_main.cpp
 * @brief ESC firmware — main initialization and loop.
 *
 * Reconstructed from DRV_1.6.13 Reset_Handler @ 0x08001100 and main().
 *
 * Startup sequence (from disassembly):
 *   1. Reset_Handler copies .data from flash to SRAM
 *   2. Zeros .bss section
 *   3. Calls SystemInit() → configures HSE + PLL for 72MHz
 *   4. Calls main()
 *   5. main() initializes peripherals, then enters infinite main loop
 *
 * Main loop (from DRV_1.6.13 analysis):
 *   while (true) {
 *       processProtocol();      // Service all 3 UART channels
 *       updateMotorControl();   // Motor commutation and speed PID
 *       checkErrors();          // Safety checks
 *       updateRegisters();      // Sync computed state to registers
 *       periodicBmsQuery();     // Query BMS every 200ms
 *       watchdog.feed();        // Prevent watchdog reset
 *   }
 */

#include "esc_firmware.h"

namespace ninebot {
namespace esc {

/* =========================================================================
 * Constructor
 * ========================================================================= */

EscFirmware::EscFirmware(hal::EscHal& hal)
    : hal_(hal)
    , chExt_(hal.uartExt)
    , chBle_(hal.uartBle)
    , chBms_(hal.uartBms)
{
}

/* =========================================================================
 * Initialization
 * =========================================================================
 * Corresponds to DRV_1.6.13 main() → init section.
 *
 * The firmware performs these steps in order:
 *   1. Clock configuration (HSE + PLL → 72MHz)
 *   2. GPIO pin configuration (hall sensors, LEDs, UART pins)
 *   3. UART initialization (115200 8N1 on all 3 USARTs)
 *   4. Timer initialization (TIM1 motor PWM, TIM3 general)
 *   5. ADC initialization (bus voltage, phase currents)
 *   6. Default register values
 *   7. Watchdog start
 */
void EscFirmware::init()
{
    initRegisters();
    initProtocol();
    initMotor();

    /* Start SysTick at 1ms intervals (72MHz / 72000 = 1kHz) */
    tickMs_ = 0;
    lastBmsQueryMs_ = 0;
    lastSpeedCalcMs_ = 0;

    /* Start watchdog with 500ms timeout */
    if (hal_.watchdog) {
        hal_.watchdog->init(500);
    }
}

/* =========================================================================
 * Register Defaults
 * =========================================================================
 * Sets all registers to their power-on default values.
 *
 * These values are observed from a stock G30 Max at first boot.
 * The serial number, firmware version, and factory calibration data
 * would normally come from flash (preserved across updates).
 */
void EscFirmware::initRegisters()
{
    /* Serial number: 14 bytes ASCII (stored in flash, read at boot) */
    const char* serial = "N2GWX000000000";
    regs_.writeBytes(esc_reg::SERIAL_NUMBER,
                     reinterpret_cast<const uint8_t*>(serial), 14);

    /*
     * Firmware version in BCD format: 1.6.13 → 0x0613
     *   Byte layout: major.minor (each nibble)
     *   0x06 = version 1.6, 0x13 = sub-version 13
     *
     * @see DRV_1.6.13 firmware version constant in flash
     */
    regs_.writeU16(esc_reg::FIRMWARE_VERSION, 0x0613);

    /* Error/warning/status: all clear at boot */
    regs_.writeU16(esc_reg::ERROR_CODE, 0x0000);
    regs_.writeU16(esc_reg::WARNING_CODE, 0x0000);
    regs_.writeU16(esc_reg::STATUS_FLAGS, 0x0000);

    /* Battery state: defaults updated by BMS queries */
    regs_.writeU16(esc_reg::REMAINING_BATTERY, 0);
    regs_.writeU16(esc_reg::REMAINING_RANGE, 0);
    regs_.writeU16(esc_reg::BATTERY_VOLTAGE, 0);
    regs_.writeU16(esc_reg::BATTERY_CURRENT, 0);

    /* Speed and distance */
    regs_.writeU16(esc_reg::CURRENT_SPEED, 0);
    regs_.writeU32(esc_reg::TRIP_DISTANCE, 0);
    regs_.writeU32(esc_reg::TOTAL_DISTANCE, 0);
    regs_.writeU16(esc_reg::UPTIME, 0);

    /* Temperature: read from ADC, default to 25.0°C */
    regs_.writeU16(esc_reg::FRAME_TEMPERATURE, 250);

    /* Riding configuration: factory defaults */
    regs_.writeU8(esc_reg::LOCK_STATE, 0);          // Unlocked
    regs_.writeU8(esc_reg::CRUISE_CONTROL, 0);      // Off
    regs_.writeU8(esc_reg::TAIL_LIGHT, 0);          // Off
    regs_.writeU8(esc_reg::RIDING_MODE, 1);         // D (normal) mode
    regs_.writeU16(esc_reg::SPEED_LIMIT_SETTING, 0);  // No user override
    regs_.writeU16(esc_reg::MOTOR_HALL_SPEED, 0);
}

/* =========================================================================
 * Protocol Initialization
 * =========================================================================
 * Sets up the three protocol channels with their packet callbacks.
 *
 * Each channel's callback routes to handlePacket() which implements
 * the firmware's dispatchReceivedPacket @ 0x08005468.
 */
void EscFirmware::initProtocol()
{
    /*
     * USART1 (External/Debug): Channel 1
     * Used for PC tools (address 0x3F) and general debugging.
     */
    chExt_.setCallback([this](const Packet& pkt) {
        handlePacket(pkt, chExt_);
    });

    /*
     * USART2 (BLE Dashboard): Channel 0
     * Primary communication path for the phone app (via BLE).
     * The BLE board forwards app packets to the ESC.
     */
    chBle_.setCallback([this](const Packet& pkt) {
        handlePacket(pkt, chBle_);
    });

    /*
     * USART3 (BMS Battery): Channel 2
     * Dedicated link to the battery management system.
     * ESC queries BMS periodically for voltage/current/SOC.
     */
    chBms_.setCallback([this](const Packet& pkt) {
        handlePacket(pkt, chBms_);
    });
}

/* =========================================================================
 * Motor Initialization
 * =========================================================================
 * Configures TIM1 for center-aligned PWM and prepares motor state.
 *
 * From DRV_1.6.13 analysis:
 *   - TIM1 configured for center-aligned mode 1
 *   - ARR = 2250 (period, giving ~16kHz PWM @ 72MHz / 2)
 *   - Channels 1-3: PWM mode 1, complementary outputs enabled
 *   - Dead time: ~1us (DTG register)
 *   - Break input: enabled for overcurrent protection
 */
void EscFirmware::initMotor()
{
    hallState_ = 0;
    motorDutyCycle_ = 0;
    hallEdgeCount_ = 0;
    targetCurrent_ = 0;
    speedSetpoint_ = 0;
    speedErrorInteg_ = 0;

    if (hal_.timerMotor) {
        /* Set all PWM channels to 0 (motor off) */
        hal_.timerMotor->setCompare(1, 0);
        hal_.timerMotor->setCompare(2, 0);
        hal_.timerMotor->setCompare(3, 0);
    }
}

/* =========================================================================
 * Main Loop
 * =========================================================================
 * One iteration of the firmware's infinite main loop.
 *
 * In the real firmware, this is:
 *   while(1) {
 *       // ... all processing ...
 *       IWDG_ReloadCounter();
 *   }
 *
 * In simulation, call this repeatedly to advance the firmware state.
 */
void EscFirmware::mainLoopIteration()
{
    processProtocol();
    updateMotorControl();
    updateSpeedLimit();
    checkErrors();
    updateRegisters();
    periodicBmsQuery();

    /* Feed the watchdog to prevent reset */
    if (hal_.watchdog) {
        hal_.watchdog->feed();
    }
}

/* =========================================================================
 * SysTick Handler
 * =========================================================================
 * Called every 1ms by the SysTick interrupt.
 *
 * @see DRV_1.6.13 SysTick_Handler @ 0x08005C54
 *
 * The SysTick handler in the firmware:
 *   1. Increments a global millisecond counter
 *   2. Updates debounce counters for button inputs
 *   3. Manages timing flags for periodic tasks
 */
void EscFirmware::sysTickHandler()
{
    tickMs_++;

    /* Update uptime register every 1000ms (1 second) */
    if (tickMs_ % 1000 == 0) {
        uint16_t up = regs_.readU16(esc_reg::UPTIME);
        regs_.writeU16(esc_reg::UPTIME, up + 1);
    }
}

/* =========================================================================
 * UART ISR Entry Points
 * =========================================================================
 * Each USART interrupt reads the data register and feeds the byte
 * into the corresponding protocol parser channel.
 */
void EscFirmware::usart1RxIsr(uint8_t byte) { chExt_.receiveByte(byte); }
void EscFirmware::usart2RxIsr(uint8_t byte) { chBle_.receiveByte(byte); }
void EscFirmware::usart3RxIsr(uint8_t byte) { chBms_.receiveByte(byte); }


} // namespace esc
} // namespace ninebot
