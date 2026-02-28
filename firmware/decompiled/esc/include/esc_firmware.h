/**
 * @file esc_firmware.h
 * @brief Decompiled ESC (DRV) firmware — main interface header.
 *
 * Reconstructed from DRV_1.6.13_Compat.bin (33,388 bytes).
 * Target: STM32F103CBT6 (128KB Flash, 20KB SRAM, 72MHz Cortex-M3).
 *
 * The ESC is the central hub of the Ninebot G30 Max. It:
 *   1. Controls the BLDC motor via 3-phase PWM (TIM1)
 *   2. Communicates with the BLE dashboard (USART2)
 *   3. Communicates with the BMS battery board (USART3)
 *   4. Accepts external/debug connections (USART1)
 *   5. Reads hall sensors for motor commutation
 *   6. Monitors bus voltage, phase currents, and temperature
 *   7. Implements riding modes (Eco/D/Sport), speed limiting, and locking
 *   8. Handles error detection and protection (overcurrent, overvoltage, etc.)
 *
 * Architecture:
 * @code
 *   ┌──────────────────────────────────────────────────┐
 *   │                  ESC Firmware                     │
 *   │                                                  │
 *   │  main()                                          │
 *   │    ├── SystemInit() → Clock to 72MHz HSE+PLL     │
 *   │    ├── InitPeripherals()                         │
 *   │    │     ├── USART1/2/3 @ 115200 8N1            │
 *   │    │     ├── TIM1 center-aligned PWM             │
 *   │    │     ├── TIM3 ADC trigger                    │
 *   │    │     ├── ADC1 scan mode                      │
 *   │    │     └── GPIO hall sensors, LEDs             │
 *   │    └── MainLoop()                                │
 *   │          ├── ProcessProtocol() [3 channels]      │
 *   │          ├── MotorControl() [FOC / 6-step]       │
 *   │          ├── SpeedControl() [PID loop]           │
 *   │          ├── ErrorCheck()                        │
 *   │          ├── UpdateRegisters()                   │
 *   │          └── WatchdogFeed()                      │
 *   │                                                  │
 *   │  ISR:                                            │
 *   │    ├── SysTick_Handler (1ms) → tick counter      │
 *   │    ├── TIM1_UP_Handler → motor commutation       │
 *   │    ├── USART1_Handler → protocol parser ch1      │
 *   │    ├── USART2_Handler → protocol parser ch2      │
 *   │    └── USART3_Handler → protocol parser ch3      │
 *   └──────────────────────────────────────────────────┘
 * @endcode
 *
 * @note All firmware addresses reference DRV_1.6.13_Compat.bin with
 *       base address 0x08001000 (after 4KB bootloader).
 */

#ifndef NINEBOT_ESC_FIRMWARE_H
#define NINEBOT_ESC_FIRMWARE_H

#include "hal.h"
#include "protocol.h"
#include "registers.h"
#include <cstdint>
#include <functional>

namespace ninebot {
namespace esc {

/* =========================================================================
 * Motor Control Constants
 * =========================================================================
 * From DRV_1.6.13 literal pool and TIM1 configuration analysis.
 */

/** Motor PWM frequency: 16 kHz (TIM1 ARR=2250 @ 72MHz / 2 center-aligned). */
static constexpr uint16_t MOTOR_PWM_PERIOD = 2250;

/** Number of hall sensor states (3 hall sensors → 6 valid states). */
static constexpr int HALL_STATES = 6;

/** Speed conversion: hall sensor edges per revolution (motor pole pairs × 6). */
static constexpr int HALL_EDGES_PER_REV = 90;   ///< 15 pole pairs × 6

/** Motor wheel circumference in millimeters (for speed calculation). */
static constexpr uint32_t WHEEL_CIRC_MM = 790;  ///< ~10 inch wheel

/* =========================================================================
 * Riding Mode Parameters
 * =========================================================================
 * Default speed limits per riding mode (can be overridden via register).
 */

/** Riding mode enumeration. */
enum class RidingMode : uint8_t {
    ECO   = 0,  ///< Economy mode — limited power and speed
    D     = 1,  ///< Default mode — balanced power
    SPORT = 2   ///< Sport mode — maximum performance
};

/** Speed limits per mode in units of 0.001 km/h. */
static constexpr uint16_t MODE_SPEED_LIMIT[] = {
    20000,  ///< Eco:   20.000 km/h
    25000,  ///< D:     25.000 km/h
    30000   ///< Sport: 30.000 km/h
};

/** Current limits per mode in units of 0.01 A. */
static constexpr uint16_t MODE_CURRENT_LIMIT[] = {
    1500,   ///< Eco:   15.00 A
    2000,   ///< D:     20.00 A
    3000    ///< Sport: 30.00 A
};


/* =========================================================================
 * ESC Firmware Class
 * =========================================================================
 * Encapsulates the complete ESC firmware behavior. In simulation mode,
 * peripheral access goes through the HAL interface. On real hardware,
 * the HAL maps to physical STM32 registers.
 */

/**
 * Complete decompiled ESC firmware.
 *
 * Reconstructed from DRV_1.6.13_Compat.bin (33,388 bytes).
 * Core functions with original firmware addresses:
 *
 *   | Function              | Address          | Description                    |
 *   |-----------------------|------------------|--------------------------------|
 *   | Reset_Handler         | 0x08001100       | Entry point → main()           |
 *   | SysTick_Handler       | 0x08005C54       | 1ms tick ISR                   |
 *   | TIM1_UP_IRQHandler    | 0x08005E74       | Motor commutation ISR          |
 *   | calculateChecksum     | 0x08002720       | Protocol checksum              |
 *   | buildPacket           | 0x080036AC       | Packet builder                 |
 *   | parseProtocolByte     | 0x08007128 (U1)  | Protocol parser                |
 *   | enqueuePacket         | 0x080071F4 (U1)  | TX queue                       |
 *   | uartTransmitHandler   | 0x08007610 (U1)  | TX byte pump                   |
 *   | dispatchReceivedPacket| 0x08005468       | Command router                 |
 */
class EscFirmware {
public:
    /**
     * Construct the ESC firmware with HAL bindings.
     * @param hal  Hardware abstraction layer for all peripherals.
     */
    explicit EscFirmware(hal::EscHal& hal);

    /** Initialize all peripherals and set default register values.
     *  Corresponds to the startup code after Reset_Handler jumps to main(). */
    void init();

    /** Execute one iteration of the main loop.
     *  In the real firmware, this runs forever. In simulation, call repeatedly. */
    void mainLoopIteration();

    /** SysTick interrupt handler (called every 1ms).
     *  @see DRV_1.6.13 SysTick_Handler @ 0x08005C54 */
    void sysTickHandler();

    /** TIM1 update interrupt — motor commutation.
     *  @see DRV_1.6.13 TIM1_UP_IRQHandler @ 0x08005E74 */
    void motorCommutationIsr();

    /** USART1 RX interrupt — external/debug protocol parser.
     *  Reads USART_DR and feeds to parseProtocolByte(). */
    void usart1RxIsr(uint8_t byte);

    /** USART2 RX interrupt — BLE dashboard protocol parser. */
    void usart2RxIsr(uint8_t byte);

    /** USART3 RX interrupt — BMS battery protocol parser. */
    void usart3RxIsr(uint8_t byte);

    /* ── Register Access ──────────────────────────────────────────── */

    /** Get the register file (for direct inspection/modification). */
    RegisterFile& registers() { return regs_; }
    const RegisterFile& registers() const { return regs_; }

    /** Read a register by address (protocol-compatible). */
    uint16_t readRegU16(uint8_t reg) const { return regs_.readU16(reg); }
    uint8_t  readRegU8(uint8_t reg) const  { return regs_.readU8(reg); }
    uint32_t readRegU32(uint8_t reg) const { return regs_.readU32(reg); }

    /** Write a register by address. */
    void writeRegU16(uint8_t reg, uint16_t val) { regs_.writeU16(reg, val); }
    void writeRegU8(uint8_t reg, uint8_t val)   { regs_.writeU8(reg, val); }

    /* ── Status Getters ───────────────────────────────────────────── */

    uint16_t currentSpeed() const    { return regs_.readU16(esc_reg::CURRENT_SPEED); }
    uint16_t batteryPercent() const  { return regs_.readU16(esc_reg::REMAINING_BATTERY); }
    uint16_t batteryVoltage() const  { return regs_.readU16(esc_reg::BATTERY_VOLTAGE); }
    uint16_t errorCode() const       { return regs_.readU16(esc_reg::ERROR_CODE); }
    uint8_t  ridingMode() const      { return regs_.readU8(esc_reg::RIDING_MODE); }
    bool     isLocked() const        { return regs_.readU8(esc_reg::LOCK_STATE) != 0; }
    uint32_t totalDistance() const   { return regs_.readU32(esc_reg::TOTAL_DISTANCE); }
    uint16_t uptime() const          { return regs_.readU16(esc_reg::UPTIME); }

    /* ── Control Setters ──────────────────────────────────────────── */

    void setSpeed(uint16_t speed_milli_kmh);
    void setRidingMode(RidingMode mode);
    void setLocked(bool locked);
    void setError(uint16_t errorBits);
    void clearError(uint16_t errorBits);
    void setBatteryVoltage(uint16_t voltage_centV);
    void setBatteryCurrent(int16_t current_centA);

    /* ── Protocol Access ──────────────────────────────────────────── */

    /** Get protocol channel for BLE (USART2). */
    ProtocolChannel& channelBle() { return chBle_; }

    /** Get protocol channel for EXT (USART1). */
    ProtocolChannel& channelExt() { return chExt_; }

    /** Get protocol channel for BMS (USART3). */
    ProtocolChannel& channelBms() { return chBms_; }

    /** Feed raw bytes into a protocol channel (for simulation). */
    void feedBytes(const uint8_t* data, size_t len);

    /** Drain all TX bytes from all channels. Returns combined TX output. */
    std::vector<uint8_t> drainAllTx();

    /** Get the tick count (ms since boot). */
    uint32_t tickCount() const { return tickMs_; }

private:
    /* ── Initialization subroutines ───────────────────────────────── */
    void initRegisters();         ///< Set default register values
    void initProtocol();          ///< Configure protocol channels
    void initMotor();             ///< Configure motor PWM

    /* ── Main loop subroutines ────────────────────────────────────── */
    void processProtocol();       ///< Pump TX on all 3 UART channels
    void updateMotorControl();    ///< Motor commutation + speed PID
    void updateSpeedLimit();      ///< Apply riding mode speed limits
    void checkErrors();           ///< Voltage/current/temperature checks
    void updateRegisters();       ///< Sync computed values to registers
    void periodicBmsQuery();      ///< Periodically request BMS data

    /* ── Packet dispatch ──────────────────────────────────────────── */

    /**
     * Dispatch a received protocol packet.
     * Routes based on command type (READ/WRITE) and register address.
     * @see DRV_1.6.13 dispatchReceivedPacket @ 0x08005468
     */
    void handlePacket(const Packet& pkt, ProtocolChannel& respondOn);

    /** Handle a register READ command. */
    void handleRead(const Packet& pkt, ProtocolChannel& respondOn);

    /** Handle a register WRITE command. */
    void handleWrite(const Packet& pkt, ProtocolChannel& respondOn);

    /** Handle a BMS response (READ_RESPONSE from BMS address). */
    void handleBmsResponse(const Packet& pkt);

    /** Forward a packet to another bus (routing between BLE <-> BMS). */
    void forwardPacket(const Packet& pkt);

    /* ── Member data ──────────────────────────────────────────────── */
    hal::EscHal&    hal_;          ///< Hardware abstraction
    RegisterFile    regs_;         ///< Device register storage

    /* Protocol channels (one per UART) */
    ProtocolChannel chExt_;        ///< USART1: external/debug
    ProtocolChannel chBle_;        ///< USART2: BLE dashboard
    ProtocolChannel chBms_;        ///< USART3: BMS battery

    /* Timing */
    uint32_t tickMs_           = 0;   ///< Millisecond tick counter
    uint32_t lastBmsQueryMs_   = 0;   ///< Last BMS query timestamp
    uint32_t lastSpeedCalcMs_  = 0;   ///< Last speed calculation time

    /* Motor control state */
    uint8_t  hallState_        = 0;   ///< Current hall sensor state (0-5)
    uint16_t motorDutyCycle_   = 0;   ///< Current PWM duty cycle (0-2250)
    uint32_t hallEdgeCount_    = 0;   ///< Hall edge counter for speed calc
    int16_t  targetCurrent_    = 0;   ///< Target motor current (PID output)

    /* Speed control */
    uint16_t speedSetpoint_    = 0;   ///< Speed setpoint from throttle
    int32_t  speedErrorInteg_  = 0;   ///< PID integral term
};


} // namespace esc
} // namespace ninebot

#endif // NINEBOT_ESC_FIRMWARE_H
