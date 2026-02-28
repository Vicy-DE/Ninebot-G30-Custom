# Ninebot G30 Max — Firmware Flashing Guide

## Overview

There are several methods to flash firmware onto the Ninebot G30 Max's three boards (ESC/DRV, BLE, BMS). This guide covers all approaches from easiest to most advanced.

## Method 1: OTA via ScooterHacking Utility (Easiest)

### Requirements
- Android phone with Bluetooth
- [ScooterHacking Utility](https://utility.cfw.sh/) app installed
- Firmware files (encrypted `.bin.enc`)

### Steps
1. Power on the scooter
2. Open ScooterHacking Utility
3. Connect to the scooter via Bluetooth
4. Navigate to **Flash Firmware**
5. Select the board to flash (DRV, BLE, or BMS)
6. Select the firmware file
7. Start flashing — **do not turn off the scooter during flashing**
8. Wait for completion and automatic restart

### Notes
- This method uses encrypted firmware files (`.bin.enc`)
- OTA flashing is the safest and most common method
- If the scooter disconnects mid-flash, the bootloader should allow re-flashing
- Typical flash time: 1-3 minutes per board

## Method 2: Custom Firmware (CFW) via max.cfw.sh

### What is max.cfw.sh?

[max.cfw.sh](https://max.cfw.sh/) is an online tool by the ScooterHacking community that generates custom firmware patches for the G30 Max. It modifies stock firmware to change parameters like speed limits, motor power, acceleration curves, and more.

### Requirements
- Web browser for [max.cfw.sh](https://max.cfw.sh/)
- ScooterHacking Utility (Android) or XiaoFlasher for flashing
- Stock firmware as base (typically DRV126 for ESC)

### Steps
1. Visit [max.cfw.sh](https://max.cfw.sh/)
2. Select your base firmware version (e.g., DRV126)
3. Configure your desired modifications:
   - Speed limit
   - Motor power constant (MPC)
   - Current limits
   - Cruise control behavior
   - Region lock removal
   - KERS (regenerative braking) strength
4. Click **Generate** to download the patched firmware
5. Flash the generated firmware using Method 1

### Available CFW Parameters

| Parameter | Description | Range |
|---|---|---|
| Speed Limit | Maximum speed | 0-50+ km/h |
| Motor Power Constant | Motor power curve | Model-dependent |
| Phase/Battery Current | Max current limits | 0-35A+ |
| Cruise Control | Enable/disable/behavior | On/Off |
| KERS | Regenerative braking strength | 0-5 |
| Motor Start Speed | Min speed before motor engages | 0-10 km/h |
| Brake Lever | Electronic brake percentage | 0-130% |
| Region Lock | Remove geographic restrictions | On/Off |
| SN Prefix Change | Change serial prefix (G30D/G30P/etc.) | Various |

## Method 3: ST-Link SWD (Direct Flash)

### Requirements
- **ST-Link V2** programmer (USB, ~$5-15)
- **STM32CubeProgrammer** software (free from ST.com)
- Soldering equipment (for connecting wires)
- Plain `.bin` firmware files (not encrypted)

### Hardware Setup

Each board has SWD pads for programming:

```
ST-Link Pin    →    Target Board
─────────────────────────────────
SWDIO          →    SWDIO pad
SWCLK          →    SWCLK pad
GND            →    GND
3.3V           →    3V3 (or parasitic power from board)
```

### SWD Pad Locations

#### ESC Board
- SWD pads are usually exposed on the edge of the PCB
- May be labeled "SWD" or have test points labeled "DIO", "CLK"
- GND available from any ground pad or connector

#### BLE Dashboard
- SWD pads on the back of the dashboard PCB
- Accessible after removing the dashboard housing
- Be careful with the ribbon cable

#### BMS Board  
- SWD pads on the BMS PCB inside the battery housing
- **⚠️ CAUTION**: BMS PCB is always energized by the battery
- Disconnect the battery connector before working
- Reconnect carefully with correct polarity

### Flashing Steps

1. Install [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
2. Connect the ST-Link to the target board's SWD pads
3. Plug ST-Link into PC via USB
4. Open STM32CubeProgrammer
5. Select **ST-LINK** connection type
6. Click **Connect**
7. If the chip is read-protected:
   - Go to **Option Bytes** → Set **Read Protection** to Level 0
   - Apply — **this will erase the chip!**
8. Load your `.bin` file
9. Set the start address (typically `0x08001000` for application firmware)
10. Click **Download** to flash
11. Disconnect and power cycle

### Alternative: OpenOCD

```bash
# Connect to STM32 via ST-Link
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg

# In another terminal
telnet localhost 4444

# Halt the CPU
halt

# Read full flash (128KB for ESC, 64KB for BLE/BMS)
flash read_image dump.bin 0x08000000 0x20000 bin

# Erase and write new firmware
flash erase_sector 0 0 last
flash write_image erase firmware.bin 0x08001000 bin

# Reset
reset run
```

## Method 4: IAP (In-Application Programming)

IAP uses the built-in bootloader in the scooter firmware. This is what OTA flashing actually uses under the hood, but it can also be triggered manually.

### Serial IAP

1. Connect a USB-TTL adapter (3.3V) to the ESC's UART lines
2. Send the IAP initiation sequence via the Ninebot protocol
3. The target board enters bootloader mode
4. Send firmware blocks using the update protocol
5. Verify and reset

This method is advanced and typically only used for development or recovery.

## Firmware Encryption / Decryption

### XiaoTEA

Ninebot firmware files distributed over BLE use a TEA-based encryption. The tool [XiaoTEA](https://tools.scooterhacking.org/xiaotea/) can encrypt and decrypt these files.

- `.bin` → Plain firmware (for ST-Link flashing)
- `.bin.enc` → Encrypted firmware (for OTA/BLE flashing)

### Usage

1. Visit [https://tools.scooterhacking.org/xiaotea/](https://tools.scooterhacking.org/xiaotea/)
2. Select **Encode** or **Decode**
3. Upload your firmware file
4. Select the appropriate model/board type
5. Download the result

## Firmware Dump (Reading Current Firmware)

### Via ST-Link

1. Connect ST-Link to SWD pads
2. Open STM32CubeProgrammer
3. Connect to target
4. Read memory starting at `0x08000000`
5. Save to `.bin` file
6. Size: 128KB for ESC (STM32F103CBT6), 64KB for BLE/BMS (STM32F103C8T6)

### Via Protocol

Some registers can be read over the Ninebot protocol, but full firmware dumps are not possible this way — only specific data registers.

## Recovery from Bricked State

### Symptoms of a Bricked Board
- Scooter won't turn on
- Display shows error code and won't clear
- Scooter turns on but immediately shuts off
- BLE not discoverable

### Recovery Steps

1. **Soft brick** (firmware issue, bootloader intact):
   - Try OTA re-flash with ScooterHacking Utility
   - If BLE won't connect, use a second BLE module or serial connection

2. **Hard brick** (bootloader corrupted or read-protected):
   - **Must use ST-Link** for recovery
   - Connect to SWD pads
   - Disable read protection (this erases the chip)
   - Flash stock firmware starting at `0x08000000` (including bootloader)
   - **You will need a full dump** that includes the bootloader

3. **BMS brick** (most dangerous):
   - BMS controls charge/discharge FETs
   - If BMS is bricked, the battery may be permanently disconnected
   - ST-Link recovery is the only option
   - **⚠️ Work carefully around the live battery**

## Firmware Version Compatibility

| DRV | BLE | Compatible? | Notes |
|---|---|---|---|
| DRV126 | BLE107 | ✅ | Most common CFW combination |
| DRV126 | BLE117 | ✅ | Works fine |
| DRV145+ | BLE107 | ⚠️ | May have some issues |
| DRV145+ | BLE117 | ✅ | Recommended for newer firmware |
| DRV154 | BLE117 | ⚠️ | Beta, improved motor algorithm |

## Safety Checklist

- [ ] Backup current firmware before flashing
- [ ] Use correct firmware file for the target board
- [ ] Ensure stable power during flashing
- [ ] Do not disconnect cables during flashing
- [ ] Verify firmware integrity (checksum) before flashing
- [ ] Keep stock firmware files as backup
- [ ] Test basic functionality after flashing (brakes, throttle, lights)
