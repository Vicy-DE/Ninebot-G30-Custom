/**
 * @file dashboard.cpp
 * @brief Dashboard state machine: bus telemetry -> display, button -> mode/light/power.
 */
#include "dashboard.h"

namespace dash {

/* ---------------------------------------------------------------- Button */

ButtonEvent Button::update(bool pressed, uint32_t now_ms)
{
    if (pressed != raw_) {              /* level changed — restart the debounce window */
        raw_ = pressed;
        changed_at_ = now_ms;
    }

    ButtonEvent ev = ButtonEvent::None;

    if (raw_ != stable_ && (now_ms - changed_at_) >= kDebounceMs) {
        stable_ = raw_;
        if (stable_) {                  /* press edge */
            pressed_at_ = now_ms;
            long_fired_ = false;
        } else {                        /* release edge */
            released_at_ = now_ms;
            if (!long_fired_) {
                if (pending_short_ && (now_ms - released_at_) <= kDoubleGapMs) {
                    pending_short_ = false;
                    ev = ButtonEvent::DoublePress;
                } else {
                    pending_short_ = true;   /* wait to see if a second press follows */
                }
            }
        }
    }

    /* Long press fires while still held, like the stock power-off. */
    if (stable_ && !long_fired_ && (now_ms - pressed_at_) >= kLongPressMs) {
        long_fired_ = true;
        pending_short_ = false;
        ev = ButtonEvent::LongPress;
    }

    /* A short press is only confirmed once the double-press window has expired. */
    if (pending_short_ && !stable_ && (now_ms - released_at_) > kDoubleGapMs) {
        pending_short_ = false;
        ev = ButtonEvent::ShortPress;
    }
    return ev;
}

/* ------------------------------------------------------------- Dashboard */

void Dashboard::begin()
{
    display_.begin();
    hal_.uartSetDir(BusDir::TxOnA);
    decoder_.reset();
    powered_ = true;
}

void Dashboard::transmit(const Frame& f)
{
    uint8_t buf[kOverhead + kMaxPayload];
    const size_t n = encode(f, buf, sizeof buf);
    if (n == 0) {
        return;
    }
    /* Half-duplex: drive the line, send, then hand it back to the receiver.
       The stock firmware does this by swapping PSELTXD/PSELRXD (see RE report §4). */
    hal_.uartSetDir(BusDir::TxOnA);
    hal_.uartWrite(buf, n);
    hal_.uartSetDir(BusDir::TxOnB);
}

void Dashboard::sendThrottle()
{
    /* On the VESC build the controller reads throttle/brake from its own ADC, so we send neutral
       and use the frame purely to keep the conversation alive. */
    transmit(makeThrottle(0x00, 0x00));
}

void Dashboard::onBusByte(uint8_t b)
{
    Frame f;
    if (decoder_.feed(b, f)) {
        onFrame(f);
    }
}

void Dashboard::onFrame(const Frame& f)
{
    if (f.cmd == CMD_TELEMETRY && f.len >= 6) {
        tele_.mode = static_cast<RideMode>(f.payload[0]);
        tele_.battery_pct = f.payload[1];
        tele_.light_on = (f.payload[2] != 0);
        tele_.speed_kmh_x10 = static_cast<uint16_t>(f.payload[4]) * 10u;
        tele_.error = f.payload[5];
        tele_.linked = true;
        last_rx_ms_ = hal_.millis();
    }
}

void Dashboard::applyButton(ButtonEvent ev)
{
    switch (ev) {
    case ButtonEvent::ShortPress:
        /* Stock behaviour: toggle the headlight. */
        tele_.light_on = !tele_.light_on;
        break;
    case ButtonEvent::DoublePress:
        /* Cycle Eco -> Drive -> Sport. No speed cap is applied here (R4.3); limits live in the VESC. */
        tele_.mode = (tele_.mode == RideMode::Eco)   ? RideMode::Drive
                   : (tele_.mode == RideMode::Drive) ? RideMode::Sport
                                                     : RideMode::Eco;
        break;
    case ButtonEvent::LongPress:
        powered_ = false;
        display_.displayOff();
        break;
    case ButtonEvent::None:
    default:
        break;
    }
}

void Dashboard::render(uint8_t seg[Tm1637::kGrids]) const
{
    for (uint8_t i = 0; i < Tm1637::kGrids; ++i) {
        seg[i] = 0x00;
    }

    if (tele_.error != 0) {
        /* Show "E" + the error code, like the stock fault display. */
        seg[0] = Tm1637::kFont[0x0E];                    /* 'E' */
        Tm1637::renderNumber(seg, tele_.error, 2, 1);
    } else if (!tele_.linked) {
        seg[0] = seg[1] = seg[2] = 0x40;                 /* "---" while the link is down */
    } else {
        /* Speed, one decimal: e.g. 25.3 -> "25.3" */
        const uint16_t whole = tele_.speed_kmh_x10 / 10u;
        const uint16_t frac = tele_.speed_kmh_x10 % 10u;
        Tm1637::renderNumber(seg, whole, 2, 0);
        seg[1] = static_cast<uint8_t>(seg[1] | Tm1637::kSegDot);   /* decimal point */
        seg[2] = Tm1637::kFont[frac];
    }

    /* Grid 4: ride mode. Grid 5: battery in tens of percent. */
    seg[4] = Tm1637::kFont[static_cast<uint8_t>(tele_.mode) & 0x0F];
    seg[5] = Tm1637::kFont[(tele_.battery_pct / 10u) & 0x0F];
}

void Dashboard::poll()
{
    const uint32_t now = hal_.millis();

    /* 1. Drain the bus. */
    uint8_t b;
    while (hal_.uartRead(b)) {
        onBusByte(b);
    }

    /* 2. Link supervision — the display must not show stale telemetry. */
    if (tele_.linked && (now - last_rx_ms_) > kLinkTimeoutMs) {
        tele_.linked = false;
        tele_.speed_kmh_x10 = 0;
    }

    /* 3. Button (hardware line is active-low). */
    const bool pressed = !hal_.pinRead(PIN_BUS_A /* placeholder: real pin set at board bring-up */);
    applyButton(button_.update(pressed, now));

    if (!powered_) {
        return;                       /* stay dark until the board is woken again */
    }

    /* 4. Keep the conversation going and refresh the display. */
    if ((now - last_poll_ms_) >= kPollIntervalMs) {
        last_poll_ms_ = now;
        sendThrottle();
        uint8_t seg[Tm1637::kGrids];
        render(seg);
        display_.update(seg, brightness_);
    }

    hal_.feedWatchdog();
}

} // namespace dash
