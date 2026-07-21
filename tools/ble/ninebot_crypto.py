"""
NinebotCrypto — the AES-128 BLE crypto used by Xiaomi/Ninebot scooters (G30 Max etc.).

Faithful Python port of scooterhacking/NinebotCrypto (C++ reference — the repo's C port is
flagged broken in issue #16, so this follows NinebotCrypto.cpp line-for-line). Based on majsi's
reverse engineering. There is NO cloud token anywhere in this protocol: the only secret is the
fixed firmware key below plus a power-button press during pairing.

Frame model (plaintext, what you pass to encrypt() / get back from decrypt()):

    5A A5 | LEN | SRC DST CMD ARG | payload[LEN]

  - LEN counts only `payload` (SRC/DST/CMD/ARG are not included), matching the live-bus capture.
  - encrypt() appends the 6-byte crypto trailer (CRC + message counter); decrypt() strips it.
  - On the wire an encrypted frame is `LEN + 13` bytes total (3 header + (4+LEN) body + 6 trailer).

Key schedule:
    sha1_key      = SHA1(name16 || FW_KEY)[:16]           (initial)
    after 0x5B rx: sha1_key = SHA1(name16 || ble_data)[:16]
    after 0x5C rx: sha1_key = SHA1(app_data || ble_data)[:16]

Use one persistent instance per connection; call encrypt() on every outbound frame and decrypt()
on every inbound frame, in order, so the message counter and key transitions stay in sync.
"""

from __future__ import annotations

import hashlib
from Crypto.Cipher import AES  # pycryptodome

# Fixed "firmware" key — identical across all supported Xiaomi/Ninebot BLE versions.
FW_KEY = bytes(
    [0x97, 0xCF, 0xB8, 0x02, 0x84, 0x41, 0x43, 0xDE,
     0x56, 0x00, 0x2B, 0x3B, 0x34, 0x78, 0x0A, 0x5D]
)


def _aes_ecb(block16: bytes, key16: bytes) -> bytes:
    """AES-128-ECB encrypt a single 16-byte block."""
    return AES.new(bytes(key16), AES.MODE_ECB).encrypt(bytes(block16))


def _sha1_16(a16: bytes, b16: bytes) -> bytes:
    """SHA1(a16 || b16) truncated to 16 bytes."""
    return hashlib.sha1(bytes(a16) + bytes(b16)).digest()[:16]


def _xor(a: bytes, b: bytes) -> bytes:
    return bytes(x ^ y for x, y in zip(a, b))


class NinebotCrypto:
    def __init__(self, name):
        if isinstance(name, str):
            name = name.encode("ascii", "ignore")
        # 16-byte zero-padded device (BLE advertised) name.
        self._name = bytes(name[:16]).ljust(16, b"\x00")
        self._fw = FW_KEY
        self._ble = bytearray(16)   # _random_ble_data — key material from the 0x5B reply
        self._app = bytearray(16)   # _random_app_data — the 16 random bytes we send in 0x5C
        self._msg_it = 0
        self._sha1 = bytearray(_sha1_16(self._name, self._fw))

    # ---- key schedule -----------------------------------------------------
    def _calc_sha1(self, d1: bytes, d2: bytes):
        self._sha1 = bytearray(_sha1_16(d1, d2))

    # ---- the two keystreams ----------------------------------------------
    def _crypto_first(self, data: bytes) -> bytes:
        """msg_it == 0: XOR with a single AES(FW_KEY, sha1_key) block (involution)."""
        ks = _aes_ecb(self._fw, self._sha1)
        out = bytearray(len(data))
        i = 0
        n = len(data)
        while n > 0:
            tmp = 16 if n > 16 else n
            for j in range(tmp):
                out[i + j] = data[i + j] ^ ks[j]
            n -= tmp
            i += tmp
        return bytes(out)

    def _crypto_next(self, data: bytes, msg_it: int) -> bytes:
        """msg_it > 0: AES-CTR-style keystream keyed by sha1_key, nonce = counter+ble_data."""
        enc = bytearray(16)
        enc[0] = 1
        enc[1] = (msg_it >> 24) & 0xFF
        enc[2] = (msg_it >> 16) & 0xFF
        enc[3] = (msg_it >> 8) & 0xFF
        enc[4] = msg_it & 0xFF
        enc[5:13] = self._ble[0:8]
        enc[13] = enc[14] = enc[15] = 0

        out = bytearray(len(data))
        i = 0
        n = len(data)
        while n > 0:
            enc[15] = (enc[15] + 1) & 0xFF
            ks = _aes_ecb(enc, self._sha1)
            tmp = 16 if n > 16 else n
            for j in range(tmp):
                out[i + j] = data[i + j] ^ ks[j]
            n -= tmp
            i += tmp
        return bytes(out)

    # ---- CRCs -------------------------------------------------------------
    @staticmethod
    def _crc_first(payload: bytes) -> bytes:
        crc = (~sum(payload)) & 0xFFFF
        return bytes([crc & 0xFF, (crc >> 8) & 0xFF])

    def _crc_next(self, data: bytes, msg_it: int) -> bytes:
        """CBC-MAC-like 4-byte tag over the full frame (incl. 3-byte header)."""
        payload_len = len(data) - 3
        enc = bytearray(16)
        enc[0] = 89
        enc[1] = (msg_it >> 24) & 0xFF
        enc[2] = (msg_it >> 16) & 0xFF
        enc[3] = (msg_it >> 8) & 0xFF
        enc[4] = msg_it & 0xFF
        enc[5:13] = self._ble[0:8]
        enc[15] = payload_len & 0xFF  # matches C++ (low byte of len)

        xor2 = _aes_ecb(enc, self._sha1)

        blk = bytearray(16)
        blk[0:3] = data[0:3]
        xor2 = _aes_ecb(_xor(blk, xor2), self._sha1)

        n = payload_len
        idx = 3
        while n > 0:
            tmp = 16 if n > 16 else n
            blk = bytearray(16)
            blk[0:tmp] = data[idx:idx + tmp]
            xor2 = _aes_ecb(_xor(blk, xor2), self._sha1)
            n -= tmp
            idx += tmp

        enc[0] = 1
        enc[15] = 0
        ks2 = _aes_ecb(enc, self._sha1)
        return _xor(ks2, xor2)[0:4]

    # ---- public API -------------------------------------------------------
    def encrypt(self, frame: bytes) -> bytes:
        """frame = 5A A5 LEN SRC DST CMD ARG payload  (no CRC). Returns the wire bytes."""
        frame = bytes(frame)
        out = bytearray(frame[0:3])
        payload = frame[3:]
        payload_len = len(payload)

        if self._msg_it == 0:
            crc = self._crc_first(payload)
            out += self._crypto_first(payload)
            out += bytes([0, 0, crc[0], crc[1], 0, 0])
            self._msg_it += 1
        else:
            self._msg_it += 1
            crc = self._crc_next(frame, self._msg_it)
            out += self._crypto_next(payload, self._msg_it)
            out += bytes([crc[0], crc[1], crc[2], crc[3],
                          (self._msg_it >> 8) & 0xFF, self._msg_it & 0xFF])
            # Save the 16 random bytes we put in the 0x5C key-exchange frame.
            if frame[0:7] == b"\x5A\xA5\x10\x3E\x21\x5C\x00":
                self._app = bytearray(frame[7:23])
        return bytes(out)

    def decrypt(self, wire: bytes) -> bytes:
        """wire = full received encrypted frame. Returns 5A A5 LEN SRC DST CMD ARG payload."""
        wire = bytes(wire)
        dec = bytearray(len(wire) - 6)
        dec[0:3] = wire[0:3]

        new_it = self._msg_it
        if (new_it & 0x8000) > 0 and (wire[-2] >> 7) == 0:
            new_it += 0x10000
        new_it = (new_it & 0xFFFF0000) + (wire[-2] << 8) + wire[-1]

        payload = wire[3:len(wire) - 6]

        if new_it == 0:
            dec[3:] = self._crypto_first(payload)
            if dec[0:6] == b"\x5A\xA5\x1E\x21\x3E\x5B":
                self._ble = bytearray(dec[7:23])
                self._calc_sha1(self._name, self._ble)
        elif new_it > self._msg_it:
            dec[3:] = self._crypto_next(payload, new_it)
            if dec[0:7] == b"\x5A\xA5\x00\x21\x3E\x5C\x01":
                self._calc_sha1(self._app, self._ble)
            self._msg_it = new_it
        else:
            # Replay / stale counter — decrypt with current counter anyway (best effort).
            dec[3:] = self._crypto_next(payload, new_it) if new_it else self._crypto_first(payload)
        return bytes(dec)


__all__ = ["NinebotCrypto", "FW_KEY"]
