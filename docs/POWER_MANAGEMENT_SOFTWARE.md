# Power Management Software Concept — Dashboard-Controlled VESC Power

## Overview

This document describes the firmware changes required on the BLE dashboard STM32 to implement the power management concept where the dashboard controls VESC power on/off. The dashboard runs on a permanent always-on 5V supply and uses ultra-low-power sleep modes when the scooter is "off."

See [POWER_MANAGEMENT_HARDWARE.md](POWER_MANAGEMENT_HARDWARE.md) for the hardware architecture and wiring.

---

## State Machine

The dashboard firmware operates in four states:

```
                    ┌──────────────────────────────────────────────────┐
                    │                                                  │
                    ▼                                                  │
              ┌──────────┐     Button press     ┌──────────────┐     │
    Power ──► │  SLEEP   │ ────────────────────► │  POWER_ON    │     │
    on        │          │     (EXTI on PB12)    │  SEQUENCE    │     │
              │ STM32    │                       │              │     │
              │ STOP mode│                       │ Enable VESC  │     │
              │ ~20µA    │                       │ Wait boot    │     │
              └──────────┘                       │ Init comms   │     │
                    ▲                             └──────┬───────┘     │
                    │                                    │             │
                    │                                    ▼             │
              ┌──────────┐     Long press        ┌──────────────┐     │
              │ POWER_OFF│ ◄──────────────────── │   ACTIVE     │     │
              │ SEQUENCE │     (3 sec hold)      │              │     │
              │          │                       │ Normal       │     │
              │ Shutdown  │                       │ operation   │     │
              │ VESC     │                       │ Ninebot prot │     │
              │ Cut power│                       │ Lisp bridge  │     │
              └──────────┘                       └──────────────┘     │
                    │                                    │             │
                    │                                    │ Error/fault │
                    └────────────────────────────────────┘─────────────┘
```

### State Definitions

| State | Description | MCU Mode | Power Draw |
|-------|-------------|----------|------------|
| `SLEEP` | Scooter is off. STM32 in STOP mode. All peripherals disabled. Only EXTI interrupt on PB12 is active. | STOP | ~20µA @ 3.3V |
| `POWER_ON_SEQUENCE` | Button pressed. MCU wakes, enables VESC power, waits for VESC boot, starts protocol. | RUN | ~40mA @ 3.3V |
| `ACTIVE` | Normal riding operation. Full Ninebot protocol, display, BLE, throttle/brake ADC. | RUN | ~80mA total |
| `POWER_OFF_SEQUENCE` | Long press detected. Graceful VESC shutdown, cut VESC power, enter SLEEP. | RUN → STOP | ~40mA → 20µA |

---

## Power-On Sequence (Button Press in SLEEP)

```
Time    Action                                      Duration
─────── ─────────────────────────────────────────── ────────
t=0     PB12 falling edge → EXTI interrupt fires
        STM32 exits STOP mode
        Reinitialize clocks (HSE, PLL → 72MHz)      ~2ms

t+2ms   Debounce: wait and verify PB12 still LOW    50ms
        If PB12 went HIGH → spurious, return to SLEEP

t+52ms  Confirmed button press
        === FOR OPTION A (MOSFET SWITCH): ===
        Set PA11 HIGH → MOSFET gate HIGH → VESC powered
        === FOR OPTION B2 (DALY BMS): ===
        Pulse PA11 LOW for 100ms → wake Daly BMS
        Wait 500ms for BMS to wake
        Send UART: enable discharge FET (0xD9 cmd)
        === FOR OPTION B1 (STOCK BMS): ===
        Send Ninebot protocol: enable discharge (custom reg 0x70)

t+100ms Initialize USART2 (PB6/PB7) for VESC comm   ~1ms
        Initialize ADC (PA0=throttle, PA1=brake)     ~1ms
        Initialize display driver                     ~1ms
        Initialize nRF51822 via USART1 (PA9/PA10)    ~1ms

t+200ms Display boot animation / power icon           200ms

t+400ms Wait for VESC boot                            ~2000ms
        (VESC boots in 1-3s depending on firmware)

t+2500  Send first Ninebot protocol heartbeat
        to VESC to establish communication

t+3000  Verify VESC responds (read FW version)
        If no response after 3 retries: show error,
        stay powered (user may need to reflash)

t+3500  Enter ACTIVE state
        Start main protocol loop (50Hz)
        Display shows speed, battery, mode
```

### Power-On — Pseudocode

```c
/* EXTI interrupt on PB12 (power button) */
void EXTI15_10_IRQHandler(void) {
    if (EXTI->PR & EXTI_PR_PR12) {
        EXTI->PR = EXTI_PR_PR12;     /* Clear pending */
        /* MCU is now awake from STOP mode */
        /* Re-init clocks will happen in main loop */
        system_state = STATE_POWER_ON;
    }
}

/* Main function after wake */
void power_on_sequence(void) {
    /* 1. Reinitialize system clocks (HSE → PLL → 72MHz) */
    SystemClock_Config();

    /* 2. Debounce button */
    delay_ms(50);
    if (gpio_read(PB12) != 0) {
        /* Spurious wake — go back to sleep */
        enter_sleep();
        return;
    }

    /* 3. Enable VESC power */
#if POWER_CONTROL == POWER_CTRL_MOSFET
    gpio_set(PA11, 1);              /* MOSFET gate HIGH → VESC on */
#elif POWER_CONTROL == POWER_CTRL_DALY_BMS
    daly_bms_wake(PA11);            /* Pulse wake pin */
    delay_ms(500);                  /* Wait for BMS wake */
    daly_bms_set_discharge(true);   /* Enable discharge FET */
#elif POWER_CONTROL == POWER_CTRL_NINEBOT_BMS
    ninebot_bms_set_discharge(true); /* Custom protocol command */
#endif

    /* 4. Wait for button release (don't trigger long-press immediately) */
    while (gpio_read(PB12) == 0) {
        delay_ms(10);
    }

    /* 5. Initialize peripherals */
    usart2_init(115200);            /* VESC UART */
    usart1_init(115200);            /* nRF51822 UART */
    adc_init();                     /* Throttle (PA0) + Brake (PA1) */
    display_init();
    display_show_boot();

    /* 6. Wait for VESC to boot */
    delay_ms(2000);

    /* 7. Verify VESC communication */
    uint8_t retries = 3;
    while (retries-- > 0) {
        if (ninebot_send_heartbeat() == OK) break;
        delay_ms(500);
    }

    /* 8. Enter active state */
    system_state = STATE_ACTIVE;
}
```

---

## Power-Off Sequence (Long Press in ACTIVE)

```
Time    Action                                      Duration
─────── ─────────────────────────────────────────── ────────
t=0     PB12 held LOW for 3 seconds (detected in main loop)

t+3s    Confirmed long press
        Beep buzzer (short confirmation tone)        100ms

t+3.1s  Display shows "powering off" animation       500ms

t+3.6s  Send Ninebot protocol shutdown to VESC       ~100ms
        (allows VESC to save config if needed)

t+3.7s  Disable display (all segments off)
        Disable LEDs

t+3.8s  === FOR OPTION A (MOSFET SWITCH): ===
        Set PA11 LOW → MOSFET gate LOW → VESC power cut
        === FOR OPTION B2 (DALY BMS): ===
        Send UART: disable discharge FET (0xD9 cmd, value=0)
        Wait 200ms for confirmation
        === FOR OPTION B1 (STOCK BMS): ===
        Send Ninebot protocol: disable discharge (custom reg 0x70)

t+4.0s  Disable all peripherals:
        - USART1 (nRF51822)
        - USART2 (VESC)
        - ADC
        - Timers
        - DMA

t+4.1s  Configure EXTI on PB12 for next wake
        Enter STM32 STOP mode

        ─── MCU is now in STOP mode (~20µA) ───
```

### Power-Off — Pseudocode

```c
void power_off_sequence(void) {
    /* 1. User feedback */
    buzzer_beep(100);               /* Short beep */
    display_show_power_off();       /* "OFF" animation */
    delay_ms(500);

    /* 2. Graceful VESC shutdown */
    ninebot_send_shutdown();        /* Optional: tell VESC to save */
    delay_ms(100);

    /* 3. Cut VESC power */
#if POWER_CONTROL == POWER_CTRL_MOSFET
    gpio_set(PA11, 0);             /* MOSFET gate LOW → VESC off */
#elif POWER_CONTROL == POWER_CTRL_DALY_BMS
    daly_bms_set_discharge(false); /* Disable discharge FET */
    delay_ms(200);
#elif POWER_CONTROL == POWER_CTRL_NINEBOT_BMS
    ninebot_bms_set_discharge(false);
#endif

    /* 4. Enter sleep */
    enter_sleep();
}

void enter_sleep(void) {
    /* Disable all peripherals */
    display_off();
    leds_off();
    adc_deinit();
    usart1_deinit();
    usart2_deinit();
    timer_deinit();
    dma_deinit();

    /* Disable nRF51822 (set to System OFF via USART1 command, if supported) */
    /* OR: assert nRF51 reset pin LOW to keep it in reset (~0.4µA) */

    /* Configure PB12 as EXTI interrupt, falling edge (button press) */
    gpio_config_input_pullup(PB12);
    exti_config_falling_edge(PB12);

    /* Ensure PA11 is LOW (MOSFET off / BMS discharge disabled) */
    gpio_set(PA11, 0);

    /* Enter STOP mode */
    /* SCB->SCR |= SCB_SCR_SLEEPDEEP; */
    /* PWR->CR |= PWR_CR_LPDS; */        /* Low-power regulator in STOP */
    /* __WFI(); */                         /* Wait For Interrupt */

    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    PWR->CR &= ~PWR_CR_PDDS;            /* STOP mode, not Standby */
    PWR->CR |= PWR_CR_LPDS;             /* Low-power regulator */
    __WFI();                              /* Enters STOP — wakes on EXTI */

    /* ─── Execution resumes here after PB12 EXTI ─── */
    power_on_sequence();
}
```

---

## Active State — Main Loop Modifications

The existing `g30_dash.lisp` runs on the VESC and handles Ninebot protocol communication. The dashboard firmware (STM32) must:

1. **Monitor PB12 button for long press** (3 seconds) → trigger power-off sequence
2. **Run standard Ninebot protocol** loop — same as current firmware
3. **Maintain VESC heartbeat** — if VESC stops responding, display error but stay powered

### Button Monitoring in Main Loop

```c
/* In main 50Hz loop (called every 20ms) */
static uint16_t button_hold_counter = 0;
#define LONG_PRESS_THRESHOLD 150     /* 150 × 20ms = 3 seconds */

void main_loop_tick(void) {
    /* ... existing Ninebot protocol handling ... */

    /* Check power button */
    if (gpio_read(PB12) == 0) {     /* Active-low */
        button_hold_counter++;
        if (button_hold_counter >= LONG_PRESS_THRESHOLD) {
            power_off_sequence();
            return;                  /* Will not return (enters SLEEP) */
        }
    } else {
        /* Short press — handle mode toggle, etc. (existing behavior) */
        if (button_hold_counter > 5 && button_hold_counter < LONG_PRESS_THRESHOLD) {
            handle_short_press();    /* Toggle speed mode, etc. */
        }
        button_hold_counter = 0;
    }
}
```

---

## STM32 STOP Mode — Technical Details

### STOP Mode Configuration

The STM32F103C8T6 STOP mode:
- All clocks stopped (HSE, HSI, PLL off)
- 1.8V voltage regulator in low-power mode
- SRAM and register contents preserved
- All I/O pins retain their state
- Wake sources: any EXTI line (including GPIO)
- Wake-up time: ~6µs (with HSI) or ~1ms (with HSE + PLL — typical)

### Clock Re-initialization After Wake

```c
void SystemClock_Config(void) {
    /* After waking from STOP, HSI is the system clock (8MHz).
     * Must reinitialize HSE and PLL to get back to 72MHz. */

    /* Enable HSE */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    /* Configure PLL: HSE × 9 = 72MHz */
    RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL);
    RCC->CFGR |= RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;

    /* Enable PLL */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    /* Switch system clock to PLL */
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}
```

### Low-Power GPIO Configuration Before Sleep

```c
void configure_gpio_for_sleep(void) {
    /* Set all unused GPIOs to analog input (lowest power) */
    /* PA0 (throttle ADC) → analog input (already is) */
    /* PA1 (brake ADC) → analog input */
    /* PA2, PA3 → analog input (or keep as UART for Daly) */
    /* PA11 → output LOW (MOSFET off) */
    /* PB12 → input pull-up (button, EXTI source) */
    /* All other pins → analog input */

    /* Disable peripheral clocks */
    RCC->APB1ENR = 0;               /* Disable USART2, timers, etc. */
    RCC->APB2ENR = RCC_APB2ENR_IOPAEN   /* Keep GPIOA for PA11 */
                 | RCC_APB2ENR_IOPBEN    /* Keep GPIOB for PB12 */
                 | RCC_APB2ENR_AFIOEN;   /* Keep AFIO for EXTI */
}
```

### EXTI Configuration for Wake

```c
void configure_exti_wake(void) {
    /* Map PB12 to EXTI12 */
    AFIO->EXTICR[3] &= ~AFIO_EXTICR4_EXTI12;
    AFIO->EXTICR[3] |= AFIO_EXTICR4_EXTI12_PB;

    /* Configure EXTI12 for falling edge (button press = LOW) */
    EXTI->FTSR |= EXTI_FTSR_TR12;       /* Falling trigger */
    EXTI->RTSR &= ~EXTI_RTSR_TR12;      /* No rising trigger */
    EXTI->IMR |= EXTI_IMR_MR12;         /* Unmask EXTI12 */
    EXTI->PR = EXTI_PR_PR12;            /* Clear any pending */

    /* Enable EXTI15_10 interrupt in NVIC */
    NVIC_EnableIRQ(EXTI15_10_IRQn);
    NVIC_SetPriority(EXTI15_10_IRQn, 0);
}
```

---

## Daly BMS UART Protocol Implementation

### UART Configuration

The dashboard uses PA2/PA3 (USART2 alternate pins or bitbang) to communicate with the Daly BMS. In the stock firmware, USART2 is on PB6/PB7 (used for VESC communication). We have two options:

1. **Use USART3** (PB10=TX, PB11=RX) — dedicated Daly UART, separate from VESC
2. **Remap USART2** to PA2/PA3 — but this conflicts with the VESC connection on PB6/PB7
3. **Bitbang UART** on PA2/PA3 — simple, works at 9600 baud

**Recommended: USART3 on PB10/PB11** (if pins are available and routable) or **bitbang on PA2/PA3**.

### Daly BMS Command Implementation

```c
/* Daly BMS protocol constants */
#define DALY_START_BYTE    0xA5
#define DALY_HOST_ADDR     0x40      /* Host → BMS */
#define DALY_DATA_LEN      0x08      /* Fixed 8-byte data field */

/* Command IDs */
#define DALY_CMD_MOSFET_STATE    0x93   /* Read MOSFET status */
#define DALY_CMD_SET_DISCHARGE   0xD9   /* Set discharge MOSFET */
#define DALY_CMD_SET_CHARGE      0xDA   /* Set charge MOSFET */
#define DALY_CMD_RESET           0x00   /* Reset BMS */

/* Checksum: sum of all bytes, truncated to uint8_t */
uint8_t daly_checksum(const uint8_t *buf, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum += buf[i];
    }
    return sum;
}

/* Send a Daly BMS command */
void daly_send_command(uint8_t cmd, uint8_t value) {
    uint8_t frame[13];
    frame[0] = DALY_START_BYTE;
    frame[1] = DALY_HOST_ADDR;
    frame[2] = cmd;
    frame[3] = DALY_DATA_LEN;
    frame[4] = value;       /* 0x01 = enable, 0x00 = disable */
    frame[5] = 0x00;
    frame[6] = 0x00;
    frame[7] = 0x00;
    frame[8] = 0x00;
    frame[9] = 0x00;
    frame[10] = 0x00;
    frame[11] = 0x00;
    frame[12] = daly_checksum(frame, 12);

    daly_uart_send(frame, 13);
}

/* Enable/disable discharge MOSFET */
void daly_bms_set_discharge(bool enable) {
    daly_send_command(DALY_CMD_SET_DISCHARGE, enable ? 0x01 : 0x00);
}

/* Wake Daly BMS from sleep via wake pin */
void daly_bms_wake(uint16_t wake_pin) {
    gpio_set(wake_pin, 0);     /* Pull LOW (active-low wake) */
    delay_ms(100);             /* Hold for 100ms */
    gpio_set(wake_pin, 1);     /* Release */
}
```

---

## Stock Ninebot BMS — Custom Protocol Command

For Option B1 (stock BMS with custom firmware), add a new Ninebot protocol register:

### Proposed Register Map Extension

| Register | R/W | Description | Data |
|----------|-----|-------------|------|
| 0x70 | W | Power control | Byte 0: 0x00=discharge OFF, 0x01=discharge ON |
| 0x71 | R | Power state | Byte 0: DSG FET state, Byte 1: CHG FET state |

### Ninebot Protocol Command

```c
/* Send power control command to BMS via Ninebot protocol */
void ninebot_bms_set_discharge(bool enable) {
    uint8_t packet[16];
    uint8_t len = 0;

    /* Header */
    packet[len++] = 0x5A;           /* Start byte 1 */
    packet[len++] = 0xA5;           /* Start byte 2 */

    /* Length (payload + src + dst + cmd) */
    packet[len++] = 0x06;           /* 3 + 1 byte data + 2 checksum */

    /* Addresses */
    packet[len++] = 0x21;           /* Source: BLE dashboard */
    packet[len++] = 0x22;           /* Destination: BMS */

    /* Command: write register */
    packet[len++] = 0x02;           /* Write command */

    /* Register + data */
    packet[len++] = 0x70;           /* Register: power control */
    packet[len++] = enable ? 0x01 : 0x00;

    /* Checksum: XOR-folded sum from byte 2 through payload */
    uint16_t sum = 0;
    for (uint8_t i = 2; i < len; i++) {
        sum += packet[i];
    }
    sum ^= 0xFFFF;
    packet[len++] = sum & 0xFF;
    packet[len++] = (sum >> 8) & 0xFF;

    uart_send(USART2, packet, len);
}
```

> **NOTE:** The stock BMS firmware does not support register 0x70. This requires custom BMS firmware that processes this command and writes to the BQ76940 `SYS_CTRL2` register via I2C. See the hardware concept document for warnings about BMS firmware modification.

---

## nRF51822 Power Control

The nRF51822 BLE SoC on the dashboard board must also be power-managed:

### Option 1: Hardware Reset (Recommended)

If the dashboard STM32 has a GPIO connected to the nRF51822 RESET pin:
- Before SLEEP: Assert nRF51 RESET LOW → nRF51 held in reset (~0.4µA)
- On POWER_ON: Release nRF51 RESET → nRF51 boots and starts BLE stack

### Option 2: UART Command

Send a "sleep" command via USART1 (PA9/PA10) to the nRF51822 firmware:
- The nRF51822 firmware must implement a `sd_power_system_off()` call
- On wake: nRF51822 can self-wake via GPIO interrupt (e.g., from the STM32)

### Option 3: Leave Running

If BLE must remain active for phone connectivity in standby (e.g., Find My Scooter):
- nRF51822 stays in low-power BLE advertising (~10µA with 5s interval)
- STM32 in STOP mode
- Phone app sends BLE command → nRF51822 asserts GPIO to STM32 → EXTI wake
- This uses ~33µW extra but adds remote wake capability

---

## Integration with VESC Lisp Script

The existing `g30_dash.lisp` handles Ninebot protocol communication on the VESC side. Power management affects the Lisp script in these ways:

### VESC Startup Behavior

When the VESC powers on, the Lisp script starts automatically (if configured as autostart):

```lisp
; Current behavior in g30_dash.lisp:
(uart-start 115200 'half-duplex)
(gpio-configure 'pin-rx 'pin-mode-in-pu)
(app-adc-detach 3 1)
```

No changes needed — the Lisp script initializes on power-up and handles the first protocol frame from the dashboard.

### VESC Shutdown Handling

The VESC does not need a graceful shutdown command — it is designed for hard power cuts. However, if VESC configuration needs to be saved:

```lisp
; Optional: save config on shutdown signal
; (called when dashboard sends a shutdown frame before cutting power)
(defun handle-shutdown ()
    (conf-store)                     ; Save motor config to flash
    (uart-write (list 0x5A 0xA5 ... ACK)) ; Acknowledge
)
```

### Timeout Detection

If the VESC Lisp script stops receiving protocol frames from the dashboard (e.g., dashboard crashed):

```lisp
; Add to g30_dash.lisp:
(def watchdog-counter 0)
(def watchdog-timeout 100)          ; 100 × 20ms = 2 seconds

(defun handle-watchdog ()
    (if (> watchdog-counter watchdog-timeout)
        (progn
            (set-current 0)          ; Cut motor current
            (set-brake 0)            ; Release brake
            ; Motor will coast to a stop
        )
    )
    (setq watchdog-counter (+ watchdog-counter 1))
)

; Reset watchdog when a valid frame is received
(defun handle-rx-frame (frame)
    (setq watchdog-counter 0)
    ; ... existing frame handling ...
)
```

---

## Error Handling

### VESC No-Response Error

```c
void handle_vesc_no_response(void) {
    /* VESC did not respond after power-on sequence */
    /* Possible causes: VESC hardware failure, wrong UART config, firmware issue */

    display_show_error(ERR_VESC_NO_COMM);  /* Show "E01" on display */
    buzzer_beep_pattern(3, 200, 200);      /* 3 short beeps */

    /* Stay powered for 30 seconds to allow user to diagnose */
    /* Then auto-sleep to conserve power */
    uint16_t timeout = 1500;               /* 1500 × 20ms = 30s */
    while (timeout-- > 0) {
        delay_ms(20);
        if (gpio_read(PB12) == 0) {        /* Button press = retry */
            delay_ms(50);                   /* Debounce */
            if (gpio_read(PB12) == 0) {
                power_on_sequence();        /* Retry */
                return;
            }
        }
    }
    power_off_sequence();                   /* Auto-sleep */
}
```

### BMS Communication Error (Option B)

```c
void handle_bms_comm_error(void) {
    /* BMS did not respond to discharge FET command */
    /* For Daly: BMS might still be asleep despite wake pulse */

#if POWER_CONTROL == POWER_CTRL_DALY_BMS
    /* Retry wake sequence */
    for (uint8_t retry = 0; retry < 3; retry++) {
        daly_bms_wake(PA11);
        delay_ms(1000);                    /* Longer wait */
        daly_bms_set_discharge(true);
        delay_ms(200);
        if (daly_bms_read_mosfet_state() & 0x01) {
            return;                         /* Success */
        }
    }
#endif

    display_show_error(ERR_BMS_NO_COMM);   /* Show "E02" */
    buzzer_beep_pattern(5, 100, 100);      /* 5 rapid beeps */
    /* Cannot power on VESC without BMS → go back to sleep */
    delay_ms(3000);
    enter_sleep();
}
```

---

## Firmware Build Configuration

### Preprocessor Defines

```c
/* Power control method — set in CMakeLists.txt or compiler flags */
#define POWER_CTRL_NONE         0    /* No power management (stock behavior) */
#define POWER_CTRL_MOSFET       1    /* Option A: external MOSFET switch */
#define POWER_CTRL_DALY_BMS     2    /* Option B2: Daly BMS UART FET control */
#define POWER_CTRL_NINEBOT_BMS  3    /* Option B1: Custom Ninebot BMS FET control */

/* Select one: */
#ifndef POWER_CONTROL
#define POWER_CONTROL  POWER_CTRL_MOSFET    /* Default: MOSFET switch */
#endif

/* GPIO assignments (adjust if different pins used) */
#define PIN_POWER_CONTROL   PA11    /* MOSFET gate / BMS wake pin */
#define PIN_POWER_BUTTON    PB12    /* Power button (active-low) */
#define PIN_BMS_UART_TX     PA2     /* Daly BMS UART TX (if Option B2) */
#define PIN_BMS_UART_RX     PA3     /* Daly BMS UART RX (if Option B2) */

/* Timing constants */
#define BUTTON_DEBOUNCE_MS     50
#define LONG_PRESS_MS          3000
#define VESC_BOOT_WAIT_MS      2000
#define VESC_COMM_TIMEOUT_MS   1500
#define BMS_WAKE_PULSE_MS      100
#define BMS_WAKE_WAIT_MS       500
#define AUTO_SLEEP_TIMEOUT_MS  30000
```

### CMake Integration

```cmake
# In firmware/decompiled/CMakeLists.txt, add:
option(POWER_MANAGEMENT "Enable power management (MOSFET/DALY/NINEBOT)" "MOSFET")

if(POWER_MANAGEMENT STREQUAL "MOSFET")
    target_compile_definitions(${TARGET} PRIVATE POWER_CONTROL=1)
elseif(POWER_MANAGEMENT STREQUAL "DALY")
    target_compile_definitions(${TARGET} PRIVATE POWER_CONTROL=2)
elseif(POWER_MANAGEMENT STREQUAL "NINEBOT")
    target_compile_definitions(${TARGET} PRIVATE POWER_CONTROL=3)
else()
    target_compile_definitions(${TARGET} PRIVATE POWER_CONTROL=0)
endif()
```

---

## Implementation Priority

| Priority | Task | Complexity | Dependencies |
|----------|------|------------|--------------|
| 1 | STM32 STOP mode + EXTI wake on PB12 | Low | None |
| 2 | PA11 GPIO toggle (MOSFET switch control) | Low | MOSFET hardware installed |
| 3 | Button long-press detection in main loop | Low | None |
| 4 | Power-on sequence (clock reinit, peripheral init, VESC wait) | Medium | Tasks 1-3 |
| 5 | Power-off sequence (shutdown, cut power, sleep) | Medium | Tasks 1-3 |
| 6 | Error handling (VESC no-response, auto-sleep) | Medium | Tasks 4-5 |
| 7 | Daly BMS UART driver (bitbang or USART3 on PA2/PA3) | Medium | Daly BMS available |
| 8 | Daly BMS wake pin control | Low | Task 7 |
| 9 | Daly BMS FET commands (0xD9, 0xDA) | Low | Tasks 7-8 |
| 10 | nRF51822 sleep/wake coordination | Medium | nRF51 custom firmware |
| 11 | VESC Lisp watchdog addition | Low | None |
| 12 | Custom Ninebot BMS firmware (register 0x70) | **High** | BMS firmware development (risky) |

---

## Testing Plan

| Test | Method | Pass Criteria |
|------|--------|---------------|
| STOP mode current | Measure with µA meter on 3.3V rail | ≤ 25 µA |
| EXTI wake | Press PB12, measure wake time on oscilloscope | < 5 ms to first GPIO toggle |
| MOSFET control | Measure VESC B+ with multimeter | 0V when PA11 LOW, battery V when HIGH |
| Power-on sequence | UART monitor at 115200 | Boot messages within 3.5s of button press |
| Power-off sequence | UART monitor + multimeter on VESC B+ | VESC power cut within 1s of long press |
| Daly BMS FET control | Multimeter on Daly P+ terminal | Voltage appears/disappears on command |
| Daly BMS wake from sleep | Let BMS sleep, then press button | BMS wakes and enables discharge within 1s |
| Long press detection | Press button 3s in ACTIVE state | Power-off sequence triggers |
| Short press detection | Quick press in ACTIVE state | Mode toggle (no power-off) |
| VESC no-response | Power on with VESC disconnected | Error displayed, auto-sleep after 30s |
| Battery drain test | Leave in SLEEP for 24h, measure voltage drop | < 0.01V drop (< 2 Wh consumed) |
