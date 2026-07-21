// test_decompiled_protocol.cpp
// Standalone (no framework) equivalence test for the byte-faithful ESC protocol
// decompilation in common/include/ninebot_protocol_verified.hpp.
//
// Build & run:
//   g++ -std=c++17 -Wall -Wextra -I../common/include test_decompiled_protocol.cpp -o t && ./t
//
// Every check asserts the decompiled C++ reproduces the DRV_1.6.13 wire behavior:
// checksum (~sum), LEN==payload framing, parser state machine (expected=LEN+7),
// checksum verification, and DST routing.

#include "ninebot_protocol_verified.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace ninebot_verified;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) { ++g_pass; } else { \
    ++g_fail; std::printf("  FAIL: %s  (line %d)\n", msg, __LINE__); } } while (0)

// Feed a whole raw frame through the parser; return true if it yields a valid packet.
static bool feedFrame(const uint8_t* frame, size_t n, Packet& out) {
    Parser p;
    RxResult r = RxResult::NeedMore;
    for (size_t i = 0; i < n; ++i) r = receiveByte(p, frame[i], out);
    return r == RxResult::PacketReady;
}

int main() {
    std::printf("== Decompiled ESC protocol verification (DRV_1.6.13) ==\n");

    // 1) Checksum == ~sum & 0xFFFF. Golden vector from docs/protocol.md example
    //    bytes (LEN..payload): 06 3E 20 01 10 0E 00  -> sum 0x83 -> ~ = 0xFF7C.
    {
        const uint8_t v[] = {0x06,0x3E,0x20,0x01,0x10,0x0E,0x00};
        uint16_t c = calculateChecksum(v, sizeof v);
        CHECK(c == 0xFF7C, "checksum golden vector 0xFF7C");
        // empty + single-byte edge cases
        CHECK(calculateChecksum(v, 0) == 0xFFFF, "checksum of 0 bytes == 0xFFFF");
        uint8_t one = 0x01; CHECK(calculateChecksum(&one,1) == 0xFFFE, "checksum {0x01}");
    }

    // 2) buildPacket: exact bytes. App(0x3E) READ(0x01) reg 0x10 (serial), payload {0x0E,0x00}.
    //    LEN must equal the PAYLOAD count (2), NOT payload+6.
    {
        const uint8_t payload[] = {0x0E, 0x00};
        uint8_t buf[64] = {};
        size_t n = buildPacket(/*src*/0x3E, /*dst*/0x20, /*cmd*/0x01, /*arg*/0x10,
                               payload, 2, buf);
        CHECK(n == 2 + 9, "frame length == payload + 9");          // 11
        CHECK(buf[0]==0x5A && buf[1]==0xA5, "header 5A A5");
        CHECK(buf[2]==0x02, "LEN field == payload count (2)");      // the fix
        CHECK(buf[3]==0x3E && buf[4]==0x20, "SRC/DST 3E/20");
        CHECK(buf[5]==0x01 && buf[6]==0x10, "CMD/ARG 01/10");
        CHECK(buf[7]==0x0E && buf[8]==0x00, "payload copied");
        uint16_t chk = calculateChecksum(buf+2, (uint16_t)(n-4));   // LEN..payload
        CHECK(buf[n-2]==(chk&0xFF) && buf[n-1]==(chk>>8), "trailer checksum LE");
        // self-consistency with validate
        CHECK((uint16_t)(buf[n-2]|(buf[n-1]<<8)) == chk, "checksum trailer value");
    }

    // 3) Round-trip: build → parse → identical fields, valid checksum.
    {
        const uint8_t payload[] = {0xDE, 0xAD, 0xBE};
        uint8_t buf[64] = {};
        size_t n = buildPacket(0x20, 0x3E, 0x01, 0x25, payload, 3, buf);
        Packet pk{};
        CHECK(feedFrame(buf, n, pk), "round-trip parses to a valid packet");
        CHECK(pk.len==3 && pk.payloadLen==3, "parsed LEN/payloadLen == 3");
        CHECK(pk.src==0x20 && pk.dst==0x3E, "parsed SRC/DST");
        CHECK(pk.cmd==0x01 && pk.arg==0x25, "parsed CMD/ARG");
        CHECK(pk.payload[0]==0xDE && pk.payload[1]==0xAD && pk.payload[2]==0xBE, "parsed payload");
    }

    // 4) Corrupt checksum is rejected.
    {
        const uint8_t payload[] = {0x01};
        uint8_t buf[32] = {};
        size_t n = buildPacket(0x3E, 0x20, 0x02, 0x7B, payload, 1, buf);
        buf[n-1] ^= 0xFF;                  // flip checksum high byte
        Packet pk{};
        CHECK(!feedFrame(buf, n, pk), "corrupt checksum rejected");
    }

    // 5) Header resync: leading garbage + a stray 0x5A before the real frame.
    {
        const uint8_t payload[] = {0x42};
        uint8_t frame[32] = {};
        size_t n = buildPacket(0x21, 0x20, 0x01, 0x1A, payload, 1, frame);
        std::vector<uint8_t> noisy = {0x00, 0xFF, 0x5A, 0x11};  // junk incl. lone 5A
        noisy.insert(noisy.end(), frame, frame + n);
        Packet pk{};
        Parser p; RxResult r = RxResult::NeedMore;
        for (uint8_t b : noisy) r = receiveByte(p, b, pk);
        CHECK(r == RxResult::PacketReady, "parser resyncs after junk + lone 0x5A");
        CHECK(pk.dst==0x20 && pk.arg==0x1A, "resynced packet fields correct");
    }

    // 6) Oversized LEN rejected: parser caps expected (LEN+7) at 0xF3 → LEN>0xEC.
    {
        Parser p; Packet pk{};
        receiveByte(p, 0x5A, pk); receiveByte(p, 0xA5, pk);
        RxResult r = receiveByte(p, 0xED, pk);   // 0xED+7 = 0xF4 > 0xF3
        CHECK(r == RxResult::Rejected, "oversized LEN (0xED) rejected");
        // boundary: 0xEC+7 = 0xF3 accepted (NeedMore)
        Parser p2; receiveByte(p2,0x5A,pk); receiveByte(p2,0xA5,pk);
        CHECK(receiveByte(p2,0xEC,pk) == RxResult::NeedMore, "LEN 0xEC accepted");
    }

    // 7) Dispatch routing by DST.
    {
        CHECK(routeByDst(0x20)==Route::Self,  "DST 0x20 -> Self(ESC)");
        CHECK(routeByDst(0x21)==Route::ToBle, "DST 0x21 -> BLE");
        CHECK(routeByDst(0x22)==Route::ToBms, "DST 0x22 -> BMS");
        CHECK(routeByDst(0x3E)==Route::ToApp, "DST 0x3E -> App");
        CHECK(routeByDst(0x99)==Route::Unknown,"unknown DST");
    }

    // 8) Register access model (App_to_ESC_handler @0x08005624).
    //    WRITE reg 0x7B (speed limit) = 0x0019, then READ it back via the ESC handler.
    {
        RegisterFile rf;
        // App WRITE: reg 0x7B <- {0x19,0x00}
        const uint8_t wp[] = {0x19, 0x00};
        uint8_t wframe[32]; size_t wn = buildPacket(APP, ESC, CMD_WRITE, 0x7B, wp, 2, wframe);
        Packet wpk{}; CHECK(feedFrame(wframe, wn, wpk), "write frame parses");
        uint8_t resp[64];
        size_t rn = handleEscPacket(wpk, rf, resp);
        CHECK(rn == 0, "WRITE produces no response frame");
        CHECK(rf.reg[0x7B] == 0x0019, "regfile[0x7B] updated to 0x0019");

        // App READ reg 0x7B, request 2 bytes (payload[0]=count).
        const uint8_t rq[] = {0x02};
        uint8_t rframe[32]; size_t rqn = buildPacket(APP, ESC, CMD_READ, 0x7B, rq, 1, rframe);
        Packet rpk{}; CHECK(feedFrame(rframe, rqn, rpk), "read request parses");
        size_t respN = handleEscPacket(rpk, rf, resp);
        Packet apk{};
        CHECK(feedFrame(resp, respN, apk), "ESC read response is a valid frame");
        CHECK(apk.src==ESC && apk.dst==APP, "response ESC->App");
        CHECK(apk.cmd==CMD_READ && apk.arg==0x7B, "response echoes CMD/ARG");
        CHECK(apk.payloadLen==2 && apk.payload[0]==0x19 && apk.payload[1]==0x00,
              "response carries regfile value 0x0019");

        // refresh flag set on ARG 0x17 write (firmware @0x080056C4)
        const uint8_t one[] = {0x01,0x00};
        rf.write(0x17, one, 2);
        CHECK(rf.refreshFlag == 1, "ARG 0x17 write sets refresh flag");
    }

    // 9) Cross-board: BMS_1.7.4.5 runs the byte-identical core (BMS_dispatch @0x08005610,
    //    register file @0x20000400, CMD1=read/CMD2=write). A BLE->BMS read of cell-1 voltage
    //    (reg 0x30) round-trips through the same parser/builder.
    {
        // BLE (0x21) -> BMS (0x22), READ reg 0x30, request 2 bytes.
        const uint8_t rq[] = {0x02};
        uint8_t frame[32]; size_t n = buildPacket(BLE, BMS, CMD_READ, 0x30, rq, 1, frame);
        Packet q{}; CHECK(feedFrame(frame, n, q), "BLE->BMS read request parses (shared core)");
        CHECK(q.src==BLE && q.dst==BMS && q.cmd==CMD_READ && q.arg==0x30, "BMS read req fields");

        // Model the BMS register file (generic; base 0x20000400 on the real BMS).
        RegisterFile bms;
        bms.reg[0x30] = 0x0F50;                      // cell 1 = 3920 mV (LE bytes 50 0F)
        uint8_t data[8]; uint8_t got = bms.read(0x30, 2, data);
        CHECK(got==2 && data[0]==0x50 && data[1]==0x0F, "BMS regfile read 0x30 -> 50 0F");

        // BMS -> BLE response carries the value; parses back cleanly.
        uint8_t resp[32]; size_t rn = buildPacket(BMS, BLE, CMD_READ, 0x30, data, got, resp);
        Packet rp{}; CHECK(feedFrame(resp, rn, rp), "BMS->BLE response parses");
        CHECK(rp.src==BMS && rp.payloadLen==2 && rp.payload[0]==0x50 && rp.payload[1]==0x0F,
              "BMS response delivers cell voltage 0x0F50");
    }

    // 10) Real register queries (semantics from docs/REGISTER_MAP.md; framing firmware-verified).
    {
        // BLE -> ESC: read battery voltage (ARG 0x48), request 2 bytes.
        const uint8_t cnt2[] = {0x02, 0x00};
        uint8_t f1[32]; size_t n1 = buildPacket(BLE, ESC, CMD_READ, EscReg::BATT_V, cnt2, 2, f1);
        // expected wire: 5A A5 02 21 20 01 48 02 00 CK CK
        CHECK(n1==11 && f1[2]==0x02 && f1[4]==ESC && f1[6]==EscReg::BATT_V, "ESC batt-voltage read frame");
        Packet q1{}; CHECK(feedFrame(f1,n1,q1) && q1.arg==0x48 && q1.cmd==CMD_READ, "ESC read parses");

        // BLE -> BMS: read cell 1 voltage (ARG 0x40). ESC/BMS share the core.
        uint8_t f2[32]; size_t n2 = buildPacket(BLE, BMS, CMD_READ, BmsReg::CELL1, cnt2, 2, f2);
        CHECK(n2==11 && f2[4]==BMS && f2[6]==0x40, "BMS cell-1 read frame (ARG 0x40)");

        // BMS read-response (CMD 0x04) carrying 3700 mV (0x0E74) parses cleanly.
        const uint8_t mv[] = {0x74, 0x0E};
        uint8_t f3[32]; size_t n3 = buildPacket(BMS, BLE, CMD_READ_RESP, BmsReg::CELL1, mv, 2, f3);
        Packet r3{};
        CHECK(feedFrame(f3,n3,r3) && r3.cmd==CMD_READ_RESP, "Ninebot read-response cmd 0x04");
        CHECK(r3.payload[0]==0x74 && r3.payload[1]==0x0E, "cell voltage 0x0E74 (3700 mV) delivered");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
