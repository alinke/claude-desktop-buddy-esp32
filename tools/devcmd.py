"""Send one JSON line to the device over USB serial and print what comes back.

    python tools/devcmd.py --port /dev/cu.wchusbserial210 '{"cmd":"opensettings"}'
    python tools/devcmd.py --port ... '{"cmd":"tap","x":160,"y":231}' --wait 3

Opens the port with DTR/RTS held low so CH340 boards don't reset.
"""
import argparse
import time

import serial


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("json")
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--wait", type=float, default=1.0, help="seconds to read replies")
    a = ap.parse_args()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = a.port, a.baud, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    s.write(a.json.encode() + b"\n")
    end = time.time() + a.wait
    out = b""
    while time.time() < end:
        out += s.read(4096)
    print(out.decode(errors="replace"), end="")
    s.close()


if __name__ == "__main__":
    main()
