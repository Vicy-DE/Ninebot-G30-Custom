# Debug Guide — Ninebot G30 Max Custom Firmware

## Tools

| Tool | Source | Purpose |
|------|--------|---------|
| USB-UART adapter | 3.3V TTL (CP2102, CH340, FTDI) | UART access to scooter bus |
| Serial terminal | PuTTY, TeraTerm, `python -m serial.tools.miniterm` | Monitor & interact |
| Python + pyserial | `pip install pyserial` | Automated flashing/testing |
| ST-Link V2 | STMicroelectronics | SWD recovery only |
| STM32CubeProgrammer | st.com | SWD flashing GUI |
| Logic analyzer | Saleae, DSLogic | Protocol sniffing |

**Primary debug interface: UART at 115200 8N1 via the scooter's internal bus.**

---

## 1. Hardware Connection

### UART Access Points

```
┌─────────────────────────────────────────────────────────┐
│                    UART Bus Topology                     │
│                                                          │
│  USB-UART ──► VESC (ESC) ──► BLE Dashboard              │
│  Adapter       │                │                        │
│  (3.3V)        │                └─► nRF51822             │
│                │                                         │
│                └──────────► BMS Battery                   │
│                                                          │
│  All links: 115200 baud, 8N1, 3.3V TTL                  │
└─────────────────────────────────────────────────────────┘
```

### Connection Options

| Method | Wiring | Use Case |
|--------|--------|----------|
| **Via VESC USB** | USB cable to VESC | Easiest — VESC passthrough mode |
| **Direct ESC tap** | USB-UART RX/TX to ESC UART header | Sniff ESC↔BLE traffic |
| **Direct BMS tap** | USB-UART RX/TX to BMS UART pins | Direct BMS debug |
| **BLE SWD pads** | ST-Link to BLE board SWD | Recovery / initial flash |

### VESC Passthrough Mode

The VESC acts as the central UART hub. To reach BLE or BMS boards:

```
PC → USB → VESC → "UPDATE BLE\n" → VESC enters passthrough → PC talks to BLE
PC → USB → VESC → "UPDATE BMS\n" → VESC enters passthrough → PC talks to BMS
```

---

## 2. Build

See [BUILD.md](BUILD.md).

```powershell
cmake --build bootloader/build/ble
```

---

## 3. Flash — Stock IAP (OTA) Method

Use the scooter's existing IAP bootloader for initial deployment. This is the safest method and requires no hardware modification.

### Step 1: Prepare firmware

```powershell
# Build the application firmware
cmake --build firmware/decompiled/build

# Sign the firmware (creates .sfw file)
python tools/signing/sign_firmware.py --input build/ble_app.bin --output build/ble_app.sfw --target ble
```

### Step 2: Flash via Ninebot IAP protocol

```powershell
# Flash BLE firmware via serial adapter connected to ESC UART
python tools/flasher/ninebot_flasher.py --port COM3 --target ble --firmware build/ble_app.bin

# Flash BMS firmware
python tools/flasher/ninebot_flasher.py --port COM3 --target bms --firmware build/bms_app.bin
```

### Step 3: Flash via NBU (custom bootloader present)

```powershell
# Flash signed firmware via NBU through VESC passthrough
python tools/flasher/nbu_send.py --port COM3 --target ble-stm32 --file build/ble_app.sfw
```

---

## 4. Flash — SWD Method (Recovery / Initial Bootstrap)

Only use SWD for:
- Initial bootloader installation (before any UART flashing is possible)
- Recovery from a bricked board
- Reading flash dumps for reverse engineering

```powershell
# Read full flash dump (backup before any modification!)
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init; halt; flash read_image backup.bin 0x08000000 0x10000 bin; shutdown"

# Flash bootloader to 0x08000000
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init; halt; flash write_image erase bootloader.bin 0x08000000 bin; reset run; shutdown"
```

---

## 5. Debug Workflow — UART Monitor

### Serial Monitor

```powershell
# Basic monitor
python -m serial.tools.miniterm COM3 115200

# With timestamp and logging
python -m serial.tools.miniterm COM3 115200 --raw | Tee-Object -FilePath "debug_log.txt"
```

### Expected Boot Messages (Custom Bootloader)

```
[BOOT] Secure Bootloader v1.0
[BOOT] Target: BLE-STM32
[BOOT] App signature valid. Booting...
```

Or if in update mode:

```
[BOOT] Secure Bootloader v1.0
[BOOT] Target: BLE-STM32
[BOOT] Waiting for .sfw file via NBU...
[BOOT] Send file now (NBU, framed half-duplex)
```

### Protocol Sniffing

```powershell
# Capture raw Ninebot protocol packets
python tools/analysis/capture_protocol.py --port COM3 --output capture.bin

# Parse captured packets
python tools/analysis/parse_protocol.py capture.bin
```

---

## 6. Verify — Post-Flash Validation

After every flash operation, verify correct operation:

### Check 1: Boot messages
- Monitor UART for bootloader banner + app startup
- Bootloader should print target board ID and signature status

### Check 2: Protocol response
```powershell
# Read BLE firmware version register
python tools/flasher/ninebot_flasher.py --port COM3 --read-register 0x21 0x17

# Read BMS cell voltages
python tools/flasher/ninebot_flasher.py --port COM3 --read-register 0x22 0x30 20
```

### Check 3: Functional test
- Throttle response (BLE board)
- Display output (BLE board)
- Cell voltage reading (BMS board)
- BLE advertising (nRF51)
- VESC communication (motor response)

### Check 4: Rollback capability
- Verify the board can still enter IAP/NBU update mode
- Confirm the update trigger (button hold or software flag) works
- Test with a known-good firmware image

---

## 7. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| No UART output | Wrong baud rate or TX/RX swapped | Check 115200 8N1, swap TX↔RX |
| Garbage on UART | Baud rate mismatch | Verify 115200, check oscillator config |
| Board doesn't boot | Corrupted app region | Hold power button to enter bootloader |
| Bootloader loops | App signature invalid | Re-flash with signed .sfw file |
| SWD connection fails | Read protection set | Full chip erase via STM32CubeProgrammer |
| VESC passthrough fails | Wrong command syntax | Use exact string "UPDATE BLE\n" |
| NBU timeout | UART not connected to bootloader | Verify board is in bootloader mode |
| Flash verify fails | Power glitch during write | Re-flash, ensure stable power |

---

## 8. Debug Logging in Firmware

Custom firmware should use a debug UART output for development:

```c
/* In application firmware */
#if DEBUG_ENABLED
    #define DBG(fmt, ...) uart_printf("[DBG] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define DBG(fmt, ...) ((void)0)
#endif
```

Use the Ninebot protocol UART for both debug output and runtime communication. The debug messages are distinguished by the `[DBG]` prefix and do not interfere with the 5A A5 framed protocol packets.
