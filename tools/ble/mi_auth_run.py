"""
Mi authentication + UART access for the G30 dashboard over BLE (Windows/bleak).

Drives miauth's MiClient through the BleakMi backend. Two modes:

  --register   On-device ECDH registration. Needs a power-button press, NO cloud/token.
               WARNING: re-keys the device -> the official Segway-Ninebot/Mi Home app loses its
               bond and must re-add the scooter afterwards. Saves the derived token to --token-file.

  (default)    Login using an existing token (--token-file), then read serial + firmware version
               over the encrypted M365 UART channel to prove the control channel is open.

Examples:
  python mi_auth_run.py --register
  python mi_auth_run.py                 # login with ./mi_token, read serial/fwver
  python mi_auth_run.py --command 55aa032001100e
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from mi_bleak import BleakMi
from miauth.mi.miclient import MiClient

ADDR_DEFAULT = "D8:68:BA:16:A0:33"
TOKEN_DEFAULT = os.path.join(os.path.dirname(__file__), "mi_token")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--addr", default=ADDR_DEFAULT)
    ap.add_argument("--register", action="store_true", help="register (button press, re-keys device)")
    ap.add_argument("--did", help="device id for registration (advanced; else read from device)")
    ap.add_argument("--token-file", default=TOKEN_DEFAULT)
    ap.add_argument("--command", help="raw UART command hex (no checksum), e.g. 55aa032001100e")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ble = BleakMi(args.addr, debug=args.verbose)
    mc = MiClient(ble, debug=args.verbose)

    print("[*] connecting ...")
    mc.connect()
    print("[+] connected")

    if args.register:
        print("[*] REGISTERING — press the scooter power button when it beeps ...")
        mc.register(did=args.did)
        mc.save_token(args.token_file)
        print(f"[+] registered. token saved -> {args.token_file}")
    else:
        if not os.path.isfile(args.token_file):
            print(f"[!] no token at {args.token_file}. Run with --register, or place the token there.")
            ble.close()
            return 2
        mc.load_token(args.token_file)
        print(f"[+] loaded token from {args.token_file}")

    print("[*] logging in ...")
    mc.login()
    print("[+] LOGIN OK — encrypted UART channel open")

    if args.command:
        resp = mc.comm(args.command)
        print("[+] UART reply:", resp.hex(" "))

    # Prove the channel: serial number + firmware version (M365 framing, no checksum).
    try:
        sn = mc.comm("55aa032001100e")
        print("[+] serial:", bytes(sn).decode(errors="replace"))
    except Exception as e:
        print("[!] serial read:", e)
    try:
        fw = mc.comm("55aa0320011a10")
        if fw:
            print("[+] firmware:", f"{fw[0]}.{fw[1]}")
    except Exception as e:
        print("[!] fwver read:", e)

    mc.disconnect()
    ble.close()
    print("[done]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
