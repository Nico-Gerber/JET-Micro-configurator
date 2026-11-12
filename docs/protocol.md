# JET Micro config protocol (firmware v1, layout v1)

Transport: USB serial, 115200 8N1, newline-terminated text lines.

**Handshake.** Opening the port resets the Nano (DTR). After the bootloader (~1 s) the firmware listens for
~1.2 s for the line `JETCFG?`. The host should send `JETCFG?\n` every ~300 ms for up to ~10 s until it gets
`OK JETCFG 1`. With no handshake the firmware switches to 31250 baud and runs normally. The firmware
transmits nothing before the handshake (the UART is also the MIDI out).

| Command | Reply | Notes |
|---|---|---|
| `PING` | `OK` | |
| `INFO` | `OK fw=1 layout=1 size=427 modes=3 slots=17 acts=2` | |
| `GET` | `OK <hex>` | Whole staged `Config` struct (see `config.h`), little-endian, 2 hex chars/byte |
| `PUT <offset> <hex>` | `OK` / `ERR ...` | Write bytes into the staged config (<= ~50 bytes per line) |
| `COMMIT` | `OK` / `ERR invalid` | Validate staged config and apply it live |
| `SAVE` | `OK` | Write the committed config to EEPROM. Takes ~1.5 s: use a >= 4 s timeout |
| `RESET` | `OK` | Load built-in defaults (staged + live). Not persisted until `SAVE` |
| `TEST <mode> <slot>` | `OK` | Fire a slot's actions. Commit first. MIDI cable should be plugged in |
| `EXIT` | `OK` | Leave config mode, resume normal operation at 31250 baud (mode 0) |

Notes
- Config mode replies leave the MIDI DIN at 115200 baud too; unplug the MIDI cable while editing.
- `TEST` switches to 31250 baud while sending, so the host sees a few garbage bytes before `OK`; skip any
  line that does not start with `OK`/`ERR` (the firmware sends an empty line first to end the garbage).
- Slot index: L short/double/long = 0/1/2, R = 3/4/5, T = 6/7/8, then combos (short, long):
  L+R = 9/10, R+T = 11/12, L+T = 13/14, L+R+T = 15/16.
- Struct layout: `uint16 doubleWindowMs, uint16 longPressMs, uint8 debounceMs, bankDelayMs, flags,
  doubleEnable[3], color[3][3], Action slot[3][17][2]` with `Action = {type, ch, a, b}` (4 bytes), offsets in order, no padding.
