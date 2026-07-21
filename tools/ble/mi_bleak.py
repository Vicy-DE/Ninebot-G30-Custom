"""
Bleak (Windows) backend for miauth's MiClient/NbClient/M365Client.

miauth ships only a Linux bluepy backend. This implements the same BLEBase interface on top of
bleak, using the deadlock-free pattern from py9b: the async event loop runs in a background thread
and only does BLE I/O + enqueues inbound notifications into a thread-safe FIFO. miauth's state
machine runs on the *calling* thread; wait_notify() drains the FIFO and invokes the handler there,
so handler-initiated writes (RCV_RDY/RCV_OK/parcels) never deadlock the loop.

Char mapping is fixed for the G30 "G30LD" (Mi product 0x035C), whose fe95 service exposes
0001/0010/0013/0014 (no 0019): control=fe95/0010, data(AVDTP)=fe95/0001(write+notify),
UART TX=NUS 6e400002, UART RX(notify)=NUS 6e400003.
"""
from __future__ import annotations

import asyncio
import queue
import threading
import time

from bleak import BleakClient, BleakScanner

from miauth.ble.base import BLEBase
from miauth.ble.uuid import UUID

# Override miauth's defaults for this device's fe95 layout.
UUID.AVDTP = "00000001-0000-1000-8000-00805f9b34fb"   # was 0x0019
UUID.UPNP = "00000010-0000-1000-8000-00805f9b34fb"
UUID.KEY = "00000014-0000-1000-8000-00805f9b34fb"
UUID.TX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"        # we write UART here
UUID.RX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"        # UART notifications here
GAP_NAME = "00002a00-0000-1000-8000-00805f9b34fb"

# Characteristics we subscribe to for notifications (auth on fe95/0001, UART on NUS/6e400003).
_NOTIFY_CHARS = [UUID.AVDTP, UUID.RX]


class BleakMi(BLEBase):
    def __init__(self, address, name=None, debug=False):
        self.address = address
        self.name = name
        self.debug = debug
        self.handler = None
        self.listen = True
        self._fifo: "queue.Queue[bytes]" = queue.Queue()
        self._client: BleakClient | None = None
        self._subscribed = set()

        self.loop = asyncio.new_event_loop()
        self._th = threading.Thread(target=self._run_loop, daemon=True)
        self._th.start()

    def _run_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def _call(self, coro, timeout=10):
        return asyncio.run_coroutine_threadsafe(coro, self.loop).result(timeout)

    # ---- BLEBase -----------------------------------------------------------
    def set_handler(self, handler):
        self.handler = handler

    def enable_notify(self, uuid):
        pass  # done in connect()

    def read(self, ch):
        return self._call(self._client.read_gatt_char(ch))

    def write(self, ch, data, resp=True):
        self._call(self._client.write_gatt_char(ch, bytes(data), response=resp))
        if self.debug:
            print("  ->", (ch[4:8] if len(ch) > 8 else ch), bytes(data).hex(" "))

    def write_chunked(self, ch, data, resp=True, chunk_size=20):
        for i in range(0, len(data), chunk_size):
            self.write(ch, data[i:i + chunk_size], resp=resp)

    def write_parcel(self, ch, data, resp=True, chunk_size=18):
        for i in range(0, len(data), chunk_size):
            chunk = bytes([i // chunk_size + 1, 0]) + bytes(data[i:i + chunk_size])
            self.write(ch, chunk, resp=resp)

    def connect(self):
        self._call(self._connect_async(), timeout=30)

    async def _connect_async(self):
        # Re-discover each time so reconnects after a power-button press are robust.
        dev = await BleakScanner.find_device_by_address(self.address, timeout=12)
        if dev is None:
            raise RuntimeError(f"device {self.address} not found (scooter on?)")
        self.name = self.name or dev.name
        self._client = BleakClient(dev)
        await self._client.connect()
        # Subscribe to the notify characteristics we care about.
        svc_uuids = {c.uuid.lower() for s in self._client.services for c in s.characteristics}
        for u in _NOTIFY_CHARS:
            if u.lower() in svc_uuids:
                try:
                    await self._client.start_notify(u, self._on_notify)
                    self._subscribed.add(u.lower())
                except Exception:
                    pass

    def _on_notify(self, _char, data: bytearray):
        if self.debug:
            print("  <-", bytes(data).hex(" "))
        self._fifo.put(bytes(data))

    def disconnect(self):
        # Keep the event loop alive: MiClient.register() disconnects then reconnects
        # around the power-button press, reusing this same backend.
        try:
            self._call(self._client.disconnect(), timeout=5)
        except Exception:
            pass
        self._subscribed = set()
        while not self._fifo.empty():
            try:
                self._fifo.get_nowait()
            except queue.Empty:
                break

    def close(self):
        try:
            self._call(self._client.disconnect(), timeout=5)
        except Exception:
            pass
        self.loop.call_soon_threadsafe(self.loop.stop)

    def pause_listening(self):
        self.listen = False

    def resume_listening(self):
        self.listen = True

    def wait_notify(self, secs=1.0):
        """Drain inbound notifications for up to `secs`, dispatching each to the handler
        on THIS thread (so handler-initiated writes don't deadlock the loop)."""
        end = time.time() + secs
        while time.time() < end:
            if not self.listen:
                return
            try:
                data = self._fifo.get(timeout=min(0.1, max(0.0, end - time.time())))
            except queue.Empty:
                continue
            if self.handler is not None:
                self.handler(data)

    def read_device_name(self):
        return self._call(self._client.read_gatt_char(GAP_NAME))

    def has_channel(self, name=None):
        if name is None:
            return True
        return name.lower() in self._subscribed
