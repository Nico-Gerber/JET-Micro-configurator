# JET Micro HX: open firmware + configurator

Open replacement firmware for the JET Micro HX 3.0 footswitch (Arduino Nano / ATmega328P inside), plus a
browser-based configurator to choose what every button and press type does in each colour mode.

Default layout (works out of the box, red mode): **left = bank down, right = bank up, top = tuner**.
Pressing all three together cycles red → green (snapshots 1/2/3) → blue (preset down/up, tuner).
Holding all three for 3 s always returns to the first mode.

> **Unofficial.** Not affiliated with JET or Line 6. Flashing replaces the original firmware. Back it up first
> (see below). You do this at your own risk.

## Features
- Short, double and long presses on 3 buttons, plus combos (L+R, R+T, L+T, all three)
- 3 modes with their own LED colours (PWM RGB)
- Per slot, up to 2 actions: MIDI CC / CC toggle / PC, preset step, bank step (3 methods), snapshot, tuner,
  tap tempo, footswitch emulation, mode change, delay
- Double-press is off by default (it adds latency to single presses) and switchable per button and mode
- Config stored in EEPROM, edited over USB with the web configurator (Chrome or Edge, Web Serial)

## Hardware map (confirmed on a real unit)
| Part | Arduino pin |
|---|---|
| Left / Right / Top footswitch (to GND, `INPUT_PULLUP`) | D4 / D8 / D6 |
| LED red / green / blue (common cathode, PWM) | D10 / D11 / D9 |
| MIDI out | D1 (hardware TX), 31250 baud |

USB-serial chip is a CH340 (`1a86:7523`); the bootloader is the old 57600 baud one
(`arduino:avr:nano:cpu=atmega328old`). The USB port is only reachable with the backplate removed.

## 1. Back up the original firmware first
```bash
brew install avrdude arduino-cli          # or your platform's equivalent
ls /dev/cu.*                              # find the port, e.g. /dev/cu.usbserial-140
avrdude -c arduino -p atmega328p -P <PORT> -b 57600 -U flash:r:backups/jet_micro_flash_backup.hex:i
avrdude -c arduino -p atmega328p -P <PORT> -b 57600 -U eeprom:r:backups/jet_micro_eeprom_backup.hex:i
```
Restore any time with `-U flash:w:backups/jet_micro_flash_backup.hex:i`. The `backups/` folder is git-ignored on
purpose: those files are JET's firmware and are specific to your unit.

## 2. Flash this firmware
```bash
arduino-cli core install arduino:avr
cd firmware
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old jetmicro
arduino-cli upload  -p <PORT> --fqbn arduino:avr:nano:cpu=atmega328old jetmicro
```

## 3. Configure
```bash
cd web && python3 -m http.server 8000     # then open http://localhost:8000 in Chrome or Edge
```
Click **Connect**, pick the USB serial port, edit, press **Write to Micro**. Unplug the MIDI cable while
editing if you can (config traffic also leaves the MIDI port); plug it back in for **Test on Stomp**.

## Restore the original JET firmware
To undo this project and put JET's firmware back (for example to sell the pedal or to reset it):
```bash
python3 tools/restore_original.py --port <PORT>                 # downloads JET's official V2 HX 3.11 from their site
python3 tools/restore_original.py --port <PORT> --hex my_backup.hex --eeprom-file my_eeprom_backup.hex   # or your own backup
python3 tools/restore_original.py --port <PORT> --dry-run       # check the connection, write nothing
```
It needs `avrdude` and the same USB connection as flashing. Only the application area is written; the
bootloader is never touched, so you can flash this project's firmware again any time. This repo does not ship
JET's firmware: the script fetches it from JET's own site (and checks its SHA-256) when you run it.

## HX Stomp MIDI
Defaults target the HX Stomp on MIDI channel 1: CC69 snapshots, CC72 preset step, CC68 tuner, CC64 tap tempo.
These came from community docs, so confirm them against your Stomp's manual and firmware. The bank-step
action has three methods (CC72 ×3, single PC with local tracking, FS4/FS5 emulation); pick per action.

## Repo layout
- `firmware/jetmicro/`: the firmware (`jetmicro.ino`, `config.h`, `pins.h`)
- `firmware/pinfinder`, `ledscan`, `miditest`: small sketches used to map the pins on a new unit
- `web/`: the configurator (`index.html`, `app.js`, `config.js`)
- `tools/`: `restore_original.py` (put the original JET firmware back)
- `docs/`: `protocol.md` (USB config protocol)
- `backups/`: your own firmware backups (git-ignored)

## Known gaps
No repeat-while-held, 2 actions per slot, no in-browser flashing yet.

## Licence
MIT. Free to use, copy, modify and share. See `LICENSE`.
