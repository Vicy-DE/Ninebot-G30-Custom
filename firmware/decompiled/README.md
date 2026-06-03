# Ninebot G30 Max — Decompiled Firmware (C++)

Decompiled C++ reconstruction of the three Ninebot G30 Max firmware images, with a host-based simulator and full test suite for behavioral equivalence validation.

| Board | Firmware | Original Binary | MCU | Status |
|-------|----------|-----------------|-----|--------|
| ESC (Motor Controller) | DRV 1.6.13 | 33,388 bytes | STM32F103CBT6 (128KB) | ✅ Decompiled + Tested |
| BLE (Dashboard) | BLE 1.1.7 | — | STM32F103C8T6 (64KB) + nRF51822 | ✅ Decompiled + Tested |
| BMS (Battery) | BMS 1.7.4.5 | — | STM32F103C8T6 (64KB) + BQ76940 | ✅ Decompiled + Tested |

## Architecture

```
Phone App ←BLE→ [BLE Dashboard] ←UART→ [ESC Motor Controller] ←UART→ [BMS Battery]
                 nRF51822 +              STM32F103CBT6              STM32F103C8T6 +
                 STM32F103C8T6                                      BQ76940

Communication: Ninebot protocol (5A A5 header, 115200 baud 8N1)
Addresses: ESC=0x20, BLE=0x21, BMS=0x22, App=0x3E, PC=0x3F
```

## Directory Structure

```
firmware-decompiled/
├── CMakeLists.txt              # Build system (CMake 3.16+, C++17)
├── README.md                   # This file
├── common/include/
│   ├── hal.h                   # Hardware Abstraction Layer interfaces
│   ├── protocol.h              # Ninebot serial protocol (parser + builder)
│   └── registers.h             # Register file storage + address definitions
├── esc/
│   ├── include/esc_firmware.h  # ESC class declaration + motor constants
│   └── src/
│       ├── esc_main.cpp        # Initialization, main loop, SysTick, ISRs
│       ├── esc_motor.cpp       # Motor commutation, speed PID, hall decoding
│       └── esc_protocol.cpp    # Packet dispatch, register R/W, BMS queries
├── ble/
│   ├── include/ble_firmware.h  # BLE class declaration + dashboard constants
│   └── src/ble_main.cpp        # Throttle/brake ADC, button, LEDs, protocol relay
├── bms/
│   ├── include/bms_firmware.h  # BMS class declaration + BQ76940 constants
│   └── src/bms_main.cpp        # Cell monitoring, SOC, protection, balancing
├── simulator/include/
│   ├── sim_hal.h               # Simulated peripherals (UART, GPIO, ADC, I2C, etc.)
│   └── sim_bus.h               # Full 3-board simulation bus with wired UARTs
└── tests/
    ├── test_framework.h        # Minimal xUnit test runner (macros + auto-register)
    ├── test_main.cpp           # Entry point
    ├── test_protocol.cpp       # Protocol checksum, build, parse, TX queue (11 tests)
    ├── test_esc.cpp            # ESC registers, modes, lock, errors, protocol (15 tests)
    ├── test_ble.cpp            # Throttle/brake ADC, buttons, LEDs, relay (14 tests)
    ├── test_bms.cpp            # Cell voltages, SOC, protection, balancing (17 tests)
    ├── test_bus_integration.cpp        # Full bus end-to-end (12 tests)
    └── test_binary_equivalence.cpp     # Behavioral equivalence vs stock (17 tests)
```

## Building

### Prerequisites

- CMake 3.16+
- C++17 compiler (GCC 10+, Clang 11+, MSVC 2019+)
- Ninja (recommended) or Make

### Build Commands

```bash
mkdir build && cd build
cmake .. -G Ninja
cmake --build .
```

### Run Tests

```bash
# Via CTest
ctest --output-on-failure

# Direct execution (verbose output)
./firmware_tests
```

**Current test results: 134 tests, 134 passed, 0 failed** (g++ 15.2; includes nRF51822 + binary-equivalence). Plus the standalone byte-verified decompilation test `tests/test_decompiled_protocol.cpp` (35/35) — see `DECOMPILATION.md`.

## Decompiled Firmware Details

### ESC Motor Controller (DRV 1.6.13)

The ESC is the central hub. Key functions with original firmware addresses:

| Function | Address | Description |
|----------|---------|-------------|
| `Reset_Handler` | `0x08001100` | Entry point → `main()` |
| `SysTick_Handler` | `0x08005C54` | 1ms tick ISR |
| `TIM1_UP_IRQHandler` | `0x08005E74` | Motor commutation ISR (16 kHz) |
| `calculateChecksum` | `0x08002720` | Protocol checksum (~sum) |
| `buildPacket` | `0x080036AC` | Packet constructor |
| `parseProtocolByte` | `0x08007128` | Protocol state machine |
| `dispatchReceivedPacket` | `0x08005468` | Command router |

**Motor control:**
- 3-phase BLDC, 15 pole pairs, 6-step commutation
- TIM1 center-aligned PWM, ARR=2250 (~16 kHz at 72 MHz)
- Hall sensors on PB5/PB6/PB7
- PI speed controller (Kp=8, Ki=1, 10 Hz update rate)

**Riding modes:**
| Mode | Speed Limit | Current Limit |
|------|-------------|---------------|
| Eco (0) | 20 km/h | 15 A |
| D (1) | 25 km/h | 20 A |
| Sport (2) | 30 km/h | 30 A |

### BLE Dashboard (BLE 1.1.7)

Protocol bridge between phone app (via nRF51822) and ESC:

- **Throttle:** ADC ch0 (PA0), range 400–3400, 50-count deadband
- **Brake:** ADC ch1 (PA1), range 500–3200, overrides throttle
- **Button:** PB12 active-low, short press cycles mode, long press (2s) power off
- **LEDs:** PB0 power, PB1 BLE, PB2–PB4 mode (Eco/D/Sport), PB5 error
- **Protocol:** Forwards non-BLE packets between nRF (USART1) and ESC (USART2)

### BMS Battery Management (BMS 1.7.4.5)

10S 3P lithium-ion pack (36V nominal, 551 Wh, 15.3 Ah):

- **AFE:** BQ76940 at I2C address 0x08
- **Cell monitoring:** 14-bit ADC, 250ms read interval, gain-calibrated
- **SOC:** Voltage-based lookup: 4.20V→100%, 3.90V→80%, 3.70V→50%, 3.50V→20%, 2.75V→0%
- **Protection thresholds:**

| Protection | Threshold | Action |
|-----------|-----------|--------|
| Cell OVP | > 4.200 V | Disable charge FET |
| Cell UVP | < 2.750 V | Disable discharge FET |
| OCP | > 30 A | Disable discharge FET |
| OTP | > 60°C | Disable both FETs |
| UTP | < −20°C | Disable charge FET |

- **Balancing:** Passive, 5s interval, activates when cells > 4100 mV with > 30 mV imbalance

## Ninebot Protocol

Packet format: `[0x5A][0xA5][LEN][SRC][DST][CMD][ARG][PAYLOAD...][CHK_LO][CHK_HI]`

- **LEN** = payload byte count (firmware-verified; full frame = LEN + 9). See `DECOMPILATION.md`.
- **Checksum** = bitwise NOT of sum of bytes from LEN through end of payload (`~Σ & 0xFFFF`)
- **Commands:** READ (0x01), WRITE (0x02 / 0x03) — confirmed from the DRV_1.6.13 CMD jump table

## Simulator

The simulator provides host-PC implementations of all hardware interfaces:

| Component | Simulated Behavior |
|-----------|-------------------|
| `SimUart` | TX buffer capture, RX queue, bidirectional cross-linking |
| `SimGpio` | 16-bit IDR/ODR registers, per-pin control |
| `SimAdc` | Configurable per-channel return values |
| `SimI2c` | Full BQ76940 register file with cell voltage/temperature injection |
| `SimTimer` | Compare register tracking, counter simulation |
| `SimWatchdog` | Feed counter for verification |
| `SimulationBus` | Complete 3-board wired system with ISR-based byte routing |

## Test Suite

134 tests covering all firmware behaviors:

- **Protocol:** Checksum calculation, packet build/parse, TX queue, round-trip
- **ESC:** Register defaults, riding modes, lock, error bits, protocol R/W, watchdog, uptime
- **BLE:** Throttle/brake ADC conversion, deadband, button debounce, LED states, protocol relay
- **BMS:** Cell voltage reading, SOC estimation, OVP/UVP/OCP/OTP/UTP protection, cell balancing
- **Bus integration:** End-to-end app→BLE→ESC→BMS communication, throttle control, BMS polling
- **Binary equivalence:** Known checksum vectors, packet format, register defaults, speed limits, motor constants, device addresses

## Safety Notes

⚠️ **BMS modifications can cause battery fires.** Never bypass undervoltage or overcurrent protection. The battery pack stores 551 Wh of energy — handle with care. Always verify firmware checksums before flashing. Keep stock firmware backups.
