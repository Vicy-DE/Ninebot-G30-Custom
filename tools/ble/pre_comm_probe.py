"""
Non-destructive PRE_COMM (0x5B) probe for the G30 dashboard, using NootNooot's Ghidra-verified
Encryption2 crypto (nb_crypto.py). PRE_COMM only reads the auth challenge + serial number — it
sets nothing, so it cannot affect the official-app pairing.

Tries both protocol generations (Gen3 5A B5 / Gen2 5A A5) and both ECB inputs, with the BLE board
addressed as 0x04. A clean decrypt (rc==0, cmd==0x5B) proves the channel and yields the serial.
"""
import asyncio, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "_ref", "segway-ninebot-ble-cli"))
from nb_crypto import NbCrypto, FW_DATA
from bleak import BleakClient, BleakScanner

ADDR = "D8:68:BA:16:A0:33"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
NAME = "G30LD"


def hx(b): return " ".join("%02X" % x for x in b)


async def main():
    dev = await BleakScanner.find_device_by_address(ADDR, timeout=12)
    if not dev:
        print("not found"); return
    name = dev.name or NAME

    combos = [
        ("Gen3 5AB5 / ecb=zeros", 0xB5, b"\x00" * 16),
        ("Gen2 5AA5 / ecb=FW_DATA", 0xA5, FW_DATA),
        ("alt  5AA5 / ecb=zeros", 0xA5, b"\x00" * 16),
        ("alt  5AB5 / ecb=FW_DATA", 0xB5, FW_DATA),
    ]

    async with BleakClient(dev) as c:
        print("connected", name)
        buf = bytearray()
        frames = asyncio.Queue()

        def on_rx(_s, data):
            buf.extend(data)
            while len(buf) >= 3:
                if buf[0] != 0x5A or buf[1] not in (0xA5, 0xB5):
                    del buf[0]; continue
                total = buf[2] + 13
                if len(buf) < total:
                    break
                frames.put_nowait(bytes(buf[:total])); del buf[:total]
        await c.start_notify(NUS_TX, on_rx)

        for label, pre, ecb in combos:
            # drain
            while not frames.empty():
                frames.get_nowait()
            tx = NbCrypto(ecb_input=ecb)
            tx.set_key(name.encode(), None)
            pt = bytes([0x5A, pre, 0x00, 0x3E, 0x04, 0x5B, 0x00])
            wire = tx.encrypt(pt)
            print(f"\n[{label}] TX {hx(wire)}")
            for i in range(0, len(wire), 20):
                await c.write_gatt_char(NUS_RX, wire[i:i + 20], response=False)
            try:
                reply = await asyncio.wait_for(frames.get(), timeout=2.5)
            except asyncio.TimeoutError:
                print("   no reply")
                continue
            print("   RX", hx(reply))
            rx = NbCrypto(ecb_input=ecb)
            rx.set_key(name.encode(), None)
            dec, rc = rx.decrypt(reply)
            print(f"   decrypt rc={rc}  -> {hx(dec)}")
            if len(dec) >= 7 and dec[5] == 0x5B:
                auth = dec[7:23]; sn = dec[23:37]
                print(f"   *** PRE_COMM OK ***  idx={dec[6]}  auth={hx(auth)}")
                print(f"   *** SERIAL = {bytes(sn)!r} ({hx(sn)})")
                print(f"   *** WINNING COMBO: {label}")
                return
        print("\nNo combo produced a valid 0x5B response.")

asyncio.run(main())
