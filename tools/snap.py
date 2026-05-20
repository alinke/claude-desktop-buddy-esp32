#!/usr/bin/env python3
"""
snap.py — pull a pixel-perfect screenshot from the CYD over USB serial.

Triggers the device's `cmd:screenshot` handler, reads the base64-encoded
sprite rows it sends back, and writes a PNG. Run while the device is
connected via USB and showing whatever you want to capture.

    python tools/snap.py                          # screenshot.png in cwd
    python tools/snap.py home.png                 # named output
    python tools/snap.py home.png --port COM7     # different port

Requires: pyserial, Pillow.
    pip install pyserial pillow
"""
import argparse
import base64
import sys
import time
from pathlib import Path

import serial
from PIL import Image


def grab(port: str, baud: int, out_path: Path, timeout: float = 15.0) -> None:
    s = serial.Serial(port, baud, timeout=2)
    try:
        # Settle the line, then ask.
        s.reset_input_buffer()
        s.write(b'{"cmd":"screenshot"}\n')
        s.flush()
        w = h = bpp = 0
        rows: list[bytes] = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            raw = s.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip()
            if not line:
                continue
            if line.startswith("SCR-BEGIN"):
                parts = line.split()
                if len(parts) >= 4:
                    w, h, bpp = int(parts[1]), int(parts[2]), int(parts[3])
                rows = []
            elif line.startswith("SCR ") and w:
                try:
                    rows.append(base64.b64decode(line[4:]))
                except Exception:
                    pass
            elif line == "SCR-END":
                break
            elif line.startswith("SCR-ERR"):
                raise RuntimeError(line)
            # any other line (status acks, etc) — ignore
        else:
            raise TimeoutError("no SCR-END inside timeout window")

        if not rows or w == 0 or h == 0:
            raise RuntimeError("no sprite data captured")
        if len(rows) != h:
            raise RuntimeError(f"row count mismatch: got {len(rows)}, expected {h}")
        if bpp != 8:
            raise RuntimeError(f"unsupported color depth {bpp} (only 8 bpp implemented)")

        img = Image.new("RGB", (w, h))
        px = img.load()
        for y, row in enumerate(rows):
            if len(row) < w:
                row = row + b"\x00" * (w - len(row))
            for x in range(w):
                b332 = row[x]
                r = ((b332 >> 5) & 7) * 36
                g = ((b332 >> 2) & 7) * 36
                b = (b332 & 3) * 85
                px[x, y] = (r, g, b)
        img.save(out_path)
        print(f"saved {out_path} ({w}x{h}, {bpp} bpp, {sum(map(len, rows))} bytes)")
    finally:
        s.close()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out", nargs="?", default="screenshot.png",
                    help="output PNG path (default: screenshot.png)")
    ap.add_argument("--port", default="COM10", help="serial port (default: COM10)")
    ap.add_argument("--baud", type=int, default=115200, help="baud rate")
    ap.add_argument("--timeout", type=float, default=15.0,
                    help="seconds to wait for SCR-END")
    args = ap.parse_args()
    try:
        grab(args.port, args.baud, Path(args.out), args.timeout)
    except (RuntimeError, TimeoutError, serial.SerialException) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
