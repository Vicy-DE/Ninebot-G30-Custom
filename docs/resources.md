# Ninebot G30 Max — Community Resources & Links

## Official Resources

| Resource | URL | Description |
|---|---|---|
| Segway-Ninebot Official | [segway.com](https://www.segway.com/) | Official manufacturer website |
| Segway-Ninebot App | iOS / Android | Official app for scooter management |

## ScooterHacking Community

The ScooterHacking community is the primary resource for Ninebot/Segway firmware modification.

| Resource | URL | Description |
|---|---|---|
| **ScooterHacking Wiki** | [wiki.scooterhacking.org](https://wiki.scooterhacking.org/) | Hardware docs, teardowns, protocol info |
| **CFW Generator** | [max.cfw.sh](https://max.cfw.sh/) | Custom firmware generator for G30 Max |
| **ScooterHacking Utility** | [utility.cfw.sh](https://utility.cfw.sh/) | Android app for flashing and diagnostics |
| **XiaoTEA** | [tools.scooterhacking.org/xiaotea](https://tools.scooterhacking.org/xiaotea/) | Firmware encryption/decryption tool |
| **Stock Firmware Downloads** | [firmware.scooterhacking.org/max](https://firmware.scooterhacking.org/max/) | Repository of stock firmware binaries |
| **ScooterHacking Cloud** | [cloud.scooterhacking.org](https://cloud.scooterhacking.org/) | Datasheets, circuit diagrams, resources |
| **Discord** | [ScooterHacking Discord](https://discord.gg/scooterhacking) | Community chat for support and discussion |

## GitHub Repositories

| Repository | Description |
|---|---|
| [etransport/ninebot-docs](https://github.com/etransport/ninebot-docs) | Protocol documentation, register maps |
| [scooterhacking](https://github.com/scooterhacking) | ScooterHacking org - various tools |
| [CamiAlfa/M365-BLE-PROTOCOL](https://github.com/CamiAlfa/M365-BLE-PROTOCOL) | BLE protocol analysis (M365, similar to G30) |
| [danielkucera/ninebot-docs](https://github.com/danielkucera/ninebot-docs) | Additional protocol documentation |

## Wiki Pages (Key References)

| Page | URL | Content |
|---|---|---|
| ES2 BLE | [wiki: ES2BLE](https://wiki.scooterhacking.org/doku.php?id=es2ble) | Dashboard hardware (nRF51822 info) |
| ES2 ESC | [wiki: ES2ESC](https://wiki.scooterhacking.org/doku.php?id=es2esc) | Motor controller reference (STM32 pinout) |
| ES2 BMS | [wiki: ES2BMS](https://wiki.scooterhacking.org/doku.php?id=es2bms) | Battery management system (BQ769x0) |
| ESC1 | [wiki: ESC1](https://wiki.scooterhacking.org/doku.php?id=esc1) | M365 ESC component list |
| Protocol | [wiki: Protocol](https://wiki.scooterhacking.org/doku.php?id=protocol) | Ninebot UART protocol specification |
| G30 Max | [wiki: nbmax](https://wiki.scooterhacking.org/doku.php?id=nbmax) | G30 Max specifications |

## Datasheets & Technical Documents

### Microcontrollers

| Component | Manufacturer | Datasheet |
|---|---|---|
| STM32F103CBT6 | STMicroelectronics | [ST.com](https://www.st.com/en/microcontrollers-microprocessors/stm32f103cb.html) |
| STM32F103C8T6 | STMicroelectronics | [ST.com](https://www.st.com/en/microcontrollers-microprocessors/stm32f103c8.html) |
| GD32F103CBT6 | GigaDevice | [GigaDevice](https://www.gigadevice.com/product/mcu/arm-cortex-m3/gd32f103cbt6/) |
| nRF51822 | Nordic Semiconductor | [Nordic](https://www.nordicsemi.com/Products/nRF51822) |

### Battery Management

| Component | Manufacturer | Datasheet |
|---|---|---|
| BQ76940 | Texas Instruments | [TI.com](https://www.ti.com/product/BQ76940) |
| BQ7693003 (ES2) | Texas Instruments | [TI.com](https://www.ti.com/product/BQ7693003) |

### Power Components

| Component | Description | Source |
|---|---|---|
| SY8502FCC | Buck converter (85V) | [ScooterHacking Cloud](https://cloud.scooterhacking.org/) |
| MT8006A | 3-phase gate driver | [ScooterHacking Cloud](https://cloud.scooterhacking.org/) |
| NCEP85T14 | N-channel MOSFET | [ScooterHacking Cloud](https://cloud.scooterhacking.org/) |
| STP15810 | N-channel MOSFET | [ScooterHacking Cloud](https://cloud.scooterhacking.org/) |
| TPS54160 | Buck converter (60V) | [ScooterHacking Cloud](https://cloud.scooterhacking.org/) |

## Development Tools

| Tool | URL | Description |
|---|---|---|
| **STM32CubeProgrammer** | [ST.com](https://www.st.com/en/development-tools/stm32cubeprog.html) | Official ST flash tool |
| **STM32CubeIDE** | [ST.com](https://www.st.com/en/development-tools/stm32cubeide.html) | IDE for STM32 development |
| **OpenOCD** | [openocd.org](https://openocd.org/) | Open-source debug/flash tool |
| **ST-Link V2** | Various vendors | SWD programmer hardware (~$5-15) |
| **Ghidra** | [ghidra-sre.org](https://ghidra-sre.org/) | NSA reverse engineering tool (for firmware analysis) |
| **IDA Free** | [hex-rays.com](https://hex-rays.com/ida-free/) | Disassembler for firmware analysis |
| **PulseView** | [sigrok.org](https://sigrok.org/wiki/PulseView) | Logic analyzer software for protocol sniffing |

## Forums & Communities

| Platform | Link | Description |
|---|---|---|
| **Reddit r/ninebot** | [r/ninebot](https://reddit.com/r/ninebot) | General Ninebot discussion |
| **Reddit r/ElectricScooters** | [r/ElectricScooters](https://reddit.com/r/ElectricScooters) | Broader e-scooter community |
| **Elektroroller Forum** | [elektroroller-forum.de](https://www.elektroroller-forum.de/) | German e-scooter forum |
| **Electric Scooter Guide** | [electric-scooter.guide](https://electric-scooter.guide/) | Reviews and guides |

## YouTube Channels & Videos

Search for these topics on YouTube for teardown and modification videos:
- "Ninebot G30 Max teardown"
- "Ninebot G30 Max custom firmware"
- "Ninebot G30 Max ST-Link flash"
- "Ninebot G30 Max speed hack"
- "Ninebot G30 Max battery mod"

## Useful Reddit Threads

| Topic | Description |
|---|---|
| "G30 Max CFW guide" | Step-by-step custom firmware flashing |
| "G30 Max battery upgrade" | 48V / 13S battery modifications |
| "G30 Max controller swap" | ESC board replacement |
| "G30 Max speed limit removal" | Regional lock bypass |

## Alternative Apps

| App | Platform | Description |
|---|---|---|
| **ScooterHacking Utility** | Android | Best for CFW flashing |
| **XiaoFlasher** | Android | Advanced flashing and diagnostics |
| **m365 Tools** | Android/iOS | Basic scooter monitoring |
| **Ninebot Flasher** | Windows/Linux | PC-based serial flasher |
