"""
Aggressive PRE_COMM grabber. Repeatedly takes a FRESH BLE connection and fires PRE_COMM (Gen2 +
Gen3) plus a Mi GET_INFO, so it catches the control channel the instant the phone app releases it.
Non-destructive (PRE_COMM/GET_INFO only). Prints the serial on first clean decrypt.
"""
import asyncio, os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "_ref", "segway-ninebot-ble-cli"))
from nb_crypto import NbCrypto, FW_DATA
from bleak import BleakClient, BleakScanner

ADDR = "D8:68:BA:16:A0:33"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
FE95_CTRL = "00000010-0000-1000-8000-00805f9b34fb"
FE95_DATA = "00000001-0000-1000-8000-00805f9b34fb"
DEADLINE = float(sys.argv[1]) if len(sys.argv) > 1 else 100.0
COMBOS = [("Gen2", 0xA5, FW_DATA), ("Gen3", 0xB5, b"\x00" * 16)]


def hx(b): return " ".join("%02X" % x for x in b)


async def attempt(dev, name):
    rx_any = []
    buf = bytearray()
    frames: asyncio.Queue = asyncio.Queue()

    def on_nus(_s, d):
        rx_any.append(("NUS", bytes(d)))
        buf.extend(d)
        while len(buf) >= 3:
            if buf[0] != 0x5A or buf[1] not in (0xA5, 0xB5):
                del buf[0]; continue
            total = buf[2] + 13
            if len(buf) < total:
                break
            frames.put_nowait(bytes(buf[:total])); del buf[:total]

    async with BleakClient(dev, timeout=12) as c:
        await c.start_notify(NUS_TX, on_nus)
        try:
            await c.start_notify(FE95_DATA, lambda _s, d: rx_any.append(("fe95", bytes(d))))
        except Exception:
            pass
        # Mi GET_INFO (in case the relay is gated behind Mi auth)
        try:
            await c.write_gatt_char(FE95_CTRL, bytes.fromhex("a2000000"), response=True)
        except Exception:
            pass
        for _ in range(3):
            for label, pre, ecb in COMBOS:
                tx = NbCrypto(ecb_input=ecb)
                tx.set_key(name.encode(), None)
                wire = tx.encrypt(bytes([0x5A, pre, 0x00, 0x3E, 0x04, 0x5B, 0x00]))
                for i in range(0, len(wire), 20):
                    await c.write_gatt_char(NUS_RX, wire[i:i + 20], response=False)
                try:
                    reply = await asyncio.wait_for(frames.get(), timeout=1.0)
                except asyncio.TimeoutError:
                    continue
                rx = NbCrypto(ecb_input=ecb)
                rx.set_key(name.encode(), None)
                dec, rc = rx.decrypt(reply)
                print(f"\n[REPLY {label}] rc={rc} RX {hx(reply)}")
                print(f"   DEC {hx(dec)}")
                if len(dec) >= 37 and dec[5] == 0x5B:
                    print(f"\n*** PRE_COMM OK ({label}) auth={hx(dec[7:23])}")
                    print(f"*** SERIAL = {bytes(dec[23:37])!r} ({hx(dec[23:37])})")
                    return True
        if rx_any:
            print("   (saw %d misc notifications: %s)" % (len(rx_any), rx_any[0][0]))
    return False


async def main():
    dev = await BleakScanner.find_device_by_address(ADDR, timeout=12)
    if not dev:
        print("not found"); return 2
    name = dev.name or "G30LD"
    print(f"[+] {name}/{dev.address} — CLOSE the phone app now; grabbing for {DEADLINE:.0f}s ...")
    end = time.time() + DEADLINE
    n = 0
    while time.time() < end:
        n += 1
        try:
            if await attempt(dev, name):
                return 0
        except Exception as e:
            print(f"   reconnect ({type(e).__name__})")
        print(f"   grab {n}: silent ({int(end - time.time())}s left)")
        await asyncio.sleep(0.5)
    print("\nStill silent — control channel never opened.")
    return 1


sys.exit(asyncio.run(main()))
