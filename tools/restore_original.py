#!/usr/bin/env python3
"""Put the original JET Micro HX firmware back on the pedal (undo this project's firmware).

Usage:
  python3 tools/restore_original.py --port /dev/cu.usbserial-140            # downloads JET's official hex
  python3 tools/restore_original.py --port COM3 --hex JET_Micro_V2_HX_311.hex  # use a file you already have
  python3 tools/restore_original.py --port ... --hex my_backup.hex --eeprom-file my_eeprom.hex

Safety: only the application area (below 0x7800) is written. The bootloader is never touched, even if the
hex file contains it, so you can always flash again afterwards. Needs avrdude on your PATH.
This repo does not ship JET's firmware; the script downloads it from JET's own site when you run it.
"""
import argparse, hashlib, os, shutil, subprocess, sys, tempfile, urllib.request

OFFICIAL_URL = "https://cdn.shopify.com/s/files/1/0219/3041/0084/files/JET_Micro_V2_HX_311.hex?v=1623161161"
OFFICIAL_SHA256 = "a2c284b002a4938057c1bcdc35fe41a983447921e17ab7d02df8baeb166b2c30"
BOOT_START = 0x7800          # old Arduino Nano bootloader; never write at or above this
EEPROM_SIZE = 1024


def parse_hex(text):
    mem, base = {}, 0
    for n, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise ValueError(f"line {n}: not an Intel HEX record")
        raw = bytes.fromhex(line[1:])
        if sum(raw) & 0xFF:
            raise ValueError(f"line {n}: bad checksum")
        count, addr, rtype, data = raw[0], (raw[1] << 8) | raw[2], raw[3], raw[4:-1]
        if rtype == 0x00:
            for i, b in enumerate(data):
                mem[base + addr + i] = b
        elif rtype == 0x04:
            base = int.from_bytes(data, "big") << 16
        elif rtype == 0x01:
            break
    return mem


def to_hex(mem):
    out, addrs, i = [], sorted(mem), 0
    while i < len(addrs):
        j = i
        while j + 1 < len(addrs) and addrs[j + 1] == addrs[j] + 1 and j + 1 - i < 16:
            j += 1
        chunk = bytes(mem[a] for a in addrs[i:j + 1])
        rec = bytes([len(chunk), addrs[i] >> 8, addrs[i] & 0xFF, 0]) + chunk
        out.append(":" + (rec + bytes([(-sum(rec)) & 0xFF])).hex().upper())
        i = j + 1
    out.append(":00000001FF")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Restore the original JET Micro HX firmware.")
    ap.add_argument("--port", required=True, help="serial port, e.g. /dev/cu.usbserial-140, /dev/ttyUSB0 or COM3")
    ap.add_argument("--hex", help="JET firmware .hex to flash (default: download JET's official V2 HX 3.11)")
    ap.add_argument("--eeprom-file", help="EEPROM backup (.hex) to restore (default: erase the area this project uses)")
    ap.add_argument("--baud", default="57600", help="57600 for the old Nano bootloader (default); try 115200 if you get 'not in sync'")
    ap.add_argument("--dry-run", action="store_true", help="talk to the board and verify, but write nothing")
    ap.add_argument("-y", "--yes", action="store_true", help="skip the confirmation prompt")
    a = ap.parse_args()

    avrdude = shutil.which("avrdude")
    if not avrdude:
        sys.exit("avrdude not found. Install it (macOS: brew install avrdude, Debian/Ubuntu: apt install avrdude).")

    if a.hex:
        data = open(a.hex, "rb").read()
        print(f"Using {a.hex}")
    else:
        print("Downloading JET's official firmware from their site...")
        data = urllib.request.urlopen(OFFICIAL_URL, timeout=30).read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != OFFICIAL_SHA256:
        print(f"Note: SHA-256 {digest[:16]}... is not the known official V2 HX 3.11 file. "
              "That is fine for your own backup; double-check it is what you expect.")

    mem = parse_hex(data.decode("ascii", "replace"))
    app = {k: v for k, v in mem.items() if k < BOOT_START}
    skipped = len(mem) - len(app)
    if not app or min(app) != 0:
        sys.exit("This hex has no application at address 0; refusing to flash it.")
    print(f"Firmware: {len(app)} bytes in the application area"
          + (f" ({skipped} bootloader bytes skipped; the bootloader is never rewritten)" if skipped else ""))

    if not a.yes and not a.dry_run:
        print("\nThis replaces the firmware on the pedal with the ORIGINAL JET firmware.")
        if input("Type 'yes' to continue: ").strip().lower() != "yes":
            sys.exit("Cancelled.")

    with tempfile.TemporaryDirectory() as tmp:
        flash = os.path.join(tmp, "flash.hex"); open(flash, "w").write(to_hex(app))
        if a.eeprom_file:
            eeprom = a.eeprom_file
        else:
            eeprom = os.path.join(tmp, "eeprom.hex"); open(eeprom, "w").write(to_hex({i: 0xFF for i in range(EEPROM_SIZE)}))
        cmd = [avrdude, "-c", "arduino", "-p", "atmega328p", "-P", a.port, "-b", a.baud]
        if a.dry_run:
            cmd.append("-n")
        cmd += ["-U", f"flash:w:{flash}:i", "-U", f"eeprom:w:{eeprom}:i"]
        print("\nRunning:", " ".join(cmd), "\n")
        rc = subprocess.call(cmd)
    if rc:
        sys.exit(f"\navrdude failed (exit {rc}). If you saw 'not in sync', try --baud 115200 and check the port and cable.")
    print("\nDone. The pedal now runs the original JET firmware." if not a.dry_run else "\nDry run OK: nothing was written.")


if __name__ == "__main__":
    main()
