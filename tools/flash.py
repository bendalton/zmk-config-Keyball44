#!/usr/bin/env python3
"""Hands-free flash: wait for one board to enter UF2 mode, prove which board
it is, then copy that board's firmware.

Usage:
    tools/flash.py {dongle|left|right} [path/to/firmware.uf2] [--timeout SECONDS]

Trigger the bootloader from the keyboard (hold Z or the rightmost thumb for
CONTROL, then):
    dongle: T + Y
    left:   Esc + left Shift
    right:  F18 + right Shift

When a NICENANO volume appears the script flashes only if it can prove the
board is the requested target:

  1. its USB serial matches the target's learned bootloader serial, or
  2. (dongle only) it matches the dongle's ZMK app-mode serial, or it is on
     the USB port the dongle was just running ZMK on and the dongle's ZMK
     device has gone, or
  3. (halves, first use only) the serial is new -- not any other known
     board's -- so it is learned as this target.

Anything else (a known board that isn't the target, two boards in UF2 mode)
is refused. IDs live in tools/board-ids.json.

Without a path it uses the newest matching .uf2 under
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
ID_FILE = os.path.join(HERE, "board-ids.json")
VOLUME = "/Volumes/NICENANO"
TARGETS = {
    "dongle": {"glob": "keyball44_dongle*.uf2", "keys": "hold Z + T+Y"},
    "left": {"glob": "keyball44_left*.uf2", "keys": "hold Z (or right thumb) + Esc+LShift"},
    "right": {"glob": "keyball44_right*.uf2", "keys": "hold Z (or right thumb) + F18+RShift"},
}
DEFAULT_IDS = {
    "dongle": {"app_serial": "A2051D2F672B2163", "boot_serial": None, "location": None},
    "left": {"boot_serial": None},
    "right": {"boot_serial": None},
}


def load_ids():
    ids = json.loads(json.dumps(DEFAULT_IDS))
    if os.path.exists(ID_FILE):
        with open(ID_FILE) as f:
            for k, v in json.load(f).items():
                ids.setdefault(k, {}).update(v)
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
    return next((d for d in devs if d["serial"] == ids["dongle"]["app_serial"]), None)


def bootloaders(devs):
    # Adafruit nRF52 UF2 bootloader (nice!nano) enumerates with Adafruit's VID.
    return [d for d in devs if d["vendor"] == "0x239a" or "nice" in d["name"].lower()]


def newest_uf2(target):
    pattern = os.path.expanduser("~/Downloads/keyball44-firmware-*/" + TARGETS[target]["glob"])
    files = glob.glob(pattern)
    return max(files, key=os.path.getmtime) if files else None


def known_serials(ids, exclude):
    s = set()
    for name, entry in ids.items():
        if name == exclude:
            continue
        for key in ("app_serial", "boot_serial"):
            if entry.get(key):
                s.add(entry[key])
    return s


def identify(target, boot, devs, ids):
    me = ids[target]
    if me.get("boot_serial") and boot["serial"] == me["boot_serial"]:
        return "bootloader serial matches"
    if boot["serial"] in known_serials(ids, target):
        return None  # it's some other known board
    if target == "dongle":
        if boot["serial"] and boot["serial"] == me["app_serial"]:
            return "serial matches the dongle's ZMK serial"
        if (
            not me.get("boot_serial")
            and me.get("location")
            and boot["location"] == me["location"]
            and zmk_dongle(devs, ids) is None
        ):
            return "same USB port the dongle was just on (first-run learn)"
        return None
    if not me.get("boot_serial") and boot["serial"]:
        return "new serial, not any other known board (first-run learn)"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target", choices=sorted(TARGETS))
    ap.add_argument("uf2", nargs="?")
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()
    target = args.target

    uf2 = args.uf2 or newest_uf2(target)
    if not uf2 or not os.path.exists(uf2):
        sys.exit(f"no {target} .uf2 found")
    if f"keyball44_{target}" not in os.path.basename(uf2):
        sys.exit(f"refusing: {uf2} is not a {target} build")

    ids = load_ids()
    if os.path.exists(VOLUME):
        sys.exit("a NICENANO volume is already mounted; flash/unplug it first so it can't be confused")

    if target == "dongle":
        dongle = zmk_dongle(usb_devices(), ids)
        if dongle:
            ids["dongle"]["location"] = dongle["location"]
            print(f"dongle running ZMK on port {dongle['location']}")
        else:
            print("dongle not currently seen running ZMK (fine if it's already rebooting)")

    print(f"firmware: {uf2}")
    print(f"waiting -- press {TARGETS[target]['keys']} now", flush=True)

    deadline = time.time() + args.timeout
    while time.time() < deadline:
        if os.path.exists(VOLUME):
            break
        time.sleep(0.5)
    else:
        sys.exit("timed out waiting for NICENANO")

    time.sleep(1.0)  # let the USB tree settle
    devs = usb_devices()
    boots = bootloaders(devs)
    if len(boots) != 1:
        sys.exit(f"refusing: expected exactly one board in UF2 mode, saw {boots}")
    boot = boots[0]
    print(f"UF2 board: serial={boot['serial']} port={boot['location']}")

    reason = identify(target, boot, devs, ids)
    if not reason:
        sys.exit(f"refusing: can't prove this board is the {target} (known ids: {ids})")
    print(f"identified as {target}: {reason}")

    try:
        shutil.copyfile(uf2, os.path.join(VOLUME, "CURRENT.UF2"))
    except OSError as e:
        # The bootloader reboots as soon as the last block lands, which can
        # yank the volume out from under the final close.
        print(f"(copy ended with {e!r}; usually means the board rebooted -- checking)")

    ids[target]["boot_serial"] = boot["serial"]
    if target == "dongle":
        ids["dongle"]["location"] = boot["location"]
    save_ids(ids)

    end = time.time() + 30
    while time.time() < end:
        gone = not os.path.exists(VOLUME)
        if gone and (target != "dongle" or zmk_dongle(usb_devices(), ids)):
            print(f"done: {target} flashed and rebooted")
            return
        time.sleep(1)
    sys.exit(f"copied, but the {target} didn't come back within 30s -- check it")


if __name__ == "__main__":
    main()
