"""
Looping non-destructive PRE_COMM probe — retries for a while so the scooter can be powered on.
PRE_COMM (0x5B) only reads the auth challenge + serial; it changes nothing on the device.
On the first clean decrypt it prints the winning protocol combo + serial number and exits.
"""
import asyncio, os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "_ref", "segway-ninebot-ble-cli"))
from nb_crypto import NbCrypto, FW_DATA
from bleak import BleakClient, BleakScanner

ADDR = "D8:68:BA:16:A0:33"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
DEADLINE = float(sys.argv[1]) if len(sys.argv) > 1 else 75.0

COMBOS = [
    ("Gen3 5AB5/zeros", 0xB5, b"\x00" * 16),
    ("Gen2 5AA5/FWDATA", 0xA5, FW_DATA),
    ("alt 5AA5/zeros", 0xA5, b"\x00" * 16),
    ("alt 5AB5/FWDATA", 0xB5, FW_DATA),
]


def hx(b): return " ".join("%02X" % x for x in b)


async def main():
    dev = await BleakScanner.find_device_by_address(ADDR, timeout=12)
    if not dev:
        print("device not found — is it advertising?"); return 2
    name = (dev.name or "G30LD")
    print(f"[+] {name} / {dev.address} — turn the scooter ON now; retrying {DEADLINE:.0f}s ...")

    end = time.time() + DEADLINE
    rnd = 0
    while time.time() < end:
        rnd += 1
        try:
            async with BleakClient(dev, timeout=15) as c:
                buf = bytearray()
                frames: asyncio.Queue = asyncio.Queue()

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

                while time.time() < end:
                    for label, pre, ecb in COMBOS:
                        while not frames.empty():
                            frames.get_nowait()
                        tx = NbCrypto(ecb_input=ecb)
                        tx.set_key(name.encode(), None)
                        wire = tx.encrypt(bytes([0x5A, pre, 0x00, 0x3E, 0x04, 0x5B, 0x00]))
                        for i in range(0, len(wire), 20):
                            await c.write_gatt_char(NUS_RX, wire[i:i + 20], response=False)
                        try:
                            reply = await asyncio.wait_for(frames.get(), timeout=1.2)
                        except asyncio.TimeoutError:
                            continue
                        rx = NbCrypto(ecb_input=ecb)
                        rx.set_key(name.encode(), None)
                        dec, rc = rx.decrypt(reply)
                        print(f"\n[REPLY] combo={label} rc={rc}")
                        print("   RX ", hx(reply))
                        print("   DEC", hx(dec))
                        if len(dec) >= 37 and dec[5] == 0x5B:
                            print(f"\n*** PRE_COMM OK — combo={label} idx={dec[6]} ***")
                            print(f"*** auth_param = {hx(dec[7:23])}")
                            print(f"*** SERIAL     = {bytes(dec[23:37])!r}  ({hx(dec[23:37])})")
                            return 0
                    print(f"   round {rnd}: still silent ({int(end - time.time())}s left)")
                    await asyncio.sleep(0.5)
        except Exception as e:
            print(f"   (reconnecting: {type(e).__name__})")
            await asyncio.sleep(1.0)
    print("\nStill no PRE_COMM reply within the window.")
    return 1


sys.exit(asyncio.run(main()))
