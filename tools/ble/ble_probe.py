"""Low-level BLE probe: dump GATT, subscribe to every notifiable char, send 0x5B, log all raw rx."""
import asyncio, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from ninebot_crypto import NinebotCrypto
from bleak import BleakClient, BleakScanner

ADDR = sys.argv[1] if len(sys.argv) > 1 else "D8:68:BA:16:A0:33"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"


def hx(b): return " ".join("%02X" % x for x in b)


async def main():
    dev = await BleakScanner.find_device_by_address(ADDR, timeout=10)
    if not dev:
        print("not found"); return
    async with BleakClient(dev) as c:
        print("connected:", dev.name, dev.address)
        rx_char = None
        notif = []
        for s in c.services:
            print("SVC", s.uuid)
            for ch in s.characteristics:
                print("   CH", ch.uuid, ch.properties)
                if ch.uuid.lower() == NUS_RX:
                    rx_char = ch
                if ("notify" in ch.properties) or ("indicate" in ch.properties):
                    notif.append(ch)

        def mk(uuid):
            def cb(_s, data):
                print("  <RX %s> %s" % (uuid[4:8], hx(data)))
            return cb
        for ch in notif:
            try:
                await c.start_notify(ch, mk(ch.uuid))
                print("   notify ON", ch.uuid[4:8])
            except Exception as e:
                print("   notify FAIL", ch.uuid[4:8], e)

        # Build the 0x5B encrypted frame and send it.
        crypto = NinebotCrypto(dev.name or "G30LD")
        wire = crypto.encrypt(bytes([0x5A, 0xA5, 0x00, 0x3E, 0x21, 0x5B, 0x00]))
        print("TX 0x5B wire:", hx(wire), " via", rx_char.uuid[4:8] if rx_char else None,
              rx_char.properties if rx_char else None)
        resp = "write" in (rx_char.properties if rx_char else [])
        for i in range(0, len(wire), 20):
            await c.write_gatt_char(rx_char, wire[i:i+20], response=resp)
        print("sent, listening 6s...")
        await asyncio.sleep(6)
        print("done")

asyncio.run(main())
