#!/usr/bin/env python3
"""Hardware test for the dashboard's 5000 ms IWDG watchdog (Req 16).

The watchdog firmware feeds the IWDG only while every *required* subsystem is
fresh; if one goes missing it stops feeding and the MCU resets within ~5 s. On a
**DASH_DEBUG** build the firmware prints `[BOOT] G30 dash wdt=5000ms` on the
USART2 (VESC/cable) line at every reset, so this tool can observe reset cadence
over a USB-TTL adapter.

Wire-up (no soldering): CP2102 @ 3.3 V on the cable DATA line (half-duplex; tie
TX→bus through ~1 kΩ, RX direct) or the ESC↔BLE 7-pin tap — see
docs/DASHBOARD_NO_SOLDER_FLASH.md. Build the test image with:
    python tools/build_dashboard.py --debug

Usage
-----
  # Monitor: print every reset and the interval between resets.
  python Target/dashboard_watchdog_test.py --port COM5

  # Verify: induce a missing dependency (e.g. unplug the VESC while riding /
  # hold the board in a failed-init state), then assert a reset lands in the
  # 5 s ± LSI-tolerance window.
  python Target/dashboard_watchdog_test.py --port COM5 --verify
"""
from __future__ import annotations
import argparse
import sys
import time

MARKER = b"[BOOT]"
# 5000 ms nominal, widened for LSI spread (30-60 kHz) + host scheduling jitter.
RESET_MIN_MS = 3500
RESET_MAX_MS = 7000


def _open_serial(port: str, baud: int):
    try:
        import serial  # pyserial
    except ImportError:
        print("ERROR: pyserial not installed (pip install pyserial).", file=sys.stderr)
        sys.exit(2)
    try:
        return serial.Serial(port, baud, timeout=0.2)
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: cannot open {port}: {exc}", file=sys.stderr)
        sys.exit(2)


def _boot_timestamps(ser, duration_s: float) -> list[float]:
    """Collect monotonic timestamps of every [BOOT] marker seen within duration."""
    stamps: list[float] = []
    buf = bytearray()
    t_end = time.monotonic() + duration_s
    while time.monotonic() < t_end:
        chunk = ser.read(64)
        if chunk:
            buf += chunk
            while MARKER in buf:
                idx = buf.index(MARKER)
                stamps.append(time.monotonic())
                buf = buf[idx + len(MARKER):]
                print(f"  [BOOT] @ t={stamps[-1]:.2f}s")
    return stamps


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port (e.g. COM5, /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--duration", type=float, default=20.0,
                    help="seconds to observe (default 20)")
    ap.add_argument("--verify", action="store_true",
                    help="assert a reset interval falls in the 5 s watchdog window")
    args = ap.parse_args(argv)

    ser = _open_serial(args.port, args.baud)
    print(f"Listening on {args.port} @ {args.baud} for '[BOOT]' (DASH_DEBUG build)...")
    if args.verify:
        print("Induce a *missing required subsystem* now (e.g. unplug the VESC while in")
        print("RUN, or hold a failed init) — the board must reset within ~5 s.\n")

    stamps = _boot_timestamps(ser, args.duration)
    ser.close()

    if not stamps:
        print("\nNo [BOOT] seen. Is this a --debug build? Is the wiring/port correct?")
        return 1 if args.verify else 0

    deltas_ms = [(b - a) * 1000 for a, b in zip(stamps, stamps[1:])]
    print(f"\nResets seen: {len(stamps)}")
    if deltas_ms:
        print("Reset intervals (ms): " + ", ".join(f"{d:.0f}" for d in deltas_ms))

    if not args.verify:
        return 0

    in_window = [d for d in deltas_ms if RESET_MIN_MS <= d <= RESET_MAX_MS]
    if in_window:
        print(f"PASS: watchdog reset within window "
              f"[{RESET_MIN_MS}-{RESET_MAX_MS} ms]: {in_window[0]:.0f} ms")
        return 0
    print(f"FAIL: no reset interval in [{RESET_MIN_MS}-{RESET_MAX_MS} ms]. "
          "Watchdog may not have fired (still healthy?) or timeout is off.",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
