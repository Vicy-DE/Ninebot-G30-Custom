"""Self-test for the NinebotCrypto port. No hardware needed.

Validates the algorithm's internal consistency: the CryptoFirst involution, frame structure,
the CryptoNext encrypt/decrypt symmetry (incl. the 4-byte CRC and the 16-bit message counter),
and counter rollover across the 0xFFFF boundary. The definitive check is the live 0x5B handshake.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from ninebot_crypto import NinebotCrypto, FW_KEY

NAME = "G30LD"
fails = 0


def check(cond, msg):
    global fails
    print(("  ok  " if cond else " FAIL ") + msg)
    if not cond:
        fails += 1


# 1) CryptoFirst is its own inverse (pure XOR keystream).
a = NinebotCrypto(NAME)
pt = bytes(range(34))
ct = a._crypto_first(pt)
check(a._crypto_first(ct) == pt, "CryptoFirst involution")
check(ct != pt, "CryptoFirst actually transforms data")

# 2) encrypt() of the 0x5B request has the right shape: LEN+13 on the wire, header preserved,
#    LEN..payload body is encrypted (differs from plaintext).
a = NinebotCrypto(NAME)
req = bytes([0x5A, 0xA5, 0x00, 0x3E, 0x21, 0x5B, 0x00])  # 5B, no payload
wire = a.encrypt(req)
check(len(wire) == req[2] + 13, "0x5B wire length == LEN+13 (=%d)" % len(wire))
check(wire[0:3] == req[0:3], "header 5A A5 LEN preserved in clear")
check(wire[3:7] != req[3:7], "SRC/DST/CMD/ARG encrypted")
check(a._msg_it == 1, "msg_it incremented to 1 after first encrypt")

# 3) Round-trip the 0x5B request through a peer that shares the same initial key.
peer = NinebotCrypto(NAME)
dec = peer.decrypt(wire)
check(dec == req, "peer.decrypt(encrypt(0x5B)) recovers the request")

# 4) Simulate a scooter 0x5B reply -> app extracts ble_data and rekeys.
scooter = NinebotCrypto(NAME)
ble_key = bytes(range(0x10, 0x20))          # 16 bytes the scooter would pick
sn = b"N3GZ1234567890"                       # 14-byte serial
reply_pt = bytes([0x5A, 0xA5, 0x1E, 0x21, 0x3E, 0x5B, 0x01]) + ble_key + sn
reply_wire = scooter.encrypt(reply_pt)        # scooter's first msg (msg_it 0->1)
app = NinebotCrypto(NAME)
app._msg_it = 1                               # app already sent its 0x5B
got = app.decrypt(reply_wire)
check(got == reply_pt, "0x5B reply decrypts cleanly")
check(bytes(app._ble) == ble_key, "app extracted ble_data (session key material)")
check(got[23:37] == sn, "serial number recoverable at payload offset 16")

# 5) CryptoNext symmetry: force identical post-pairing state in two instances, then a
#    data frame encrypted by one must decrypt in the other (exercises CTR keystream + CRC + counter).
def paired(msg_it):
    c = NinebotCrypto(NAME)
    c._ble = bytearray(ble_key)
    c._app = bytearray(range(0x20, 0x30))
    c._calc_sha1(c._app, c._ble)
    c._msg_it = msg_it
    return c

for start in (1, 0x0050, 0x7FFF, 0x8FFF):
    tx = paired(start)
    rx = paired(start)
    frame = bytes([0x5A, 0xA5, 0x06, 0x3E, 0x21, 0x64, 0x00]) + bytes([1, 2, 3, 4, 5, 6])
    w = tx.encrypt(frame)
    out = rx.decrypt(w)
    check(out == frame, "CryptoNext round-trip @msg_it=0x%04X" % start)
    check(rx._msg_it == start + 1, "rx counter advanced to 0x%04X" % (start + 1))

# 6) Counter rollover across 0xFFFF.
tx = paired(0xFFFF)
rx = paired(0xFFFF)
frame = bytes([0x5A, 0xA5, 0x02, 0x3E, 0x21, 0x01, 0x00]) + bytes([0xAA, 0xBB])
w = tx.encrypt(frame)
check(tx._msg_it == 0x10000, "tx counter rolled to 0x10000")
out = rx.decrypt(w)
check(out == frame, "CryptoNext round-trip across 0xFFFF rollover")
check(rx._msg_it == 0x10000, "rx counter rolled to 0x10000")

print("\n%s — %d failure(s)" % ("PASS" if fails == 0 else "FAIL", fails))
sys.exit(1 if fails else 0)
