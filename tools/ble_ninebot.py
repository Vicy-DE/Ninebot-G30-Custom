#!/usr/bin/env python3
"""Talk the Ninebot serial protocol to the G30 dashboard over BLE (Nordic UART
Service), via the PC's Bluetooth. The nRF51 relays frames to/from the STM32, so this
reaches the same protocol handler as the wired ESC bus — but, unlike the ESC bus, the
dashboard ANSWERS here (the App is normally the BLE master). Used to read the chip UID
for the CMD 0x57 update password, then drive the update.

    python tools/ble_ninebot.py read 0x10 14     # read dashboard reg 0x10 (serial)
    python tools/ble_ninebot.py raw 5A A5 ...     # send arbitrary Ninebot frame
    python tools/ble_ninebot.py monitor 8         # just log notifications for 8 s
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # write (app -> device)
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # notify (device -> app)
NAME = "G30LD"


def frame(src, dst, cmd, arg, payload=b""):
    body = bytes([len(payload), src, dst, cmd, arg]) + payload
    ck = (sum(body) ^ 0xFFFF) & 0xFFFF
    return bytes([0x5A, 0xA5]) + body + bytes([ck & 0xFF, ck >> 8])


def parse(bs):
    out = []
    i = 0
    while i < len(bs) - 1:
        if bs[i] == 0x5A and bs[i + 1] == 0xA5 and i + 9 <= len(bs):
            ln = bs[i + 2]
            tot = 2 + 5 + ln + 2
            if i + tot <= len(bs):
                body = bs[i + 2:i + 2 + 5 + ln]
                ck = bs[i + 2 + 5 + ln] | (bs[i + 3 + 5 + ln] << 8)
                ok = ((sum(body) ^ 0xFFFF) & 0xFFFF) == ck
                out.append((bs[i:i + tot], ok))
                i += tot
                continue
        i += 1
    return out


async def find():
    d = await BleakScanner.find_device_by_name(NAME, timeout=12.0)
    if not d:
        devs = await BleakScanner.discover(timeout=8.0)
        for x in devs:
            if x.name and "G30" in x.name:
                return x.address
        return None
    return d.address


async def run(argv):
    addr = await find()
    if not addr:
        print("G30 dashboard not found in BLE scan"); return 1
    print(f"connecting {addr} ...")
    rx_buf = bytearray()

    def on_notify(_, data):
        rx_buf.extend(data)
        print("  NOTIFY:", " ".join(f"{b:02X}" for b in data))

    async with BleakClient(addr, timeout=20.0) as c:
        print("connected:", c.is_connected)
        print("services:", [s.uuid for s in c.services])
        await c.start_notify(NUS_TX, on_notify)

        cmd = argv[0]
        if cmd == "monitor":
            await asyncio.sleep(float(argv[1]) if len(argv) > 1 else 8.0)
        else:
            if cmd == "read":
                reg = int(argv[1], 0); n = int(argv[2]) if len(argv) > 2 else 14
                fr = frame(0x3E, 0x21, 0x01, reg, bytes([n]))
            elif cmd == "raw":
                fr = bytes(int(x, 16) for x in argv[1:])
            else:
                print(__doc__); return 1
            print("WRITE:", " ".join(f"{b:02X}" for b in fr))
            await c.write_gatt_char(NUS_RX, fr, response=False)
            await asyncio.sleep(3.0)

        await c.stop_notify(NUS_TX)
        if rx_buf:
            print("\nframes decoded from notifications:")
            for f, ok in parse(bytes(rx_buf)):
                print(f"  [{'ok ' if ok else 'BAD'}] " + " ".join(f"{b:02X}" for b in f))
        else:
            print("\n(no notifications received)")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(run(sys.argv[1:])))
