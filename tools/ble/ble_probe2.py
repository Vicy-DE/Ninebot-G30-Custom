"""Disambiguation probe: read fe95 info chars, then try plaintext vs crypto 0x5B / serial-read."""
import asyncio, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from ninebot_crypto import NinebotCrypto
from bleak import BleakClient, BleakScanner

ADDR = "D8:68:BA:16:A0:33"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
FE95 = lambda n: "0000%04x-0000-1000-8000-00805f9b34fb" % n
GAP = lambda n: "0000%04x-0000-1000-8000-00805f9b34fb" % n


def hx(b): return " ".join("%02X" % x for x in b)


def cksum(data):
    s = sum(data) & 0xFFFF
    return (s ^ 0xFFFF)


def plain(src, dst, cmd, arg, data=b""):
    body = bytes([len(data), src, dst, cmd, arg]) + data
    return b"\x5A\xA5" + body + bytes([cksum(body) & 0xFF, (cksum(body) >> 8) & 0xFF])


async def read_char(c, uuid, label):
    try:
        v = await c.read_gatt_char(uuid)
        print("  READ %s (%s): %s | %r" % (label, uuid[4:8], hx(v), bytes(v)))
        return v
    except Exception as e:
        print("  READ %s (%s) FAIL: %s" % (label, uuid[4:8], e))


async def main():
    dev = await BleakScanner.find_device_by_address(ADDR, timeout=10)
    if not dev:
        print("not found"); return
    async with BleakClient(dev) as c:
        print("connected", dev.name)
        got = []
        await c.start_notify(NUS_TX, lambda s, d: (got.append(bytes(d)), print("  <NUS> %s" % hx(d))))
        try:
            await c.start_notify(FE95(0x0001), lambda s, d: print("  <fe95-0001> %s" % hx(d)))
        except Exception as e:
            print("  fe95-0001 notify fail:", e)

        print("\n-- device info chars --")
        await read_char(c, GAP(0x2A00), "GAP-Name")
        await read_char(c, GAP(0x2A24), "ModelNo")
        await read_char(c, GAP(0x2A25), "Serial")
        await read_char(c, GAP(0x2A26), "FW-Rev")
        for n in (0x0002, 0x0004, 0x0013, 0x0014):
            await read_char(c, FE95(n), "fe95-%04x" % n)

        async def burst(label, frames, secs=3.0):
            print("\n-- %s --" % label)
            got.clear()
            for f in frames:
                print("  TX", hx(f))
                for i in range(0, len(f), 20):
                    await c.write_gatt_char(NUS_RX, f[i:i+20], response=False)
                await asyncio.sleep(0.3)
            await asyncio.sleep(secs)
            print("  -> %d NUS replies" % len(got))

        # A) plaintext
        await burst("PLAINTEXT 0x5B + serial-read", [
            plain(0x3E, 0x21, 0x5B, 0x00),
            plain(0x3E, 0x21, 0x01, 0x10, bytes([14])),   # ReadRegs BLE serial
            plain(0x3E, 0x21, 0x01, 0x1A, bytes([2])),    # ReadRegs BLE version
        ])

        # B) crypto 0x5B (write-without-response)
        crypto = NinebotCrypto(dev.name or "G30LD")
        w = crypto.encrypt(bytes([0x5A, 0xA5, 0x00, 0x3E, 0x21, 0x5B, 0x00]))
        await burst("CRYPTO 0x5B (WNR)", [w])

        print("\ndone")

asyncio.run(main())
