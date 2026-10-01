#!/usr/bin/env python3
"""Build every published board and assemble the web flasher site.

    python scripts/build_site.py            # all boards in web/boards.json
    python scripts/build_site.py cyd sunton-3248s035r   # just these envs

Output goes to site/ (gitignored):

    site/index.html, site/boards.json          copied from web/
    site/firmware/<env>/buddy-<env>.bin         merged image, flash at 0x0
    site/firmware/<env>/manifest.json           ESP Web Tools manifest

The merged image holds bootloader + partition table + app, so one write at
offset 0 installs everything. LittleFS isn't included; the firmware formats
it on first boot.

Needs PlatformIO on PATH (`pio`). esptool comes from PlatformIO's own
tool-esptoolpy package, which the first build installs.
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
SITE = ROOT / "site"

# Where the 2nd-stage bootloader lives: 0x1000 on the classic ESP32, 0x0 on
# the S3, 0x2000 on the P4.
BOOTLOADER_OFFSET = {"ESP32": 0x1000, "ESP32-S3": 0x0, "ESP32-P4": 0x2000}
ESPTOOL_CHIP = {"ESP32": "esp32", "ESP32-S3": "esp32s3", "ESP32-P4": "esp32p4"}


def version() -> str:
    m = re.search(r'#define\s+BUDDY_VERSION\s+"([^"]+)"', (ROOT / "src/version.h").read_text())
    if not m:
        sys.exit("BUDDY_VERSION not found in src/version.h")
    return m.group(1)


def esptool() -> list:
    pio_home = Path.home() / ".platformio"
    script = pio_home / "packages/tool-esptoolpy/esptool.py"
    python = pio_home / "penv/bin/python"
    if not script.exists():
        sys.exit(f"esptool not found at {script} — build one env first")
    return [str(python if python.exists() else sys.executable), str(script)]


def build(env: str) -> None:
    print(f"== building {env}", flush=True)
    subprocess.run(["pio", "run", "-e", env], cwd=ROOT, check=True)


def merge(board: dict, ver: str) -> None:
    env, chip = board["env"], board["chip"]
    bdir = ROOT / ".pio/build" / env
    out_dir = SITE / "firmware" / env
    out_dir.mkdir(parents=True, exist_ok=True)
    image = out_dir / f"buddy-{env}.bin"
    parts = [
        hex(BOOTLOADER_OFFSET[chip]), str(bdir / "bootloader.bin"),
        "0x8000", str(bdir / "partitions.bin"),
    ]
    if board.get("otaLayout"):
        # Partition tables with OTA slots: write otadata selecting slot 0, so
        # a board that wasn't erased doesn't keep booting an older image left
        # in the other slot.
        boot_app0 = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
        parts += ["0xe000", str(boot_app0)]
    parts += ["0x10000", str(bdir / "firmware.bin")]
    subprocess.run(esptool() + ["--chip", ESPTOOL_CHIP[chip], "merge-bin", "-o", str(image)] + parts,
                   cwd=ROOT, check=True)
    manifest = {
        "name": f"Claude Desktop Buddy — {board['name']}",
        "version": ver,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": chip, "parts": [{"path": image.name, "offset": 0}]}],
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"   {image.relative_to(ROOT)} ({image.stat().st_size // 1024} KB)")


def main() -> None:
    catalog = json.loads((WEB / "boards.json").read_text())
    boards = catalog["boards"]
    only = set(sys.argv[1:])
    if only:
        boards = [b for b in boards if b["env"] in only]
        missing = only - {b["env"] for b in boards}
        if missing:
            sys.exit(f"not in web/boards.json: {', '.join(sorted(missing))}")

    ver = version()
    SITE.mkdir(exist_ok=True)
    for b in boards:
        build(b["env"])
        merge(b, ver)

    for f in WEB.iterdir():
        if f.is_file():
            shutil.copy2(f, SITE / f.name)
    # The page shows the version it was built with.
    catalog["version"] = ver
    (SITE / "boards.json").write_text(json.dumps(catalog, indent=2) + "\n")
    print(f"site ready in {SITE.relative_to(ROOT)}/ (v{ver})")


if __name__ == "__main__":
    main()
