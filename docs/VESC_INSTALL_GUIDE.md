# VESC Installation & Flashing Guide — Ninebot G30 Max

## Overview

This guide covers everything needed to replace the stock ESC motor controller with a **VESC** (Benjamin Vedder's Electronic Speed Controller) in the Ninebot G30 Max. It includes exact wiring, cable colors, connector pinouts, VESC Tool installation, firmware flashing, and Lisp script upload.

The VESC replaces the stock ESC board entirely. The BLE dashboard and BMS battery boards keep their original hardware but communicate with the VESC using the Ninebot protocol via UART.

### System Architecture After VESC Install

```
                                    ┌───────────────────────┐
                                    │    BLE Dashboard      │
                                    │    STM32F103C8T6      │
     Phone App ◄──── BLE ─────────►│    + nRF51822         │
                                    │                       │
                                    │  PB6 (TX) ──┐        │
                                    │  PB7 (RX) ──┤        │
                                    └─────────────┼────────┘
                                                  │ UART 115200 8N1
                           4-pin Dashboard Cable  │ Half-duplex
                            (Red/Black/Green/White)│
                                                  │
                                    ┌─────────────┼────────┐
                                    │  VESC       │        │
                                    │  Motor Controller    │
     USB (PC) ◄──── USB ──────────►│                       │
                                    │  COMM Port ─┘        │
                                    │                       │
                                    │  Phase A ─────────── Motor Yellow
                                    │  Phase B ─────────── Motor Green
                                    │  Phase C ─────────── Motor Blue
                                    │  Hall Port ────────── Hall 5-pin
                                    │                       │
                                    │  B+ ─────────── Battery +
                                    │  B- ─────────── Battery -
                                    │                       │
                                    │  UART2 / AUX ─┐      │
                                    └───────────────┼──────┘
                                                    │ UART 115200 8N1
                            4-pin BMS Cable         │ Full-duplex
                            (Red/Black/Yellow/Green) │
                                                    │
                                    ┌───────────────┼──────┐
                                    │  BMS Battery  │      │
                                    │  STM32F103C8T6       │
                                    │  + BQ76940           │
                                    │  PA2 (TX) ────┘      │
                                    │  PA3 (RX)            │
                                    └──────────────────────┘
```

---

## 1. Required Hardware

| Item | Description | Notes |
|------|-------------|-------|
| **VESC controller** | VESC 6+ compatible (e.g., Flipsky FSESC 75100, 75200, or VESC 6.7) | Must support 36V–42V input, Lisp scripting (FW 6.x+) |
| **USB cable** | USB-A to USB-B / USB-C (depends on VESC model) | For VESC Tool connection |
| **Motor phase connectors** | MT30 or MT60 bullet connectors (or solder directly) | Match your VESC's phase wire gauge |
| **JST-PH connectors** | 2.0mm pitch JST-PH crimps and housings | For COMM port and hall sensor wiring |
| **Heat shrink tubing** | 3:1 ratio, assorted sizes | For insulating solder joints |
| **Multimeter** | For verifying connections | Essential before powering on |
| **Soldering iron** | For connector modification | Temperature-controlled recommended |

### VESC Requirements

| Parameter | Minimum | Recommended |
|-----------|---------|-------------|
| Voltage rating | 42V (10S Li-ion) | 60V+ (headroom for regen spikes) |
| Continuous current | 20A | 40A+ |
| Firmware | 6.00+ | Latest 6.x stable |
| Features | UART, Lisp scripting | USB, UART, Lisp, ADC, Hall sensors |

---

## 2. G30 Max Scooter Cable Identification

### Internal Cable Harness Overview

The G30 Max has a wiring harness running through the deck and up the stem. When you open the ESC compartment (underside of the deck), you will find these connectors coming from the stock ESC board:

```
Stock ESC Board (to be removed)
┌──────────────────────────────────────────────────────┐
│                                                      │
│  [4-pin]──── Dashboard cable (up the stem)           │
│  [4-pin]──── BMS cable (to battery compartment)      │
│  [3-pin]──── Motor phase wires (Yellow/Green/Blue)   │
│  [5-pin]──── Hall sensor cable (from motor)          │
│  [2-pin]──── Battery power B+/B- (thick wires)       │
│  [2-pin]──── Charge port (if separate)               │
│  [1-pin]──── Brake light signal (to tail light)      │
│                                                      │
└──────────────────────────────────────────────────────┘
```

---

## 3. Wiring — Dashboard to VESC (CRITICAL)

### 3.1 Dashboard Cable (4-Pin Connector)

This cable runs from the ESC compartment up through the stem to the BLE dashboard board. It carries power and the half-duplex UART data bus.

#### Wire Colors and Pinout

| Pin | Wire Color | Signal | Voltage | Description |
|-----|------------|--------|---------|-------------|
| 1 | **Red** | **+5V** | 5.0V DC | Power supply to dashboard (from ESC/VESC buck converter) |
| 2 | **Black** | **GND** | 0V | Ground reference |
| 3 | **Green** | **UART TX** | 3.3V TTL | Data: ESC/VESC → Dashboard (PB7 on BLE STM32) |
| 4 | **White** | **UART RX** | 3.3V TTL | Data: Dashboard → ESC/VESC (PB6 on BLE STM32) |

> **⚠️ VERIFY ON YOUR SCOOTER:** Wire colors can vary between production batches and revisions. Before connecting to the VESC, **use a multimeter** to identify each wire:
> - **+5V**: Continuity to the 5V rail on the dashboard PCB
> - **GND**: Continuity to any ground pad on the dashboard PCB
> - **TX/RX**: Continuity to PB6 (pin 42) and PB7 (pin 43) on the BLE STM32

#### Half-Duplex UART — How It Works

The Ninebot protocol between the dashboard and ESC is **half-duplex**: only one device transmits at a time. The stock ESC uses a single MCU pin (PA2) in STM32 UART half-duplex mode.

On the VESC side, the Lisp script configures half-duplex mode:

```lisp
(uart-start 115200 'half-duplex)   ; ← from g30_dash.lisp line 48
(gpio-configure 'pin-rx 'pin-mode-in-pu)  ; RX pin as pull-up input (button detect)
```

This means on the VESC **COMM port**:
- **TX pin** handles both transmit and receive (bidirectional, half-duplex)
- **RX pin** is repurposed as a GPIO input for the dashboard power button detection

#### VESC COMM Port Wiring

Connect the dashboard cable to the VESC's **COMM port** (usually a JST-PH connector):

```
Dashboard Cable              VESC COMM Port
(from stem)                  (JST-PH header)
┌──────────┐                ┌──────────────┐
│ Red  (+5V)├───────────────┤ 5V           │
│ Black(GND)├───────────────┤ GND          │
│ Green(TX) ├───────┐       │              │
│ White(RX) ├───┐   │       │              │
└──────────┘    │   │       │              │
                │   └───────┤ TX  (half-duplex data) │
                └───────────┤ RX  (button input)     │
                            └──────────────┘
```

**Connection detail:**

| Dashboard Wire | → Connects to → | VESC COMM Pin | Reason |
|----------------|-----------------|---------------|--------|
| Red (+5V) | → | 5V | Powers the dashboard from VESC's 5V rail |
| Black (GND) | → | GND | Common ground reference |
| Green (ESC→BLE data) | → | TX | VESC transmit in half-duplex mode = bidirectional bus |
| White (BLE→ESC data) | → | RX | Button detection via `(gpio-read 'pin-rx)` in Lisp |

> **NOTE:** In half-duplex mode the VESC TX pin is the main bidirectional data line. The Green and White wires from the dashboard are typically bridged on the original ESC (both connect to the single half-duplex bus). On the VESC, the script uses TX for data and RX for button detection. If your scooter has the TX and RX lines bridged inside the connector, you may need to split them.

#### Verification Against the Lisp Script

The `g30_dash.lisp` script confirms this wiring:

| Script Line | Code | Confirms |
|-------------|------|----------|
| 48 | `(uart-start 115200 'half-duplex)` | 115200 baud, half-duplex on TX pin |
| 49 | `(gpio-configure 'pin-rx 'pin-mode-in-pu)` | RX pin = pull-up input for button |
| 50 | `(app-adc-detach 3 1)` | No hardwired ADC — throttle comes via protocol |
| 53 | `(bufset-u16 tx-frame 0 0x5AA5)` | Ninebot protocol header (0x5A 0xA5) |
| 54 | `(bufset-u8 tx-frame 2 0x06)` | Payload length 6 bytes |
| 55 | `(bufset-u16 tx-frame 3 0x2021)` | Source 0x20 (ESC), Dest 0x21 (BLE) |
| 56 | `(bufset-u16 tx-frame 5 0x6400)` | Frame code 0x64 (display update) |

The script reads **frame 0x65** (throttle/brake from dashboard) and sends **frame 0x64** (display update to dashboard). This matches the Ninebot protocol where:
- Dashboard sends throttle byte (0–255) and brake byte (0–255) to the ESC
- ESC sends speed mode, battery %, light, beep, speed, and error code to the dashboard

---

### 3.2 BMS Cable (4-Pin Connector)

This cable runs from the ESC compartment to the battery compartment. The stock ESC communicates with the BMS via **full-duplex UART** (separate TX and RX lines).

#### Wire Colors and Pinout

| Pin | Wire Color | Signal | Voltage | Description |
|-----|------------|--------|---------|-------------|
| 1 | **Red** | **+5V** | 5.0V DC | Power / signal reference |
| 2 | **Black** | **GND** | 0V | Ground reference |
| 3 | **Yellow** | **UART TX** | 3.3V TTL | Data: ESC/VESC → BMS (PA3 on BMS STM32) |
| 4 | **Green** | **UART RX** | 3.3V TTL | Data: BMS → ESC/VESC (PA2 on BMS STM32) |

> **⚠️ VERIFY:** Use a multimeter to confirm. Trace the wires to the BMS board's PA2 (USART2_TX, pin 12) and PA3 (USART2_RX, pin 13).

#### VESC BMS Connection

If your VESC has a second UART port (AUX / UART2), connect the BMS cable there:

| BMS Wire | → Connects to → | VESC AUX UART Pin |
|----------|-----------------|-------------------|
| Red (+5V) | → | 5V (or leave unconnected if BMS is self-powered) |
| Black (GND) | → | GND |
| Yellow (ESC TX) | → | TX |
| Green (BMS TX) | → | RX |

> **NOTE:** The `g30_dash.lisp` script does **not** handle BMS communication. The VESC gets battery voltage directly from its own voltage divider (main power input). Battery percentage shown on the dashboard comes from `(get-batt)` — the VESC's internal battery estimation, not the BMS. If you need BMS cell-level data on the dashboard, additional Lisp code or custom BLE firmware is required.

---

### 3.3 Motor Phase Wires (3 Thick Wires)

The G30 Max motor has three phase wires. These are the thick power cables (typically 12–14 AWG silicone wire).

#### Wire Colors and Pinout

| Motor Wire Color | Signal | → Connect to VESC |
|------------------|--------|-------------------|
| **Yellow** | Phase A | → **VESC Phase A (U)** |
| **Green** | Phase B | → **VESC Phase B (V)** |
| **Blue** | Phase C | → **VESC Phase C (W)** |

> **⚠️ IMPORTANT:** Phase order determines motor direction. If the motor spins backwards after setup, **swap any two phase wires** (e.g., swap Green ↔ Blue). Do NOT change the wiring after running the motor detection wizard — instead re-run detection.

#### Connection Method

| Method | Pros | Cons |
|--------|------|------|
| **MT30/MT60 bullet connectors** | Clean, removable | Requires crimping tool |
| **Direct solder** | Most reliable connection | Permanent, harder to modify |
| **XT60 connector** | Easy to find, robust | Bulky, not standard for motor phase |

1. Strip 5mm of insulation from each motor phase wire
2. Tin the exposed copper with solder
3. Solder or crimp to matching VESC phase output connectors
4. Heat-shrink each joint individually
5. Heat-shrink all three together for strain relief

---

### 3.4 Hall Sensor Cable (5-Pin Connector)

The G30 Max motor uses three Hall effect sensors for rotor position feedback. These connect to the VESC's Hall sensor port (typically a JST-PH 6-pin header).

#### Wire Colors and Pinout

| Motor Hall Wire Color | Signal | → VESC Hall Port Pin |
|-----------------------|--------|---------------------|
| **Red** | +5V | → **5V** |
| **Yellow** | Hall A | → **H1** |
| **Green** | Hall B | → **H2** |
| **Blue** | Hall C | → **H3** |
| **Black** | GND | → **GND** |

#### VESC Hall Sensor Port (Typical 6-Pin JST-PH)

```
VESC Hall Sensor Port (JST-PH 6-pin):
┌─────────────────────────────────┐
│ 5V │ H1 │ H2 │ H3 │ TEMP │ GND │
└──┬───┬───┬───┬───┬──────┬──────┘
   │   │   │   │   │      │
   │   │   │   │   NC     │
   ↑   ↑   ↑   ↑         ↑
  Red  Yel Grn Blu       Blk
  (from G30 motor hall connector)
```

| VESC Pin | Motor Wire | Notes |
|----------|------------|-------|
| 5V | Red | 5V supply to Hall sensors |
| H1 | Yellow | Hall sensor A signal |
| H2 | Green | Hall sensor B signal |
| H3 | Blue | Hall sensor C signal |
| TEMP | — | Leave unconnected (G30 motor has no temp sensor) |
| GND | Black | Ground reference |

> **⚠️ VERIFY:** Hall sensor order may need swapping if the VESC reports bad hall sensor sequence during motor detection. The VESC Tool will tell you if the sequence is wrong.

---

### 3.5 Battery Power Cables (2 Thick Wires)

The main battery power cables are the thickest wires in the harness (typically 10–12 AWG).

| Wire Color | Signal | Voltage | → VESC |
|------------|--------|---------|--------|
| **Red (thick)** | B+ (battery positive) | 30V–42V | → **VESC B+ / VIN+** |
| **Black (thick)** | B- (battery negative) | 0V (reference) | → **VESC B- / VIN-** |

> **⚠️ DANGER — HIGH VOLTAGE:** The G30 Max battery pack is 36V nominal (42V fully charged), 10S3P Li-ion, 551 Wh. This voltage is **lethal** under fault conditions. Always:
> - Disconnect the battery before any wiring work
> - Use appropriately rated connectors (XT60 or Anderson Powerpole)
> - Double-check polarity with a multimeter before connecting
> - Install an inline fuse or circuit breaker (40A recommended)
> - Never short B+ to B-

---

## 4. Complete Wiring Summary

```
                    VESC Motor Controller
            ┌─────────────────────────────────────┐
            │                                     │
  ┌─────────┤ B+ ◄──── Red (thick) ──── Battery B+
  │         │ B- ◄──── Black (thick) ── Battery B-
  │         │                                     │
  │         │ Phase A ◄── Yellow ─── Motor Phase A│
  │         │ Phase B ◄── Green ──── Motor Phase B│
  │         │ Phase C ◄── Blue ───── Motor Phase C│
  │         │                                     │
  │         │ Hall 5V ◄── Red ────── Motor Hall   │
  │         │ Hall H1 ◄── Yellow ─── Motor Hall   │
  │         │ Hall H2 ◄── Green ──── Motor Hall   │
  │         │ Hall H3 ◄── Blue ───── Motor Hall   │
  │         │ Hall GND◄── Black ──── Motor Hall   │
  │         │                                     │
  │         │ COMM 5V ◄── Red ────── Dashboard +5V│
  │         │ COMM GND◄── Black ──── Dashboard GND│
  │         │ COMM TX ◄── Green ──── Dashboard Data│
  │         │ COMM RX ◄── White ──── Dashboard Btn│
  │         │                                     │
  │         │ AUX TX ◄─── Yellow ─── BMS TX      │
  │         │ AUX RX ◄─── Green ──── BMS RX      │
  │         │ AUX GND ◄── Black ──── BMS GND     │
  │         │                                     │
  │  ┌──────┤ USB ◄──── USB Cable ──── PC         │
  │  │      └─────────────────────────────────────┘
  │  │
  │  └─► For VESC Tool connection, firmware flash, configuration
  │
  └────► Main power (36-42V DC from battery via BMS)
```

---

## 5. Pre-Power-On Checklist

Before applying any power, verify **every** connection:

- [ ] **Battery disconnected** during all wiring work
- [ ] **Multimeter continuity test**: no shorts between B+ and B-, between B+ and GND, between 5V and GND
- [ ] **Phase wires**: each phase wire goes to exactly one VESC phase output, no shorts between phases
- [ ] **Hall sensors**: 5V and GND correct polarity, H1/H2/H3 connected (order can be fixed in software)
- [ ] **Dashboard COMM**: 5V (Red) → VESC 5V, GND (Black) → VESC GND, Data and Button wires correct
- [ ] **BMS (if connected)**: TX/RX not swapped, GND connected
- [ ] **No exposed conductors**: all joints insulated with heat shrink
- [ ] **Connectors seated fully**: no half-inserted JST connectors
- [ ] **VESC USB connected to PC**: ready to configure before first motor spin

---

## 6. VESC Tool — Download & Installation

### 6.1 Download VESC Tool

VESC Tool is available from the official VESC Project website. It is free for all platforms except iOS.

| Platform | Download Source | Price |
|----------|----------------|-------|
| **Windows** | [vesc-project.com/vesc_tool](https://vesc-project.com/vesc_tool) | Free (€0.00) |
| **Linux** | [vesc-project.com/vesc_tool](https://vesc-project.com/vesc_tool) | Free (€0.00) |
| **macOS** | [vesc-project.com/vesc_tool](https://vesc-project.com/vesc_tool) | Free (€0.00) |
| **Android** | [Google Play Store](https://play.google.com/store/apps/details?id=vedder.vesctool) | Free |
| **iOS** | [Apple App Store](https://apps.apple.com/us/app/vesc-tool/id1605488891) | Paid |

**Steps to download (Windows):**

1. Go to [https://vesc-project.com/vesc_tool](https://vesc-project.com/vesc_tool)
2. Scroll down to **"VESC Tool Free"** (€0.00)
3. Click **"Add to cart"** → complete the free checkout (requires account)
4. After checkout, download the Windows installer (`.exe`)
5. Run the installer and follow the prompts
6. Launch VESC Tool from the Start Menu or desktop shortcut

> **Paid tiers** (Bronze €5, Silver €10, Gold €15, Platinum €20) add a donation badge to your VESC Project profile. The software functionality is identical across all tiers.

#### Alternative: Build from Source

```powershell
# Clone the repository
git clone https://github.com/vedderb/vesc_tool.git
cd vesc_tool

# Build (requires Qt 5.15+ and qmake)
qmake -config release "CONFIG += release_lin build_original exclude_fw"
make -j8
```

### 6.2 Install USB Drivers

Most VESC controllers use an STM32 USB CDC (virtual COM port) which should be recognized automatically on Windows 10/11. If not:

1. Open **Device Manager**
2. Look for an unrecognized device under "Other devices"
3. Install the **STMicroelectronics Virtual COM Port** driver:
   - Download from [ST.com VCP drivers](https://www.st.com/en/development-tools/stsw-stm32102.html)
   - Or let Windows Update find the driver automatically

### 6.3 Connect VESC to PC

1. Connect the VESC to your PC via USB cable
2. **Do not connect the battery yet** — USB power is sufficient for configuration
3. Open VESC Tool
4. Click the **Connect** button (plug icon) in the top-left toolbar
5. Select the correct serial port (e.g., `COM3`, `/dev/ttyACM0`)
6. Click **Connect**

You should see the VESC firmware version in the bottom status bar.

---

## 7. VESC Firmware — Flash / Update

### 7.1 Check Current Firmware

1. Connect to the VESC in VESC Tool
2. Go to **Firmware** tab (left sidebar, chip icon)
3. The current firmware version is displayed at the top
4. The Lisp script requires **firmware 6.00 or newer**

### 7.2 Update Firmware

If the firmware is outdated or needs updating:

1. Go to **Firmware** tab
2. Select the **"Included Files"** sub-tab
3. Select your VESC hardware from the dropdown:
   - Choose the exact hardware version (e.g., "VESC 6.7", "75/100", etc.)
   - **If unsure, click "Detect Hardware"** — VESC Tool will identify it
4. Select the firmware version:
   - Use **"Latest Stable"** for production use
   - Use **"Latest Beta"** only if you need cutting-edge features
5. Click **"Upload"**
6. **DO NOT disconnect USB or power during the flash process**
7. Wait for the progress bar to complete (~30 seconds)
8. The VESC will reboot automatically
9. Reconnect in VESC Tool and verify the new firmware version

> **⚠️ WARNING:** Flashing wrong firmware for your hardware can brick the VESC. Always verify the hardware version before flashing. If something goes wrong, the VESC bootloader is resilient — you can usually reflash via USB even after a failed update.

### 7.3 Flash Custom Firmware (Advanced)

If you need to flash a custom-built firmware `.bin` file:

1. Go to **Firmware** tab
2. Select the **"Custom File"** sub-tab
3. Click **"Browse"** and select your custom `.bin` file
4. Click **"Upload"**
5. Wait for the flash to complete

---

## 8. VESC Motor Configuration

### 8.1 Motor Setup Wizard

The fastest way to configure the VESC for the G30 Max motor:

1. Connect to the VESC (USB, no battery yet for initial settings — **connect battery for motor detection**)
2. Go to **Wizards** → **Setup Motors FOC**
3. Follow the wizard:

| Step | Setting | G30 Max Value |
|------|---------|---------------|
| Motor Type | BLDC (with hall sensors) | **BLDC** |
| Sensor Mode | Hall Sensors | **Hall Sensors** |
| Motor Poles (pole pairs) | — | **15 pole pairs (30 poles)** |
| Battery Cells | 10S (36V nominal) | **10S** |
| Battery Type | Li-ion | **Li-ion** |
| Max Motor Current | — | **25A** (adjust to preference) |
| Max Brake Current | — | **-15A** (adjust to preference) |
| Max Battery Current | — | **20A** (adjust to BMS limits) |
| Max Regen Current | — | **-10A** (adjust to BMS limits) |

4. Click **"Run Detection"** to detect motor parameters
   - **The motor will spin briefly** — make sure the wheel is off the ground!
   - The VESC will detect: flux linkage, motor resistance, motor inductance, hall sensor table
5. Review detected parameters and click **"Apply"**

### 8.2 Key Parameters for G30 Max

After the wizard, verify or adjust these settings in **Motor Settings** → **FOC**:

| Parameter | Path in VESC Tool | Recommended Value |
|-----------|-------------------|-------------------|
| Motor Type | Motor Settings → General → Motor Type | **FOC** |
| Sensor Mode | Motor Settings → General → Sensor Mode | **Hall Sensors** |
| Max ERPM | Motor Settings → General → Speed Limit | **20000** (adjust for desired top speed) |
| Current Max | Motor Settings → General → Motor Current Max | **25A** |
| Current Brake Max | Motor Settings → General → Motor Current Brake Max | **-15A** |
| Battery Cells | Motor Settings → General → Battery → Cells Series | **10** |
| Battery Cutoff Start | Motor Settings → General → Battery → Voltage Cutoff Start | **33.0V** (3.3V/cell) |
| Battery Cutoff End | Motor Settings → General → Battery → Voltage Cutoff End | **30.0V** (3.0V/cell) |

> **SAFETY:** The BMS has its own undervoltage protection, but the VESC cutoff adds a software safety layer. Set the VESC cutoff slightly above the BMS trip point to avoid abrupt BMS shutdowns.

### 8.3 App Settings

Configure the VESC application layer for the Ninebot dashboard protocol:

1. Go to **App Settings** → **General**
2. Set **App to Use**: **Custom User App** (or **No App** if using Lisp only)
3. Go to **App Settings** → **ADC**
4. Set **ADC Mode**: **Off** (throttle comes via Ninebot protocol, not physical ADC)
5. Click **Write Motor Configuration** (M) and **Write App Configuration** (A) in the toolbar

---

## 9. Upload the Lisp Script

The `g30_dash.lisp` script bridges the G30 dashboard to the VESC. It reads throttle/brake values from the Ninebot protocol and controls the motor via VESC's ADC override.

### 9.1 Upload via VESC Tool Desktop

1. Connect to the VESC in VESC Tool
2. Go to **VESC Dev** → **LispBM** tab (left sidebar)
3. Click **"Open File"** (folder icon)
4. Navigate to: `vesc-lisp/g30_dash.lisp`
5. The script will appear in the editor
6. **Review the user parameters** at the top of the script and adjust if needed:

```lisp
; User parameters — adjust these to your preference
(def min-speed 1)                    ; km/h — below this is "idle"
(def eco-speed (/ 7 3.6))           ; 7 km/h eco mode limit
(def drive-speed (/ 20 3.6))        ; 20 km/h drive mode limit
(def sport-speed (/ 30 3.6))        ; 30 km/h sport mode limit
(def eco-watts 400)                  ; eco power limit (watts)
(def drive-watts 500)                ; drive power limit (watts)
(def sport-watts 700)                ; sport power limit (watts)
```

7. Click **"Upload"** (up arrow icon) to compile and upload the script to the VESC
8. The script starts running immediately
9. Click **"Stream"** to see live debug output (optional)

### 9.2 Upload via VESC Tool Mobile (Android)

1. Connect to the VESC via Bluetooth (if VESC has BLE module) or USB OTG
2. Navigate to **LispBM** section
3. Open the `g30_dash.lisp` file
4. Tap **Upload**

### 9.3 Make Script Persistent (Auto-Start on Boot)

By default, uploaded Lisp scripts persist across reboots. To verify:

1. In the LispBM tab, check that **"Upload to Flash"** is enabled (this is the default)
2. Power cycle the VESC
3. The script should auto-start — verify by checking if the dashboard displays data

### 9.4 Verify Script Operation

After uploading, verify the script is working:

| Test | Expected Result |
|------|-----------------|
| Dashboard displays speed | Speed shows `0` (or battery % if idle display is enabled) |
| Dashboard shows battery | Battery percentage from VESC estimation |
| Dashboard shows mode | Speed mode icon (Eco/Drive/Sport) |
| Throttle response | Twist throttle → motor spins (wheel off ground!) |
| Brake response | Squeeze brake lever → regenerative braking |
| Single press (when off) | Scooter turns on |
| Single press (when on) | Toggle headlight |
| Double press | Cycle speed mode: Sport → Eco → Drive → Sport |
| Double press + brake | Toggle lock mode |
| Long press (6 seconds) | Turn off |

---

## 10. Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Dashboard shows nothing | No UART connection to VESC | Check COMM cable: 5V/GND/TX wired correctly |
| Dashboard shows garbled data | Wrong baud rate or TX/RX swapped | Verify 115200 baud, check wire mapping |
| Throttle doesn't work | Frame 0x65 not received | Verify Green wire (data) connected to VESC TX (half-duplex) |
| Motor spins wrong direction | Phase wires swapped | Swap any two motor phase wires, re-run motor detection |
| Motor stutters / vibrates | Bad hall sensor sequence | Swap hall sensor wires (H1/H2/H3 order), re-run detection |
| Hall sensor error | Hall wires not connected or wrong voltage | Verify 5V/GND to hall sensors, check for damaged wires |
| VESC won't connect via USB | Missing driver | Install STM32 VCP driver (see Section 6.2) |
| Lisp script doesn't start | Script not uploaded to flash | Re-upload with "Upload to Flash" enabled |
| Battery % always 0 or wrong | VESC voltage calibration off | Check Motor Settings → Battery → Cells Series = 10 |
| Button not working | RX pin not connected to button wire | Verify White wire → VESC RX pin |
| BMS not communicating | BMS UART not connected | Connect BMS cable to VESC AUX UART (if available) |
| VESC overheating | Insufficient cooling or too high current | Reduce current limits, improve VESC mounting for heat dissipation |
| Dashboard beeps continuously | Lock mode active while moving | Double-press + brake to unlock |
| "Fault: xxx" on dashboard | VESC fault condition | Connect via VESC Tool, check **Terminal** → type `faults` to read fault log |

---

## 11. Passthrough Mode — VESC as USB-UART Bridge

The VESC can act as a USB-to-UART bridge for updating the BLE dashboard or BMS firmware. This is useful for flashing custom firmware without additional hardware.

### Entering Passthrough Mode

Send one of these ASCII commands over the VESC USB serial port:

| Command | Target | Description |
|---------|--------|-------------|
| `UPDATE BLE\n` | BLE Dashboard (STM32) | VESC bridges USB ↔ COMM UART |
| `UPDATE BMS\n` | BMS Battery (STM32) | VESC bridges USB ↔ AUX UART |

After sending the command, the VESC transparently passes all data between USB and the selected UART. You can then use the standard flashing tools:

```powershell
# Flash BLE dashboard firmware via VESC passthrough
python tools/flasher/ninebot_flasher.py --port COM3 --target ble --firmware build/ble_app.bin
```

### Exiting Passthrough Mode

Power cycle the VESC (disconnect and reconnect battery or USB).

---

## 12. Reference — Ninebot Protocol Frames Used by the Script

### Frame 0x65: Dashboard → VESC (Throttle/Brake Input)

```
Byte:  0     1     2     3     4     5     6     7     8     9
     [5A]  [A5]  [len] [src] [dst] [65]  [sub] [...] [crcL] [crcH]
                              0x21  0x20  0x65
                              (BLE) (ESC)
```

| Byte | Field | Range | Conversion |
|------|-------|-------|------------|
| 5 | Throttle | 0–255 | Voltage = value / 77.2 (0–3.3V) |
| 6 | Brake | 0–255 | Voltage = value / 77.2 (0–3.3V) |

### Frame 0x64: VESC → Dashboard (Display Update)

```
Byte:  0     1     2     3     4     5     6     7     8     9    10    11    12    13    14
     [5A]  [A5]  [06]  [20]  [21]  [64]  [00] [mode][batt][lgt] [beep][spd] [err] [crcL][crcH]
                        (ESC) (BLE)
```

| Byte | Field | Values |
|------|-------|--------|
| 7 | Mode | 1=Drive, 2=Eco, 4=Sport, 16=Off, 32=Lock, +128=TempWarn |
| 8 | Battery | 0–100 (%) |
| 9 | Light | 0=Off, 1=On |
| 10 | Beep | 0=Off, 1=Beep |
| 11 | Speed | km/h (or battery % when idle if `show-batt-in-idle` = 1) |
| 12 | Error | Fault code from VESC |

### CRC Calculation

```
CRC = (sum of bytes [2] through [12]) XOR 0xFFFF
Frame[13] = CRC & 0xFF         (low byte)
Frame[14] = (CRC >> 8) & 0xFF  (high byte)
```

---

## 13. Safety Warnings

| Warning | Details |
|---------|---------|
| **HIGH VOLTAGE** | The battery pack is 36V nominal / 42V max. Can deliver >50A short circuit current. |
| **MOTOR SPIN** | The motor WILL spin during detection and testing. **Keep wheel off the ground.** |
| **REGENERATIVE BRAKING** | Regen at high battery SOC (>95%) can overcharge the pack. Set VESC regen limits accordingly. |
| **THERMAL** | The VESC generates heat under load. Ensure adequate cooling / thermal pad mounting. |
| **WATERPROOFING** | The stock ESC compartment has basic water resistance. Ensure the VESC is sealed if riding in wet conditions. |
| **FIRMWARE MISMATCH** | Always use matching VESC firmware + Lisp script versions. Outdated scripts may not work with new firmware. |
| **BMS PROTECTION** | Do NOT bypass BMS protection. The BMS safeguards against overcurrent, undervoltage, and overtemperature. |

---

## 14. Quick Reference Card

```
┌─────────────────────────────────────────────────────────────┐
│               VESC ↔ G30 Max Quick Wiring Reference         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  DASHBOARD (4-pin, up stem)         VESC COMM Port          │
│  ─────────────────────────         ──────────────           │
│  Red ────────────────────────────► 5V                       │
│  Black ──────────────────────────► GND                      │
│  Green (data) ───────────────────► TX (half-duplex bus)     │
│  White (button) ─────────────────► RX (button GPIO input)   │
│                                                             │
│  MOTOR PHASES (3 thick wires)       VESC Phase Output       │
│  ────────────────────────────      ─────────────────        │
│  Yellow ─────────────────────────► Phase A (U)              │
│  Green ──────────────────────────► Phase B (V)              │
│  Blue ───────────────────────────► Phase C (W)              │
│                                                             │
│  HALL SENSORS (5-pin)               VESC Hall Port          │
│  ────────────────────              ──────────────           │
│  Red   (+5V) ────────────────────► 5V                       │
│  Yellow (HA) ────────────────────► H1                       │
│  Green  (HB) ────────────────────► H2                       │
│  Blue   (HC) ────────────────────► H3                       │
│  Black  (GND)────────────────────► GND                      │
│                                                             │
│  BATTERY POWER (2 thick wires)      VESC Power Input        │
│  ─────────────────────────────     ─────────────────        │
│  Red   (B+, 36-42V) ────────────► VIN+ / B+                │
│  Black (B-) ─────────────────────► VIN- / B-                │
│                                                             │
│  BMS (4-pin, to battery)            VESC AUX UART           │
│  ───────────────────────           ─────────────            │
│  Red ────────────────────────────► 5V (optional)            │
│  Black ──────────────────────────► GND                      │
│  Yellow (TX) ────────────────────► TX                       │
│  Green  (RX) ────────────────────► RX                       │
│                                                             │
│  UART: 115200 8N1, 3.3V TTL                                │
│  Protocol: Ninebot (0x5AA5 header)                          │
│  VESC FW: 6.x+ with Lisp scripting                         │
│  Script: vesc-lisp/g30_dash.lisp                            │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```
