#!/usr/bin/env python3
"""Hands-free dongle flash: wait for the dongle to enter UF2 mode, then copy.

Usage:
    tools/flash_dongle.py [path/to/keyball44_dongle*.uf2] [--timeout SECONDS]

Run it, then press hold-Z + T+Y on the keyboard (&dongle_boot). When a
NICENANO volume appears, the script only flashes if it can prove that board is
the dongle:

  1. its USB serial matches the dongle's (learned bootloader serial, or the
     ZMK app-mode serial -- both come from the nRF52's factory device ID), or
  2. on first use, before the bootloader serial is known: it is on the SAME
     USB port (location ID) where the dongle was running ZMK a moment ago,
     and the dongle's ZMK device has disappeared.

Anything else (a half plugged in, two boards in UF2 mode) is refused. On
success the bootloader serial is saved to tools/dongle-id.json so later runs
match on serial alone.

Without a path it uses the newest keyball44_dongle*.uf2 under
~/Downloads/keyball44-firmware-*/.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ID_FILE = os.path.join(HERE, "dongle-id.json")
VOLUME = "/Volumes/NICENANO"
DEFAULT_IDS = {"app_serial": "A2051D2F672B2163", "boot_serial": None, "location": None}


def load_ids():
    ids = dict(DEFAULT_IDS)
    if os.path.exists(ID_FILE):
        with open(ID_FILE) as f:
            ids.update(json.load(f))
    return ids


def save_ids(ids):
    with open(ID_FILE, "w") as f:
        json.dump(ids, f, indent=2)
        f.write("\n")


def usb_devices():
    out = subprocess.run(
        ["system_profiler", "SPUSBHostDataType", "-json"], capture_output=True, text=True
    ).stdout
    found = []

    def walk(node):
        if isinstance(node, list):
            for n in node:
                walk(n)
        elif isinstance(node, dict):
            if "USBDeviceKeyVendorID" in node:
                found.append(
                    {
                        "name": node.get("_name", ""),
                        "vendor": node.get("USBDeviceKeyVendorID", "").lower(),
                        "serial": node.get("USBDeviceKeySerialNumber", ""),
                        "location": node.get("USBKeyLocationID", ""),
                    }
                )
            for v in node.values():
                if isinstance(v, (list, dict)):
                    walk(v)

    walk(json.loads(out or "{}"))
    return found


def zmk_dongle(devs, ids):
    return next((d for d in devs if d["serial"] == ids["app_serial"]), None)


def bootloaders(devs):
    # Adafruit nRF52 UF2 bootloader (nice!nano) enumerates with Adafruit's VID.
    return [d for d in devs if d["vendor"] == "0x239a" or "nice" in d["name"].lower()]


def newest_uf2():
    pattern = os.path.expanduser("~/Downloads/keyball44-firmware-*/keyball44_dongle*.uf2")
    files = glob.glob(pattern)
    return max(files, key=os.path.getmtime) if files else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("uf2", nargs="?")
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()

    uf2 = args.uf2 or newest_uf2()
    if not uf2 or not os.path.exists(uf2):
        sys.exit("no dongle .uf2 found")
    if "dongle" not in os.path.basename(uf2):
        sys.exit(f"refusing: {uf2} is not a dongle build")

    ids = load_ids()
    if os.path.exists(VOLUME):
        sys.exit("a NICENANO volume is already mounted; unplug/flash it first so it can't be confused with the dongle")

    dongle = zmk_dongle(usb_devices(), ids)
    if dongle:
        ids["location"] = dongle["location"]
        print(f"dongle running ZMK on port {dongle['location']}")
    else:
        print("dongle not currently seen running ZMK (fine if it's already rebooting)")

    print(f"firmware: {uf2}")
    print("waiting -- press hold-Z + T+Y now", flush=True)

    deadline = time.time() + args.timeout
    while time.time() < deadline:
        if os.path.exists(VOLUME):
            break
        time.sleep(0.5)
    else:
        sys.exit("timed out waiting for NICENANO")

    # Give the USB tree a moment to settle, then identify the board.
    time.sleep(1.0)
    devs = usb_devices()
    boots = bootloaders(devs)
    if len(boots) != 1:
        sys.exit(f"refusing: expected exactly one board in UF2 mode, saw {boots}")
    boot = boots[0]
    print(f"UF2 board: serial={boot['serial']} port={boot['location']}")

    reason = None
    if ids.get("boot_serial") and boot["serial"] == ids["boot_serial"]:
        reason = "bootloader serial matches"
    elif boot["serial"] and boot["serial"] == ids["app_serial"]:
        reason = "serial matches the dongle's ZMK serial"
    elif (
        not ids.get("boot_serial")
        and ids.get("location")
        and boot["location"] == ids["location"]
        and zmk_dongle(devs, ids) is None
    ):
        reason = "same USB port the dongle was just on (first-run learn)"

    if not reason:
        sys.exit(f"refusing: can't prove this board is the dongle (known ids: {ids})")
    print(f"identified as dongle: {reason}")

    try:
        shutil.copyfile(uf2, os.path.join(VOLUME, "CURRENT.UF2"))
    except OSError as e:
        # The bootloader reboots as soon as the last block lands, which can
        # yank the volume out from under the final close.
        print(f"(copy ended with {e!r}; usually means the board rebooted -- checking)")

    ids["boot_serial"] = boot["serial"]
    ids["location"] = boot["location"]
    save_ids(ids)

    end = time.time() + 30
    while time.time() < end:
        if not os.path.exists(VOLUME) and zmk_dongle(usb_devices(), ids):
            print("done: dongle is back up running ZMK")
            return
        time.sleep(1)
    sys.exit("copied, but the dongle didn't come back as ZMK within 30s -- check it")


if __name__ == "__main__":
    main()
