#!/usr/bin/env python3
"""
capture_readme.py — drive the CYD through every notable screen and save
a PNG for each into docs/.

Two flavours of capture:

  Automated (no preconditions):
    splash           home              pet_stats
    buddies          menu              settings
    info_about       info_controls     info_claude
    info_response    info_sessions     info_device
    info_bluetooth   info_credits
    approval         multichoice       ask_picker
    reset

  Manual (--manual flag — pauses, you set up the state, hit Enter):
    touch_keyboard   — open via Settings -> wifi setup
    calibration      — open via Settings -> calibrate
    clock_face       — leave idle on USB ~30 s with RTC synced

Usage:
    python tools/capture_readme.py
    python tools/capture_readme.py --port COM10 --only splash home
    python tools/capture_readme.py --skip menu approval
    python tools/capture_readme.py --manual    # also include the manual shots
"""

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT  = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
DOCS  = ROOT / "docs"
PY    = sys.executable

# ── Subprocess + serial helpers ────────────────────────────────────────
def sh(*args, **kw):
    return subprocess.run([PY, *args], check=True, **kw)

def snap(name: str, port: str):
    out = DOCS / f"{name}.png"
    out.parent.mkdir(exist_ok=True)
    print(f"  -> docs/{out.name}")
    sh(str(TOOLS / "snap.py"), str(out), "--port", port)

def send_json(payload: dict, port: str):
    import serial
    s = serial.Serial(port, 115200, timeout=1)
    try:
        s.reset_input_buffer()
        s.write((json.dumps(payload) + "\n").encode("utf-8"))
        s.flush()
        time.sleep(0.05)
    finally:
        s.close()

def settle(secs: float = 0.4):
    time.sleep(secs)

def close_all(port: str):
    send_json({"cmd": "closeall"}, port)
    settle(0.3)

# ── Automated captures (state injection, no tap dance) ─────────────────
def cap_splash(port: str):
    close_all(port)
    send_json({"cmd": "splash"}, port)
    settle(0.6)
    snap("splash", port)

def cap_home(port: str):
    close_all(port)
    settle(0.4)
    snap("home", port)

def cap_pet_stats(port: str):
    close_all(port)
    # No direct "open pet stats" cmd; tap the heart bubble — short tap is
    # reliable enough.
    sh(str(TOOLS / "sim.py"), "pet", "--port", port)
    settle(0.6)
    snap("pet_stats", port)
    close_all(port)

def cap_buddies(port: str):
    close_all(port)
    send_json({"cmd": "openbuddies"}, port)
    settle(0.7)
    snap("buddies", port)
    close_all(port)

def cap_menu(port: str):
    close_all(port)
    send_json({"cmd": "openmenu"}, port)
    settle(0.4)
    snap("menu", port)
    close_all(port)

def cap_settings(port: str):
    close_all(port)
    send_json({"cmd": "opensettings"}, port)
    settle(0.4)
    snap("settings", port)
    close_all(port)

def cap_reset(port: str):
    close_all(port)
    send_json({"cmd": "openreset"}, port)
    settle(0.4)
    snap("reset", port)
    close_all(port)

def cap_info_pages(port: str):
    close_all(port)
    # cmd:openinfo jumps DISP_INFO straight to the requested page index,
    # bypassing the "i" bubble tap (whose hit-test isn't reliable enough
    # under serial injection — captured the home screen instead) and the
    # tap-right page-advance dance (likely to drift if any frame misses).
    #
    # Page index <-> filename mapping matches main.cpp's drawInfo() ordering:
    #   0 ABOUT   1 CONTROLS   2 CLAUDE   3 RESPONSE
    #   4 SESSIONS   5 DEVICE   6 BLUETOOTH   7 CREDITS
    pages = [
        (0, "about"),     (1, "controls"), (2, "claude"),    (3, "response"),
        (4, "sessions"),  (5, "device"),   (6, "bluetooth"), (7, "credits"),
    ]
    for idx, name in pages:
        send_json({"cmd": "openinfo", "page": idx}, port)
        settle(0.5)
        snap(f"info_{name}", port)
    close_all(port)

def cap_approval(port: str):
    close_all(port)
    # README screenshots are demo theatre — putting actual git/rm commands
    # in the modal makes the device look scary, and the "approve / deny"
    # mechanic is more memorable when the stakes are obviously fake.
    # So: Claude is mid-prep on a pizza and needs sign-off to swap the
    # toppings, and the multichoice (below) is the natural follow-up.
    send_json({
        "prompt": {
            "id":   "test-bin-1",
            "tool": "Bash",
            "hint": "chef --swap pepperoni \"tiny pancakes\"",
        }
    }, port)
    settle(0.6)
    snap("approval", port)
    send_json({"cmd": "clearprompt"}, port)
    settle(0.4)

def cap_multichoice(port: str):
    close_all(port)
    # Continues the pizza/pancake bit from cap_approval — the user just
    # approved the swap, now Claude needs to know how to serve the result.
    send_json({
        "prompt": {
            "id":   "test-mcq-1",
            "tool": "Bash",
            "hint": "tiny-pancake pizza - how to serve?",
            "choices": [
                {"id": "a", "label": "syrup AND butter"},
                {"id": "b", "label": "syrup OR butter (xor?)"},
                {"id": "c", "label": "raw -- already cooked!"},
            ],
        }
    }, port)
    settle(0.6)
    snap("multichoice", port)
    send_json({"cmd": "clearprompt"}, port)
    settle(0.4)

def cap_ask_picker(port: str):
    close_all(port)
    send_json({"cmd": "openask"}, port)
    settle(0.6)
    snap("ask_picker", port)
    close_all(port)

# ── Manual captures (you set up, hit Enter) ────────────────────────────
def cap_manual(name: str, instruction: str, port: str):
    print(f"  ! {instruction}")
    input(f"    Press Enter when '{name}' is on screen ... ")
    snap(name, port)

def cap_touch_keyboard(port: str):
    cap_manual(
        "touch_keyboard",
        "On the device: Settings -> wifi setup (this opens the keyboard).",
        port,
    )

def cap_calibration(port: str):
    cap_manual(
        "calibration",
        "On the device: Settings -> calibrate (the first target appears).",
        port,
    )

def cap_clock_face(port: str):
    cap_manual(
        "clock_face",
        "Wait ~30 s without touching the device — clock takes over when "
        "idle on USB with RTC synced from the desktop bridge.",
        port,
    )

CAPTURES = {
    # automated
    "splash":         cap_splash,
    "home":           cap_home,
    "pet_stats":      cap_pet_stats,
    "buddies":        cap_buddies,
    "menu":           cap_menu,
    "settings":       cap_settings,
    "reset":          cap_reset,
    "info":           cap_info_pages,
    "approval":       cap_approval,
    "multichoice":    cap_multichoice,
    "ask_picker":     cap_ask_picker,
    # manual
    "touch_keyboard": cap_touch_keyboard,
    "calibration":    cap_calibration,
    "clock_face":     cap_clock_face,
}

AUTO_ORDER = [
    "splash", "home", "pet_stats", "buddies", "menu", "settings",
    "reset", "info", "approval", "multichoice", "ask_picker",
]
MANUAL_ORDER = ["touch_keyboard", "calibration", "clock_face"]

# ── Main ───────────────────────────────────────────────────────────────
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port",   default="COM10")
    ap.add_argument("--only",   nargs="+", choices=list(CAPTURES.keys()),
                    help="capture only these names")
    ap.add_argument("--skip",   nargs="+", choices=list(CAPTURES.keys()),
                    default=[], help="skip these names")
    ap.add_argument("--manual", action="store_true",
                    help="also include touch_keyboard / calibration / clock_face "
                         "(prompts you to set up each on-device, then hit Enter)")
    args = ap.parse_args()

    DOCS.mkdir(exist_ok=True)
    if args.only:
        names = args.only
    else:
        names = list(AUTO_ORDER) + (list(MANUAL_ORDER) if args.manual else [])
    names = [n for n in names if n not in args.skip]

    print(f"capture_readme: {len(names)} group(s) -> docs/")
    for name in names:
        print(f"[{name}]")
        try:
            CAPTURES[name](args.port)
        except subprocess.CalledProcessError as e:
            print(f"  ! sub-tool failed: {e}", file=sys.stderr)
            return 1
        except KeyboardInterrupt:
            print("\nabort.", file=sys.stderr)
            return 130
        except Exception as e:
            print(f"  ! {type(e).__name__}: {e}", file=sys.stderr)
            return 1

    print("[cleanup] closing all overlays")
    close_all(args.port)
    print("done.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
