/**
 * @file hal.h
 * @brief Hardware Abstraction Layer for Ninebot G30 Max firmware.
 *
 * Provides a platform-independent interface for all STM32F103 peripherals
 * used by the ESC, BLE, and BMS firmware. When compiled for the host
 * (simulation mode), these are backed by software implementations.
 * When cross-compiled for STM32, they map to real peripheral registers.
 *
 * Peripheral mapping (from firmware analysis):
 *   ┌─────────┬────────────────┬──────────────────────────────────┐
 *   │ Board   │ Peripheral     │ Function                         │
 *   ├─────────┼────────────────┼──────────────────────────────────┤
 *   │ ESC     │ USART1         │ External/debug port              │
 *   │         │ USART2         │ BLE dashboard communication      │
 *   │         │ USART3         │ BMS battery communication        │
 *   │         │ TIM1           │ Motor PWM (3-phase bridge)       │
 *   │         │ TIM3           │ General timing / ADC trigger      │
 *   │         │ ADC1           │ Throttle, voltage, current sense  │
 *   │         │ GPIOA/B/C      │ Hall sensors, LEDs, misc I/O     │
 *   │         │ SysTick        │ 1ms system tick                   │
 *   ├─────────┼────────────────┼──────────────────────────────────┤
 *   │ BLE     │ USART1         │ nRF51822 communication           │
 *   │         │ USART2         │ ESC mainboard communication      │
 *   │         │ TIM2           │ Display / LED timing             │
 *   │         │ ADC1           │ Throttle, brake ADC              │
 *   │         │ SysTick        │ 1ms system tick                   │
 *   ├─────────┼────────────────┼──────────────────────────────────┤
 *   │ BMS     │ USART2         │ ESC mainboard communication      │
 *   │         │ I2C1           │ BQ76940 analog front-end         │
 *   │         │ ADC1           │ Temperature NTC sensors          │
 *   │         │ SysTick        │ 1ms system tick                   │
 *   └─────────┴────────────────┴──────────────────────────────────┘
 *
 * @note This is reverse-engineered code. See project safety warnings.
 */

#ifndef NINEBOT_HAL_H
#define NINEBOT_HAL_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <vector>
#include <array>

namespace ninebot {
namespace hal {

/* =========================================================================
 * STM32F103 Memory Map Constants
 * =========================================================================
 * From STM32F103 Reference Manual (RM0008)
 */

/** Flash memory base addresses */
static constexpr uint32_t FLASH_BASE        = 0x08000000;
static constexpr uint32_t FLASH_APP_BASE    = 0x08001000;  ///< After 4KB bootloader
static constexpr uint32_t FLASH_SIZE_CB     = 128 * 1024;  ///< STM32F103CBT6 = 128KB
static constexpr uint32_t FLASH_SIZE_C8     = 64 * 1024;   ///< STM32F103C8T6 = 64KB

/** SRAM */
static constexpr uint32_t SRAM_BASE         = 0x20000000;
static constexpr uint32_t SRAM_SIZE         = 20 * 1024;   ///< 20KB

/** Peripheral base addresses */
static constexpr uint32_t PERIPH_BASE       = 0x40000000;
static constexpr uint32_t APB1_BASE         = PERIPH_BASE;
static constexpr uint32_t APB2_BASE         = PERIPH_BASE + 0x10000;
static constexpr uint32_t AHB_BASE          = PERIPH_BASE + 0x20000;

/** USART base addresses */
static constexpr uint32_t USART1_BASE       = APB2_BASE + 0x3800;
static constexpr uint32_t USART2_BASE       = APB1_BASE + 0x4400;
static constexpr uint32_t USART3_BASE       = APB1_BASE + 0x4800;

/** Timer base addresses */
static constexpr uint32_t TIM1_BASE         = APB2_BASE + 0x2C00;
static constexpr uint32_t TIM2_BASE         = APB1_BASE + 0x0000;
static constexpr uint32_t TIM3_BASE         = APB1_BASE + 0x0400;

/** GPIO base addresses */
static constexpr uint32_t GPIOA_BASE        = APB2_BASE + 0x0800;
static constexpr uint32_t GPIOB_BASE        = APB2_BASE + 0x0C00;
static constexpr uint32_t GPIOC_BASE        = APB2_BASE + 0x1000;

/** I2C base addresses */
static constexpr uint32_t I2C1_BASE         = APB1_BASE + 0x5400;


/* =========================================================================
 * UART Configuration
 * ========================================================================= */

/** UART configuration for Ninebot protocol (all boards use the same settings). */
struct UartConfig {
    uint32_t baudRate = 115200;   ///< 115200 baud (BRR=0x0271 @ 72MHz)
    uint8_t  dataBits = 8;        ///< 8 data bits
    uint8_t  stopBits = 1;        ///< 1 stop bit
    bool     parity   = false;    ///< No parity
};


/* =========================================================================
 * UART Hardware Interface
 * =========================================================================
 * Abstract interface for UART peripheral operations. Implementations:
 *   - SimUart (simulator/include/sim_uart.h) for host testing
 *   - Stm32Uart (target only) for real hardware
 */

/**
 * Abstract UART peripheral interface.
 *
 * Mirrors STM32 USART register operations at a functional level.
 * The firmware reads/writes USART_SR and USART_DR directly; this
 * interface wraps those operations for portability.
 */
class IUart {
public:
    virtual ~IUart() = default;

    /** Send one byte (writes to USART_DR). */
    virtual void sendByte(uint8_t byte) = 0;

    /** Receive one byte (reads from USART_DR). Returns 0 if none available. */
    virtual uint8_t receiveByte() = 0;

    /** Check if TX data register is empty (USART_SR.TXE). */
    virtual bool isTxEmpty() const = 0;

    /** Check if transmission is complete (USART_SR.TC). */
    virtual bool isTxComplete() const = 0;

    /** Check if RX data register is not empty (USART_SR.RXNE). */
    virtual bool isRxReady() const = 0;

    /** Enable TX interrupt. */
    virtual void enableTxInterrupt() = 0;

    /** Disable TX interrupt. */
    virtual void disableTxInterrupt() = 0;

    /** Enable RX interrupt (RXNEIE). */
    virtual void enableRxInterrupt() = 0;
};


/* =========================================================================
 * GPIO Interface
 * ========================================================================= */

/** GPIO pin mode (from STM32 CRL/CRH register configuration). */
enum class GpioMode : uint8_t {
    INPUT_ANALOG    = 0x00,   ///< Analog input (for ADC)
    INPUT_FLOATING  = 0x04,   ///< Floating input
    INPUT_PULLUP    = 0x08,   ///< Input with pull-up/pull-down
    OUTPUT_PP_10MHZ = 0x01,   ///< Push-pull output, 10MHz
    OUTPUT_PP_2MHZ  = 0x02,   ///< Push-pull output, 2MHz
    OUTPUT_PP_50MHZ = 0x03,   ///< Push-pull output, 50MHz
    AF_PP_50MHZ     = 0x0B,   ///< Alternate function push-pull, 50MHz
    AF_OD_50MHZ     = 0x0F    ///< Alternate function open-drain, 50MHz
};

/**
 * Abstract GPIO port interface.
 *
 * Each port (A, B, C) has 16 pins. The firmware accesses GPIO through
 * direct register writes (BSRR, BRR, IDR, ODR).
 */
class IGpio {
public:
    virtual ~IGpio() = default;

    /** Set a pin high (writes to BSRR). */
    virtual void setPin(uint8_t pin) = 0;

    /** Reset a pin low (writes to BRR). */
    virtual void resetPin(uint8_t pin) = 0;

    /** Read a pin state (reads IDR bit). */
    virtual bool readPin(uint8_t pin) const = 0;

    /** Read entire port (reads IDR). */
    virtual uint16_t readPort() const = 0;

    /** Configure pin mode (writes CRL/CRH). */
    virtual void configurePin(uint8_t pin, GpioMode mode) = 0;
};


/* =========================================================================
 * ADC Interface
 * ========================================================================= */

/**
 * Abstract ADC interface.
 *
 * The firmware uses single-channel conversions on ADC1 for:
 *   - ESC: bus voltage (0.01V), phase currents, throttle
 *   - BLE: throttle position, brake pressure
 *   - BMS: NTC temperature sensors
 */
class IAdc {
public:
    virtual ~IAdc() = default;

    /** Read a single ADC channel (12-bit result, 0-4095). */
    virtual uint16_t readChannel(uint8_t channel) = 0;

    /** Start a conversion on the specified channel. */
    virtual void startConversion(uint8_t channel) = 0;

    /** Check if conversion is complete. */
    virtual bool isConversionDone() const = 0;
};


/* =========================================================================
 * I2C Interface (BMS only)
 * ========================================================================= */

/**
 * Abstract I2C master interface.
 *
 * Used exclusively by the BMS firmware to communicate with the BQ76940
 * analog front-end IC. The BQ76940 has I2C address 0x08.
 *
 * @see BQ76940 datasheet, section 7.6 (I2C interface)
 */
class II2c {
public:
    virtual ~II2c() = default;

    /** Write a register on the I2C slave device. */
    virtual bool writeRegister(uint8_t slaveAddr, uint8_t reg, uint8_t value) = 0;

    /** Read a register from the I2C slave device. */
    virtual uint8_t readRegister(uint8_t slaveAddr, uint8_t reg) = 0;

    /** Read multiple bytes from an I2C slave device. */
    virtual bool readRegisters(uint8_t slaveAddr, uint8_t startReg,
                               uint8_t* buffer, uint8_t length) = 0;
};


/* =========================================================================
 * Timer Interface
 * ========================================================================= */

/**
 * Abstract timer interface for PWM generation and system timing.
 *
 * ESC uses TIM1 for motor PWM (complementary outputs, center-aligned).
 * TIM3 is used for general timing and ADC trigger.
 */
class ITimer {
public:
    virtual ~ITimer() = default;

    /** Set PWM duty cycle for a channel (0-ARR range). */
    virtual void setCompare(uint8_t channel, uint16_t value) = 0;

    /** Get the auto-reload register value (period). */
    virtual uint16_t getAutoReload() const = 0;

    /** Get the current counter value. */
    virtual uint16_t getCounter() const = 0;

    /** Enable/disable the timer. */
    virtual void setEnabled(bool enabled) = 0;
};


/* =========================================================================
 * SysTick Interface
 * ========================================================================= */

/**
 * System tick counter.
 *
 * The SysTick timer fires every 1ms in all firmware variants.
 * Used for timing, debouncing, and periodic task scheduling.
 *
 * @see DRV_1.6.13 SysTick_Handler @ 0x08005C54
 */
class ISysTick {
public:
    virtual ~ISysTick() = default;

    /** Get the current millisecond tick count. */
    virtual uint32_t getTickMs() const = 0;

    /** Delay for the specified number of milliseconds. */
    virtual void delayMs(uint32_t ms) = 0;
};


/* =========================================================================
 * System Watchdog
 * ========================================================================= */

/**
 * Independent watchdog interface.
 *
 * The firmware feeds the IWDG periodically from the main loop.
 * If the main loop stalls, the IWDG triggers a system reset.
 */
class IWatchdog {
public:
    virtual ~IWatchdog() = default;

    /** Feed/kick the watchdog to prevent reset. */
    virtual void feed() = 0;

    /** Initialize the watchdog with the specified timeout. */
    virtual void init(uint32_t timeoutMs) = 0;
};


/* =========================================================================
 * Complete Board HAL
 * =========================================================================
 * Bundles all peripheral interfaces for a single board. Each firmware
 * implementation receives a BoardHal at startup.
 */

/**
 * Hardware abstraction for the ESC (DRV) board.
 *
 * Provides access to all peripherals used by the ESC firmware:
 *   - 3 UARTs (BLE, EXT, BMS)
 *   - TIM1 for motor PWM
 *   - TIM3 for timing
 *   - ADC for voltage/current/throttle
 *   - GPIO for hall sensors and misc I/O
 *   - SysTick for timing
 */
struct EscHal {
    IUart*    uartBle;        ///< USART2: BLE dashboard
    IUart*    uartExt;        ///< USART1: External/debug
    IUart*    uartBms;        ///< USART3: BMS battery
    ITimer*   timerMotor;     ///< TIM1: Motor PWM
    ITimer*   timerGeneral;   ///< TIM3: General purpose
    IAdc*     adc;            ///< ADC1: Analog readings
    IGpio*    gpioA;          ///< GPIOA
    IGpio*    gpioB;          ///< GPIOB
    IGpio*    gpioC;          ///< GPIOC
    ISysTick* systick;        ///< System tick timer
    IWatchdog* watchdog;      ///< Independent watchdog
};

/**
 * Hardware abstraction for the BLE dashboard board.
 *
 * Peripherals:
 *   - 2 UARTs (nRF51822, ESC)
 *   - ADC for throttle/brake
 *   - GPIO for display LEDs, buttons
 *   - SysTick for timing
 */
struct BleHal {
    IUart*    uartNrf;        ///< USART1: nRF51822 BLE module
    IUart*    uartEsc;        ///< USART2: ESC mainboard
    IAdc*     adc;            ///< ADC1: Throttle, brake
    IGpio*    gpioA;          ///< GPIOA
    IGpio*    gpioB;          ///< GPIOB
    ISysTick* systick;        ///< System tick timer
    IWatchdog* watchdog;      ///< Independent watchdog
};

/**
 * Hardware abstraction for the BMS battery board.
 *
 * Peripherals:
 *   - 1 UART (ESC communication)
 *   - I2C for BQ76940 AFE
 *   - ADC for temperature sensors
 *   - GPIO for charge/discharge FET control
 *   - SysTick for timing
 */
struct BmsHal {
    IUart*    uartEsc;        ///< USART2: ESC mainboard
    II2c*     i2cAfe;         ///< I2C1: BQ76940 analog front-end
    IAdc*     adc;            ///< ADC1: Temperature NTCs
    IGpio*    gpioA;          ///< GPIOA
    IGpio*    gpioB;          ///< GPIOB
    ISysTick* systick;        ///< System tick timer
    IWatchdog* watchdog;      ///< Independent watchdog
};


} // namespace hal
} // namespace ninebot

#endif // NINEBOT_HAL_H
