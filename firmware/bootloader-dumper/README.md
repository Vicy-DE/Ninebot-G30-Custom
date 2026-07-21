# Stock-Bootloader Dumper

A tiny firmware app that recovers the **otherwise-undumpable 4 KB stock bootloader**
(`0x08000000`–`0x08000FFF`). The distributed `BLE_*` images don't contain it (it's
factory-programmed and persists across IAP updates), so the only way to read it is
to run code on the chip that reads that region and ships it out — which is exactly
what this does.

## How it works
- Linked at **`0x08001000`** (the app base) → **flash it via the stock bootloader / IAP**,
  no soldering required (see [`../../docs/DASHBOARD_NO_SOLDER_FLASH.md`](../../docs/DASHBOARD_NO_SOLDER_FLASH.md)).
- On boot it reads `0x08000000..0x08000FFF`, CRC32s it, and emits it over the
  **dashboard cable UART** (USART2 PA2, half-duplex, 115200) as marker-framed hex,
  re-sending every ~1 s so a USB-TTL catches a full frame:
  ```
  ==NBDUMP== base=08000000 len=4096 crc32=XXXXXXXX
  <8192 hex chars, newline every 32 bytes>
  ==NBDUMPEND==
  ```
- It is **read-only** — it never erases or writes flash, so it cannot brick anything.
  (744-byte image; reuses the dashboard startup/linker/registers.)

## Use it (real hardware)
```bash
make                                  # -> build/bldump.bin  (744 bytes, @0x08001000)
# 1) flash build/bldump.bin via the stock IAP (tools/flasher/ninebot_flasher.py, addr 0x21)
# 2) connect a USB-TTL to the cable DATA line and receive:
python ../../tools/dump_bootloader.py --port COM5 --out BLE_bootloader_dump.bin
```
The receiver checks the device CRC32 against its own and writes the raw `.bin`
(default `boards/ble-dashboard/firmware/BLE_bootloader_dump.bin`).

## Verify in the simulator (no hardware)
Renode places a known pattern at `0x08000000` and runs the real dumper `.elf`:
```bash
python sim/make_pattern.py            # regenerate pattern.bin (gitignored)
# from the Renode bin/ dir:
Renode.exe --disable-xwt --console -P 0 -e "i @<repo>/firmware/bootloader-dumper/sim/dump_verify.resc"
python ../../tools/dump_bootloader.py --from-file sim/usart2_dump.log --out sim/recovered.bin
```
Verified result: device CRC32 == host CRC32, and `recovered.bin` is **byte-for-byte
identical** to `pattern.bin` (signature `STOCKBOOT_v1.337` recovered).

## Files
| File | Role |
|------|------|
| `src/main.cpp` | clock + USART2 init, CRC32, the read-and-emit loop |
| `Makefile` | reuses `../dashboard` startup.s, linker (`@0x08001000`), `stm32f103.h` |
| `sim/dump_verify.resc` | Renode: pattern @0x08000000 → run dumper → capture USART2 |
| `sim/make_pattern.py` | regenerates the 4 KB test pattern |

> ⚠️ Legitimate use only: dumping the bootloader from **your own** scooter for
> reverse-engineering/backup. The recovered image lets you study the stock IAP and
> build a complete SWD recovery image (bootloader + app).
