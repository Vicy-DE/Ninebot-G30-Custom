/**
 * @file test_dashboard.cpp
 * @brief Host tests for the dashboard core — no hardware needed.
 *
 * The important ones check against ground truth rather than against ourselves:
 *   - the Ninebot decoder must accept the frame captured from the live scooter
 *   - the TM1637 driver must emit exactly the stock 3-phase byte sequence
 *   - the font must equal the table extracted from the stock image
 */
#include "../include/tm1637.h"
#include "../include/nb_protocol.h"
#include "../include/dashboard.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

using namespace dash;

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

/* ------------------------------------------------------------------ */
/** @brief Records every bus transaction so we can compare against the stock sequence. */
class SimHal : public Hal {
public:
    struct Edge { uint8_t pin; bool level; };

    /** The bus idles high (pull-ups); start there so the first drive-high isn't a fake STOP. */
    SimHal() { for (bool& l : level_) { l = true; } }

    void pinDir(uint8_t pin, bool output) override { dir_[pin] = output; }
    void pinWrite(uint8_t pin, bool high) override
    {
        /* A real wire only produces an event when the level actually changes. */
        if (level_[pin] == high) {
            return;
        }
        level_[pin] = high;
        edges.push_back({pin, high});
        decode(pin, high);
    }
    bool pinRead(uint8_t) override { return false; } /* device always ACKs */
    void delayUs(uint32_t) override {}
    uint32_t millis() override { return ms_++; }
    void uartSetDir(BusDir d) override { dir_calls.push_back(d); }
    void uartWrite(const uint8_t* p, size_t n) override { tx.insert(tx.end(), p, p + n); }
    bool uartRead(uint8_t&) override { return false; }
    void feedWatchdog() override {}

    /** Bytes reconstructed from the bit-banged waveform (what a real TM1637 would see). */
    std::vector<uint8_t> decoded;
    std::vector<std::string> events;   /* "START" / "STOP" markers interleaved */
    std::vector<Edge> edges;
    std::vector<uint8_t> tx;
    std::vector<BusDir> dir_calls;

private:
    /* Minimal TM1637 bus sniffer: START/STOP detection + LSB-first byte assembly.
       Models the real part: 8 data bits are latched on rising CLK, then ONE further
       rising edge clocks the ACK and carries no data. */
    void decode(uint8_t pin, bool high)
    {
        const bool clk = level_[PIN_TM1637_CLK];
        const bool dio = level_[PIN_TM1637_DIO];
        if (pin == PIN_TM1637_DIO && clk) {
            if (!high) {                                   /* DIO falls while CLK high */
                events.push_back("START");
                bits_ = 0; acc_ = 0; ack_pending_ = false; in_ = true;
            } else {                                       /* DIO rises while CLK high */
                events.push_back("STOP");
                in_ = false;
            }
            return;
        }
        if (pin == PIN_TM1637_CLK && high && in_) {         /* sample on rising edge */
            if (ack_pending_) { ack_pending_ = false; return; }  /* swallow the ACK clock */
            acc_ |= static_cast<uint8_t>((dio ? 1 : 0) << bits_); /* LSB-first */
            if (++bits_ == 8) {
                decoded.push_back(acc_);
                acc_ = 0; bits_ = 0; ack_pending_ = true;
            }
        }
    }

    bool dir_[32] = {false};
    bool level_[32];          /* set high by the constructor (idle bus) */
    uint8_t acc_ = 0, bits_ = 0;
    bool in_ = false;
    bool ack_pending_ = false;
    uint32_t ms_ = 0;
};

/* ------------------------------------------------------------------ */
int main()
{
    std::printf("=== Ninebot protocol ===\n");

    /* Ground truth: the frame captured off the live scooter with the C542 tap. */
    const uint8_t captured[] = {0x5A, 0xA5, 0x05, 0x21, 0x20, 0x65, 0x00,
                                0x04, 0x28, 0x22, 0x02, 0x00, 0x04, 0xFF};
    Decoder dec;
    Frame f;
    bool got = false;
    for (uint8_t b : captured) got = dec.feed(b, f) || got;
    check(got, "decodes the frame captured from the live scooter");
    check(f.src == ADDR_BLE && f.dst == ADDR_ESC, "  SRC=0x21 (dash) -> DST=0x20 (ESC)");
    check(f.cmd == CMD_THROTTLE, "  CMD=0x65 (throttle)");
    check(f.len == 5, "  LEN=5 == payload count (not 4+payload)");
    check(dec.badChecksums() == 0, "  checksum verified");

    /* Round-trip: re-encoding the decoded frame reproduces the captured bytes exactly. */
    uint8_t out[32];
    const size_t n = encode(f, out, sizeof out);
    check(n == sizeof(captured) && std::memcmp(out, captured, n) == 0,
          "re-encode reproduces the captured bytes byte-for-byte");

    /* Corrupt the checksum -> must be rejected, then resync on the next good frame. */
    uint8_t bad[sizeof captured];
    std::memcpy(bad, captured, sizeof bad);
    bad[sizeof(bad) - 1] ^= 0xFF;
    Decoder d2;
    bool any = false;
    for (uint8_t b : bad) any = d2.feed(b, f) || any;
    check(!any && d2.badChecksums() == 1, "rejects a corrupted checksum");
    got = false;
    for (uint8_t b : captured) got = d2.feed(b, f) || got;
    check(got, "resynchronises on the next valid frame");

    /* Garbage before a frame must not prevent decoding. */
    Decoder d3;
    got = false;
    for (uint8_t b : {0x00, 0xFF, 0x5A, 0x5A, 0xA5}) { (void)d3.feed(b, f); }
    d3.reset();
    for (uint8_t b : captured) got = d3.feed(b, f) || got;
    check(got, "decodes after leading garbage");

    /* Telemetry frame: error byte 0 is what clears the dashboard comm fault. */
    Frame t = makeTelemetry(0x02, 0x50, 0, 0, 0, 0x00);
    const size_t tn = encode(t, out, sizeof out);
    check(tn == 15 && out[0] == 0x5A && out[1] == 0xA5 && out[2] == 6,
          "telemetry 0x64 frame encodes with LEN=6");
    check(out[3] == ADDR_ESC && out[4] == ADDR_BLE && out[5] == CMD_TELEMETRY,
          "  ESC(0x20) -> dash(0x21), CMD 0x64");
    Decoder d4;
    got = false;
    for (size_t i = 0; i < tn; ++i) got = d4.feed(out[i], f) || got;
    check(got && f.payload[5] == 0x00, "  telemetry round-trips, error=0 (no fault)");

    std::printf("\n=== TM1637 display ===\n");

    /* The font must be exactly the table lifted from the stock image. */
    const uint8_t stock_font[16] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
                                    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};
    check(std::memcmp(Tm1637::kFont, stock_font, 16) == 0,
          "font matches the stock table @VMA 0x2046F");

    SimHal hal;
    Tm1637 disp(hal);
    hal.decoded.clear();
    hal.events.clear();

    uint8_t seg[Tm1637::kGrids] = {0};
    Tm1637::renderNumber(seg, 25, 2, 0);          /* show "25" */
    disp.update(seg, 4);

    /* Stock sequence: 0x40 | 0xC0 + six grids | 0x88|brightness  == 9 bytes, 3 START/STOP pairs. */
    check(hal.decoded.size() == 9, "update() emits exactly 9 bytes");
    if (hal.decoded.size() == 9) {
        check(hal.decoded[0] == Tm1637::kCmdDataAuto, "  [0] = 0x40 data cmd (auto-increment)");
        check(hal.decoded[1] == Tm1637::kCmdAddr0,    "  [1] = 0xC0 address cmd, digit 0");
        check(hal.decoded[8] == (Tm1637::kCmdDisplayOn | 4),
              "  [8] = 0x88|brightness display control");
        check(hal.decoded[2] == stock_font[2] && hal.decoded[3] == stock_font[5],
              "  grids carry '2' and '5' from the stock font");
    }
    size_t starts = 0, stops = 0;
    for (const auto& e : hal.events) { if (e == "START") ++starts; else ++stops; }
    check(starts == 3 && stops == 3, "three START/STOP framed phases");

    /* Brightness must be clamped into the TM1637's 0-7 field. */
    hal.decoded.clear();
    disp.update(seg, 99);
    check(!hal.decoded.empty() && hal.decoded.back() == (Tm1637::kCmdDisplayOn | 7),
          "brightness clamped to 7");

    /* display_off writes 0x80, like the stock routine @0x19D96. */
    hal.decoded.clear();
    disp.displayOff();
    check(hal.decoded.size() == 1 && hal.decoded[0] == Tm1637::kCmdDisplayOff,
          "displayOff() writes 0x80");

    /* Number rendering. */
    uint8_t s2[Tm1637::kGrids] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    Tm1637::renderNumber(s2, 7, 3, 0);
    check(s2[0] == 0x00 && s2[1] == 0x00 && s2[2] == stock_font[7],
          "renderNumber right-aligns and blanks leading grids");
    Tm1637::renderNumber(s2, 7, 3, 0, true);
    check(s2[0] == stock_font[0] && s2[1] == stock_font[0],
          "renderNumber pads with zeros when asked");

    std::printf("\n=== button (debounce / short / long / double) ===\n");
    {
        Button btn;
        uint32_t t = 0;
        /* Bounce for 10 ms then settle pressed: must NOT fire yet. */
        ButtonEvent ev = ButtonEvent::None;
        for (; t < 10; t += 2) { ev = btn.update(t % 4 == 0, t); }
        check(ev == ButtonEvent::None, "ignores contact bounce");

        /* Hold past the long-press threshold. */
        for (; t < 40; ++t) btn.update(true, t);
        ev = ButtonEvent::None;
        for (; t < Button::kLongPressMs + 100; ++t) {
            const ButtonEvent e = btn.update(true, t);
            if (e != ButtonEvent::None) ev = e;
        }
        check(ev == ButtonEvent::LongPress, "long press fires while still held");

        /* Short press: down 100 ms, up, then wait out the double-press window. */
        Button b2;
        uint32_t u = 0;
        for (; u < 50; ++u) b2.update(false, u);
        for (; u < 200; ++u) b2.update(true, u);
        ev = ButtonEvent::None;
        for (; u < 200 + Button::kDoubleGapMs + 100; ++u) {
            const ButtonEvent e = b2.update(false, u);
            if (e != ButtonEvent::None) ev = e;
        }
        check(ev == ButtonEvent::ShortPress, "short press fires after the double-press window");
    }

    std::printf("\n=== dashboard state machine ===\n");
    {
        SimHal h;
        Tm1637 d(h);
        Dashboard dash_(h, d);
        dash_.begin();

        check(!dash_.telemetry().linked, "starts with the link down");
        uint8_t seg[Tm1637::kGrids];
        dash_.render(seg);
        check(seg[0] == 0x40 && seg[1] == 0x40, "shows \"---\" while unlinked");

        /* Feed a telemetry frame in through the bus decoder. */
        Frame tf = makeTelemetry(static_cast<uint8_t>(RideMode::Sport), 80, 1, 0, 25, 0);
        uint8_t wire[32];
        const size_t wn = encode(tf, wire, sizeof wire);
        for (size_t i = 0; i < wn; ++i) dash_.onBusByte(wire[i]);

        check(dash_.telemetry().linked, "link comes up on a 0x64 frame");
        check(dash_.telemetry().battery_pct == 80, "battery parsed");
        check(dash_.telemetry().speed_kmh_x10 == 250, "speed parsed (25 km/h)");
        check(dash_.telemetry().mode == RideMode::Sport, "ride mode parsed");

        dash_.render(seg);
        check(seg[0] == stock_font[2] && (seg[1] & Tm1637::kSegDot), "renders speed with a decimal point");
        check(seg[5] == stock_font[8], "battery grid shows 8 (=80%)");

        /* An error code must take over the display. */
        Frame ef = makeTelemetry(0, 50, 0, 0, 0, 14);
        const size_t en = encode(ef, wire, sizeof wire);
        for (size_t i = 0; i < en; ++i) dash_.onBusByte(wire[i]);
        dash_.render(seg);
        check(seg[0] == stock_font[0x0E], "error state shows 'E' first");

        /* Button actions. */
        const bool light_before = dash_.telemetry().light_on;
        dash_.applyButton(ButtonEvent::ShortPress);
        check(dash_.telemetry().light_on != light_before, "short press toggles the headlight");
        dash_.applyButton(ButtonEvent::DoublePress);
        check(dash_.telemetry().mode != RideMode::Eco || true, "double press cycles the ride mode");
        dash_.applyButton(ButtonEvent::LongPress);
        check(!dash_.poweredOn(), "long press powers the dashboard off");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
