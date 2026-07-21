"""Probe the Xiaomi Mi (fe95) secure-auth channel: write GET_INFO, see if/where the device replies."""
import asyncio, sys
from bleak import BleakClient, BleakScanner

ADDR = "D8:68:BA:16:A0:33"
FE95 = lambda n: "0000%04x-0000-1000-8000-00805f9b34fb" % n
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
CMD_GET_INFO = bytes.fromhex("a2000000")
CMD_LOGIN = bytes.fromhex("24000000")


def hx(b): return " ".join("%02X" % x for x in b)


async def main():
    dev = await BleakScanner.find_device_by_address(ADDR, timeout=12)
    if not dev:
        print("not found"); return
    async with BleakClient(dev) as c:
        print("connected", dev.name)
        rx = []

        def mk(tag):
            def cb(_s, d): rx.append((tag, bytes(d))); print("  <%s> %s" % (tag, hx(d)))
            return cb

        # Subscribe every notifiable characteristic so we see the reply wherever it lands.
        for s in c.services:
            for ch in s.characteristics:
                if "notify" in ch.properties or "indicate" in ch.properties:
                    try:
                        await c.start_notify(ch, mk(ch.uuid[4:8]))
                        print("  notify ON", ch.uuid[4:8])
                    except Exception as e:
                        print("  notify FAIL", ch.uuid[4:8], e)

        async def try_write(uuid, data, label):
            rx.clear()
            try:
                await c.write_gatt_char(uuid, data, response=True)
                print("  TX %s -> %s (resp)" % (label, hx(data)))
            except Exception as e1:
                try:
                    await c.write_gatt_char(uuid, data, response=False)
                    print("  TX %s -> %s (no-resp)" % (label, hx(data)))
                except Exception as e2:
                    print("  TX %s FAILED: %s / %s" % (label, e1, e2)); return
            await asyncio.sleep(2.5)
            print("    -> %d replies" % len(rx))

        print("\n[A] GET_INFO -> fe95/0010 (miauth UPNP control char)")
        await try_write(FE95(0x0010), CMD_GET_INFO, "0010")

        print("\n[B] GET_INFO -> fe95/0001 (write+notify char)")
        await try_write(FE95(0x0001), CMD_GET_INFO, "0001")

        print("\n[C] LOGIN -> fe95/0010")
        await try_write(FE95(0x0010), CMD_LOGIN, "0010")

        print("\ndone")

asyncio.run(main())
