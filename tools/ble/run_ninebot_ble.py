"""Drive the known-working ownbee/ninebot-ble client against the G30, with DEBUG logging.

Validates whether this (restricted-firmware) dashboard answers the NbCrypto 5B/5C/5D handshake
over the Nordic UART service. The PAIR step needs a physical power-button press.
"""
import asyncio, logging, sys
from bleak import BleakScanner

logging.basicConfig(format="%(asctime)s %(levelname)s %(name)s %(message)s", level=logging.DEBUG)
for noisy in ("bleak.backends", "habluetooth", "bleak_retry_connector"):
    logging.getLogger(noisy).setLevel(logging.INFO)

from ninebot_ble import NinebotClient
from ninebot_ble.register import CtrlIdx

ADDR = sys.argv[1] if len(sys.argv) > 1 else "D8:68:BA:16:A0:33"
CONNECT_TIMEOUT = float(sys.argv[2]) if len(sys.argv) > 2 else 75.0


async def main():
    print(f"[*] scanning for {ADDR} ...")
    dev = await BleakScanner.find_device_by_address(ADDR, timeout=12.0)
    if not dev:
        print("[!] not found — is the scooter ON?"); return 2
    print(f"[+] {dev.name} / {dev.address}")

    client = NinebotClient()
    try:
        print("[*] connect + handshake (PRESS THE SCOOTER POWER BUTTON when asked) ...")
        await asyncio.wait_for(client.connect(dev), timeout=CONNECT_TIMEOUT)
        print("[+] AUTHENTICATED — encrypted channel open")

        for idx in (CtrlIdx.NB_INF_SN, CtrlIdx.NB_FW_VER, CtrlIdx.NB_INF_VER_BLE,
                    CtrlIdx.NB_INF_ERROR, CtrlIdx.NB_INF_ALERM,
                    CtrlIdx.NB_CTL_LIMIT_SPD, CtrlIdx.NB_CTL_NOMALSPEED):
            try:
                print(f"[+] {idx} = {await client.read_reg(idx)!r}")
            except Exception as e:
                print(f"[!] {idx}: {e}")
    except asyncio.TimeoutError:
        print("[!] handshake timed out (no reply to INIT, or button not pressed in time)")
        return 1
    finally:
        await client.disconnect()
    return 0


sys.exit(asyncio.run(main()))
