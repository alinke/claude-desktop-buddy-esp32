#!/usr/bin/env python3
"""
sim.py — inject synthetic touch events into the CYD over USB serial.

The device's HAL accepts {"cmd":"tap", ...} and {"cmd":"swipe", ...} on
its serial input and routes the injected coordinates through the same
zone-classification / button-edge / gesture pipeline as a real finger.
Useful for scripting screen-by-screen captures with snap.py.

    python tools/sim.py tap 16 110                # tap (x=16, y=110), 80 ms
    python tools/sim.py tap 16 110 --hold 700     # long-press (opens menu)
    python tools/sim.py swipe 120 280 120 200     # swipe up in the HUD
    python tools/sim.py menu                      # named shortcut: long-press left
    python tools/sim.py bubble pet                # tap the heart bubble
    python tools/sim.py page                      # tap right side (advance Info)

Requires: pyserial.   pip install pyserial
"""
import argparse
import json
import sys
import time

import serial

# Named shortcuts — common UI hits so you can script flows without
# remembering pixel coordinates.
SHORTCUTS = {
    "menu":    {"cmd": "tap",   "x":  20, "y": 200, "duration": 700},  # long-press left
    "page":    {"cmd": "tap",   "x": 200, "y": 200, "duration":  80},  # tap right
    "next":    {"cmd": "tap",   "x":  40, "y": 200, "duration":  80},  # tap left (next screen)
    "power":   {"cmd": "tap",   "x": 220, "y":  20, "duration":  80},  # top-right corner
    "pet":     {"cmd": "tap",   "x":  18, "y":  33, "duration":  80},  # heart bubble
    "buddies": {"cmd": "tap",   "x":  18, "y":  59, "duration":  80},  # face bubble
    "set":     {"cmd": "tap",   "x":  18, "y":  85, "duration":  80},  # gear bubble
    "info":    {"cmd": "tap",   "x":  18, "y": 111, "duration":  80},  # i bubble
    "wake":    {"cmd": "tap",   "x": 120, "y": 160, "duration":  80},  # center tap
}


def send(port: str, baud: int, payload: dict, settle: float = 0.5) -> None:
    s = serial.Serial(port, baud, timeout=1)
    try:
        s.reset_input_buffer()
        line = (json.dumps(payload) + "\n").encode("utf-8")
        s.write(line)
        s.flush()
        # Wait for ack or for the synthetic touch to land before exiting,
        # so scripts that chain sim.py + snap.py see the post-tap state.
        dur = payload.get("duration", 80)
        deadline = time.time() + max(settle, dur / 1000.0 + 0.3)
        while time.time() < deadline:
            raw = s.readline()
            if not raw:
                continue
            txt = raw.decode("utf-8", errors="replace").strip()
            if not txt:
                continue
            if txt.startswith('{"ack":'):
                # acked — keep waiting just long enough for the press to release
                pass
        print(f"sent {payload}")
    finally:
        s.close()


def build_payload(args: argparse.Namespace) -> dict:
    if args.action in SHORTCUTS:
        return dict(SHORTCUTS[args.action])
    if args.action == "tap":
        if args.x is None or args.y is None:
            raise ValueError("tap requires x and y")
        return {"cmd": "tap", "x": args.x, "y": args.y,
                "duration": args.hold}
    if args.action == "swipe":
        if None in (args.x, args.y, args.x1, args.y1):
            raise ValueError("swipe requires x y x1 y1")
        return {"cmd": "swipe", "x0": args.x, "y0": args.y,
                "x1": args.x1, "y1": args.y1, "duration": args.hold}
    raise ValueError(f"unknown action {args.action!r}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("action",
                    help=f"tap | swipe | {' | '.join(SHORTCUTS.keys())}")
    ap.add_argument("x",  nargs="?", type=int)
    ap.add_argument("y",  nargs="?", type=int)
    ap.add_argument("x1", nargs="?", type=int)
    ap.add_argument("y1", nargs="?", type=int)
    ap.add_argument("--hold", type=int, default=80,
                    help="touch duration in ms (default 80, use ~700 for long-press)")
    ap.add_argument("--port", default="COM10", help="serial port (default COM10)")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()
    try:
        payload = build_payload(args)
    except ValueError as e:
        ap.error(str(e))
    try:
        send(args.port, args.baud, payload)
    except serial.SerialException as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
