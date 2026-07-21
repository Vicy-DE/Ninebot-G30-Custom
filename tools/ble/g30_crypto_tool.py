"""
ninebot_ble.py — talk to a Ninebot G30 dashboard over BLE using NinebotCrypto.

This is the BLE channel the Segway-Ninebot app uses. It needs NO cloud token and NO account:
the only secret is the fixed firmware key (in ninebot_crypto.py) plus a power-button press during
pairing. Flow:

    scan -> connect (Nordic UART Service) -> 0x5B (get serial) -> 0x5C (key exchange, press button)
         -> 0x5D (confirm) -> paired -> ReadRegs (serial / version) as proof of an open channel.

Once paired you have the full Ninebot command set (ReadRegs 0x01, WriteRegs 0x02, ReadMem 0x80,
IAP 0x07/08/09/0A) — i.e. the path to flashing custom BLE firmware (project Phase 1).

Usage:
    python ninebot_ble.py                 # scan, connect, pair, read regs
    python ninebot_ble.py --addr D8:68:BA:16:A0:33
    python ninebot_ble.py --name G30LD --pair-timeout 60
"""

from __future__ import annotations

import argparse
import asyncio
import os
import sys
from struct import pack, unpack

sys.path.insert(0, os.path.dirname(__file__))
from ninebot_crypto import NinebotCrypto

from bleak import BleakScanner, BleakClient

NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # we write here (-> scooter)
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # we notify here (<- scooter)
GAP_NAME = "00002a00-0000-1000-8000-00805f9b34fb"  # Device Name characteristic

HOST = 0x3E
BLE = 0x21
ESC = 0x20


def hexs(b) -> str:
    return " ".join("%02X" % x for x in b)


class NinebotBLE:
    def __init__(self, client: BleakClient, name: str):
        self.client = client
        self.crypto = NinebotCrypto(name)
        self.name = name
        self._buf = bytearray()
        self._frames: "asyncio.Queue[bytes]" = asyncio.Queue()
        self._rx_char = None
        self._rx_response = True

    async def start(self):
        # Pick the RX characteristic and decide write mode from its properties.
        for svc in self.client.services:
            for ch in svc.characteristics:
                if ch.uuid.lower() == NUS_RX:
                    self._rx_char = ch
                    props = [p.lower() for p in ch.properties]
                    self._rx_response = "write" in props  # else write-without-response
        if self._rx_char is None:
            raise RuntimeError("Nordic UART RX characteristic not found (not a Ninebot BLE?)")
        await self.client.start_notify(NUS_TX, self._on_notify)

    def _on_notify(self, _sender, data: bytearray):
        # Reassemble Ninebot frames from arbitrary BLE chunk boundaries.
        self._buf += data
        while True:
            # Resync to the 5A A5 preamble.
            while len(self._buf) >= 1 and self._buf[0] != 0x5A:
                del self._buf[0]
            if len(self._buf) >= 2 and self._buf[1] != 0xA5:
                del self._buf[0]
                continue
            if len(self._buf) < 3:
                return
            total = self._buf[2] + 13  # 3 header + (4+LEN) body + 6 crypto trailer
            if len(self._buf) < total:
                return
            frame = bytes(self._buf[:total])
            del self._buf[:total]
            self._frames.put_nowait(frame)

    async def _send(self, plaintext: bytes):
        wire = self.crypto.encrypt(plaintext)
        # Chunk to 20 bytes — the classic nRF UART write size; the bridge re-streams to the STM32.
        for i in range(0, len(wire), 20):
            await self.client.write_gatt_char(
                self._rx_char, wire[i:i + 20], response=self._rx_response
            )

    async def _recv(self, timeout: float):
        wire = await asyncio.wait_for(self._frames.get(), timeout)
        return self.crypto.decrypt(wire)

    async def _recv_cmd(self, cmd: int, timeout: float):
        """Decrypt frames in order until one with the given CMD; return (arg, payload)."""
        loop = asyncio.get_event_loop()
        end = loop.time() + timeout
        while True:
            remaining = end - loop.time()
            if remaining <= 0:
                raise asyncio.TimeoutError
            dec = await self._recv(remaining)
            src, dst, c, arg = dec[3], dec[4], dec[5], dec[6]
            payload = dec[7:]
            tag = "  rx %02X->%02X cmd=%02X arg=%02X [%s]" % (src, dst, c, arg, hexs(payload))
            print(tag if c == cmd else tag + "  (waiting for %02X)" % cmd)
            if c == cmd:
                return arg, payload

    # ---- handshake --------------------------------------------------------
    async def pair(self, pair_timeout: float = 60.0) -> bytes:
        print("[*] 0x5B  request pairing / read serial")
        await self._send(bytes([0x5A, 0xA5, 0x00, HOST, BLE, 0x5B, 0x00]))
        arg, payload = await self._recv_cmd(0x5B, timeout=6.0)
        # payload = 16 bytes ble_data (already consumed by crypto) + 14 bytes serial
        serial = payload[16:30]
        print("[+] paired-flag=%d  serial=%r (%s)" % (arg, bytes(serial), hexs(serial)))

        # 0x5C key exchange — fixed 16 random bytes for the whole session.
        app_key = bytes(os.urandom(16))
        c5 = bytes([0x5A, 0xA5, 0x10, HOST, BLE, 0x5C, 0x00]) + app_key
        print("[*] 0x5C  key exchange — PRESS THE SCOOTER POWER BUTTON NOW")
        loop = asyncio.get_event_loop()
        end = loop.time() + pair_timeout
        while loop.time() < end:
            await self._send(c5)
            try:
                arg, _ = await self._recv_cmd(0x5C, timeout=1.2)
            except asyncio.TimeoutError:
                print("    ... waiting for power-button press")
                continue
            if arg == 0x01:
                print("[+] 0x5C arg=01 — power button pressed, key accepted")
                break
            else:
                print("[i] 0x5C arg=%02X — key acknowledged, still waiting for button (01)" % arg)
        else:
            raise TimeoutError("0x5C timed out — power button not pressed in time")

        # 0x5D confirm with the serial number.
        print("[*] 0x5D  confirm pairing")
        for attempt in range(6):
            await self._send(bytes([0x5A, 0xA5, 0x0E, HOST, BLE, 0x5D, 0x00]) + bytes(serial))
            try:
                arg, _ = await self._recv_cmd(0x5D, timeout=2.0)
            except asyncio.TimeoutError:
                print("    retry 0x5D (%d)" % (attempt + 1))
                continue
            if arg == 0x01:
                print("[+] 0x5D arg=01 — PAIRED. Encrypted channel is open.")
                return bytes(serial)
        raise RuntimeError("0x5D not confirmed")

    # ---- post-pairing commands -------------------------------------------
    async def read_reg(self, dev: int, reg: int, size: int, timeout: float = 3.0) -> bytes:
        await self._send(bytes([0x5A, 0xA5, 0x01, HOST, dev, 0x01, reg, size]))
        arg, payload = await self._recv_cmd(0x01, timeout=timeout)
        if arg != reg:
            raise RuntimeError("ReadRegs reg mismatch: asked %02X got %02X" % (reg, arg))
        return payload[:size]


async def find_device(addr: str | None, name: str | None, scan_time: float):
    print("[*] scanning %.0fs ..." % scan_time)
    devices = await BleakScanner.discover(timeout=scan_time)
    for d in devices:
        dn = d.name or ""
        if addr and d.address.upper() == addr.upper():
            return d
        if not addr and (("G30" in dn) or (name and name.lower() in dn.lower())):
            return d
    # Fallback: print what we saw.
    print("    seen:", ", ".join("%s/%s" % (d.name, d.address) for d in devices if d.name))
    return None


async def main():
    ap = argparse.ArgumentParser(description="Ninebot G30 BLE (NinebotCrypto) client")
    ap.add_argument("--addr", help="BLE MAC to connect to directly")
    ap.add_argument("--name", default="G30LD", help="device name hint / crypto name")
    ap.add_argument("--scan-time", type=float, default=8.0)
    ap.add_argument("--pair-timeout", type=float, default=60.0)
    ap.add_argument("--no-read", action="store_true", help="pair only, skip register reads")
    args = ap.parse_args()

    dev = await find_device(args.addr, args.name, args.scan_time)
    if dev is None:
        print("[!] device not found. Is the scooter ON and in range?")
        return 2
    print("[+] found %s / %s" % (dev.name, dev.address))

    async with BleakClient(dev) as client:
        print("[+] connected")
        # Authoritative crypto name = GATT Device Name char, else advertised name.
        crypto_name = dev.name or args.name
        try:
            gap = await client.read_gatt_char(GAP_NAME)
            if gap:
                crypto_name = gap.decode("ascii", "ignore").strip("\x00") or crypto_name
        except Exception:
            pass
        print("[*] crypto name = %r" % crypto_name)

        nb = NinebotBLE(client, crypto_name)
        await nb.start()

        serial = await nb.pair(pair_timeout=args.pair_timeout)

        if not args.no_read:
            print("\n[*] reading registers over the encrypted channel:")
            try:
                sn = await nb.read_reg(BLE, 0x10, 14)
                print("[+] BLE serial reg(0x10) = %r" % bytes(sn))
            except Exception as e:
                print("[!] read serial:", e)
            for reg, size, label in [(0x1A, 2, "BLE fw ver"), (0x17, 2, "ESC<->BLE")]:
                try:
                    v = await nb.read_reg(BLE, reg, size)
                    print("[+] %s reg(0x%02X) = %s" % (label, reg, hexs(v)))
                except Exception as e:
                    print("[!] read 0x%02X: %s" % (reg, e))
        print("\n[done] paired serial = %r" % bytes(serial))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(asyncio.run(main()))
    except KeyboardInterrupt:
        pass
