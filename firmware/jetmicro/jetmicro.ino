// JET Micro HX replacement firmware - Arduino Nano (ATmega328P, old bootloader).
// Phase 2a: input engine + action engine + built-in defaults + EEPROM load.
// Phase 2b: USB-serial config mode (handshake at boot, line protocol).
#include <EEPROM.h>
#include "pins.h"
#include "config.h"

Config cfg;

// ---------- defaults (Nico's config) ----------
static const uint8_t BTN_MASK_L = 1, BTN_MASK_R = 2, BTN_MASK_T = 4;

static int8_t slotFor(uint8_t mask, uint8_t kind);   // fwd

static void setAct(uint8_t mode, uint8_t slot, uint8_t k, uint8_t type, uint8_t ch, uint8_t a, uint8_t b) {
  Action &x = cfg.slot[mode][slot][k];
  x.type = type; x.ch = ch; x.a = a; x.b = b;
}

static void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.doubleWindowMs = 500;
  cfg.longPressMs = 750;
  cfg.debounceMs = 15;
  cfg.bankDelayMs = 60;
  cfg.flags = CFG_FLAG_FEEDBACK;
  // doubleEnable stays 0 (off)
  static const uint8_t col[NUM_MODES][3] = { {255,0,0}, {0,255,0}, {0,0,255} };
  memcpy(cfg.color, col, sizeof(col));

  for (uint8_t m = 0; m < NUM_MODES; m++) {
    // ABC (all three) short = next mode, in every mode
    setAct(m, slotFor(7, PRESS_SHORT), 0, ACT_MODE_NEXT, 0, 0, 0);
  }
  // Mode 0 (red): Nico's layout. MIDI channel 0 == MIDI channel 1.
  setAct(0, slotFor(BTN_MASK_L, PRESS_SHORT), 0, ACT_BANK_STEP, 0, 0, BANK_CC72_X3); // left  = bank down
  setAct(0, slotFor(BTN_MASK_R, PRESS_SHORT), 0, ACT_BANK_STEP, 0, 1, BANK_CC72_X3); // right = bank up
  setAct(0, slotFor(BTN_MASK_T, PRESS_SHORT), 0, ACT_TUNER,     0, 0, 0);            // top   = tuner
  // Mode 1 (green): snapshots 1/2/3 on left/right/top.
  setAct(1, slotFor(BTN_MASK_L, PRESS_SHORT), 0, ACT_SNAPSHOT, 0, 0, 0);
  setAct(1, slotFor(BTN_MASK_R, PRESS_SHORT), 0, ACT_SNAPSHOT, 0, 1, 0);
  setAct(1, slotFor(BTN_MASK_T, PRESS_SHORT), 0, ACT_SNAPSHOT, 0, 2, 0);
  // Mode 2 (blue): preset down/up and tuner.
  setAct(2, slotFor(BTN_MASK_L, PRESS_SHORT), 0, ACT_PRESET_STEP, 0, 0, 0);
  setAct(2, slotFor(BTN_MASK_R, PRESS_SHORT), 0, ACT_PRESET_STEP, 0, 1, 0);
  setAct(2, slotFor(BTN_MASK_T, PRESS_SHORT), 0, ACT_TUNER,       0, 0, 0);
}

// ---------- EEPROM ----------
#define EE_MAGIC 0x4A4D   // "JM"
#define EE_VERSION 1
struct Stored { uint16_t magic; uint8_t version; uint16_t len; };

static uint8_t crc8(const uint8_t *p, uint16_t n) {
  uint8_t c = 0;
  while (n--) { c ^= *p++; for (uint8_t i = 0; i < 8; i++) c = (c & 0x80) ? (c << 1) ^ 0x07 : (c << 1); }
  return c;
}

static bool validateConfig(const Config &c) {
  if (c.doubleWindowMs < 100 || c.doubleWindowMs > 2000) return false;
  if (c.longPressMs < 200 || c.longPressMs > 3000) return false;
  if (c.debounceMs < 2 || c.debounceMs > 60) return false;
  for (uint8_t m = 0; m < NUM_MODES; m++)
    for (uint8_t s = 0; s < NUM_SLOTS; s++)
      for (uint8_t k = 0; k < ACTS_PER_SLOT; k++) {
        const Action &a = c.slot[m][s][k];
        if (a.type >= ACT_TYPE_COUNT || a.ch > 15) return false;
        if (a.type == ACT_MODE_GOTO && a.a >= NUM_MODES) return false;
      }
  return true;
}

static Config stage;   // working copy edited by the configurator (also reused as load buffer)

static bool loadConfig() {
  Stored h; EEPROM.get(0, h);
  if (h.magic != EE_MAGIC || h.version != EE_VERSION || h.len != sizeof(Config)) return false;
  Config &tmp = stage; EEPROM.get(sizeof(Stored), tmp);
  uint8_t crc = EEPROM.read(sizeof(Stored) + sizeof(Config));
  if (crc != crc8((const uint8_t*)&tmp, sizeof(Config))) return false;
  if (!validateConfig(tmp)) return false;
  cfg = tmp;
  return true;
}

// Used by the serial protocol in phase 2b.
static void saveConfig() {
  Stored h = { EE_MAGIC, EE_VERSION, (uint16_t)sizeof(Config) };
  EEPROM.put(0, h);
  EEPROM.put(sizeof(Stored), cfg);
  EEPROM.update(sizeof(Stored) + sizeof(Config), crc8((const uint8_t*)&cfg, sizeof(Config)));
}

// ---------- MIDI output queue (non-blocking, so buttons stay responsive) ----------
struct Msg { uint8_t s, a, b, n; uint16_t after; };   // n = bytes to send (0 = pure delay)
static Msg q[24];
static uint8_t qh = 0, qt = 0;
static uint32_t nextAt = 0;

static void qPush(uint8_t s, uint8_t a, uint8_t b, uint8_t n, uint16_t after) {
  uint8_t nt = (qt + 1) % 24;
  if (nt == qh) return;                 // full: drop
  q[qt] = { s, a, b, n, after };
  qt = nt;
}
static void sendCC(uint8_t ch, uint8_t cc, uint8_t val, uint16_t after = 0) { qPush(0xB0 | (ch & 15), cc, val, 3, after); }
static void sendPC(uint8_t ch, uint8_t pgm, uint16_t after = 0)            { qPush(0xC0 | (ch & 15), pgm, 0, 2, after); }
static void queueDelay(uint16_t ms) {
  if (qh != qt) q[(qt + 23) % 24].after += ms; else qPush(0, 0, 0, 0, ms);
}
static void pumpMidi(uint32_t now) {
  if (qh == qt) { nextAt = now; return; }
  if ((int32_t)(now - nextAt) < 0) return;
  Msg &m = q[qh];
  if (m.n >= 2) { Serial.write(m.s); Serial.write(m.a); if (m.n == 3) Serial.write(m.b); }
  nextAt = now + m.after;
  qh = (qh + 1) % 24;
}

// ---------- LED ----------
static uint8_t curMode = 0, prevMode = 0;
static uint32_t flashUntil = 0;

static void ledWrite(uint8_t r, uint8_t g, uint8_t b) {
  if (LED_COMMON_ANODE) { r = 255 - r; g = 255 - g; b = 255 - b; }
  analogWrite(PIN_LED_R, r); analogWrite(PIN_LED_G, g); analogWrite(PIN_LED_B, b);
}
static void ledUpdate(uint32_t now) {
  if ((int32_t)(flashUntil - now) > 0) { ledWrite(255, 255, 255); return; }
  ledWrite(cfg.color[curMode][0], cfg.color[curMode][1], cfg.color[curMode][2]);
}
static void setMode(uint8_t m) {
  if (m >= NUM_MODES || m == curMode) return;
  prevMode = curMode; curMode = m;
}

// ---------- actions ----------
static uint8_t toggleBits[(NUM_MODES * NUM_SLOTS * ACTS_PER_SLOT + 7) / 8];
static int8_t curPreset = 0;   // local preset estimate for BANK_PC_TRACK

static bool toggleFlip(uint16_t idx) {
  uint8_t &byte = toggleBits[idx >> 3]; uint8_t bit = 1 << (idx & 7);
  byte ^= bit; return (byte & bit) != 0;
}

static void bankStep(const Action &x) {
  bool up = x.a != 0;
  switch (x.b) {
    case BANK_PC_TRACK: {
      int p = (curPreset / 3) * 3 + (up ? 3 : -3);
      if (p < 0) p = (125 / 3) * 3;      // wrap to last bank (123)
      if (p > 125) p = 0;
      curPreset = p; sendPC(x.ch, p); break;
    }
    case BANK_FS_EMU:
      sendCC(x.ch, CC_FS_BASE + (up ? 5 : 4), 127, 20);
      sendCC(x.ch, CC_FS_BASE + (up ? 5 : 4), 0);
      break;
    default:   // BANK_CC72_X3
      for (uint8_t i = 0; i < 3; i++) sendCC(x.ch, CC_PRESET_STEP, up ? 127 : 0, i < 2 ? cfg.bankDelayMs : 0);
  }
}

static void runAction(const Action &x, uint16_t tIdx) {
  switch (x.type) {
    case ACT_CC:          sendCC(x.ch, x.a & 127, x.b & 127); break;
    case ACT_CC_TOGGLE:   sendCC(x.ch, x.a & 127, toggleFlip(tIdx) ? (x.b & 127) : 0); break;
    case ACT_PC:          sendPC(x.ch, x.a & 127); curPreset = x.a; break;
    case ACT_PRESET_STEP:
      sendCC(x.ch, CC_PRESET_STEP, x.a ? 127 : 0);
      curPreset = constrain(curPreset + (x.a ? 1 : -1), 0, 125); break;
    case ACT_BANK_STEP:   bankStep(x); break;
    case ACT_SNAPSHOT:    sendCC(x.ch, CC_SNAPSHOT, x.a & 127); break;
    case ACT_TUNER:       sendCC(x.ch, CC_TUNER, toggleFlip(tIdx) ? 127 : 0); break;
    case ACT_TAP:         sendCC(x.ch, CC_TAP_TEMPO, 127); break;
    case ACT_FS_EMU:      sendCC(x.ch, CC_FS_BASE + constrain(x.a, 1, 5), 127, 20);
                          sendCC(x.ch, CC_FS_BASE + constrain(x.a, 1, 5), 0); break;
    case ACT_MODE_GOTO:   setMode(x.a); break;
    case ACT_MODE_NEXT:   setMode((curMode + 1) % NUM_MODES); break;
    case ACT_MODE_PREV:   setMode(prevMode); break;
    case ACT_DELAY:       queueDelay((uint16_t)x.a * 10); break;
  }
}

static void fireSlot(int8_t slot, uint32_t now) {
  if (slot < 0) return;
  bool any = false;
  uint8_t mode = curMode;   // actions may change the mode; toggle bits belong to the mode pressed in
  for (uint8_t k = 0; k < ACTS_PER_SLOT; k++) {
    const Action x = cfg.slot[mode][slot][k];
    if (x.type == ACT_NONE) continue;
    any = true;
    runAction(x, (mode * NUM_SLOTS + slot) * ACTS_PER_SLOT + k);
  }
  if (any && (cfg.flags & CFG_FLAG_FEEDBACK)) flashUntil = now + 60;
}

// slot index for a button mask + press kind. -1 if not representable.
static int8_t slotFor(uint8_t mask, uint8_t kind) {
  switch (mask) {
    case 1: return 0 + kind;
    case 2: return 3 + kind;
    case 4: return 6 + kind;
    case 3: case 6: case 5: case 7: {
      if (kind == PRESS_DOUBLE) return -1;
      uint8_t idx = (mask == 3) ? 0 : (mask == 6) ? 1 : (mask == 5) ? 2 : 3;
      return 9 + idx * 2 + (kind == PRESS_LONG ? 1 : 0);
    }
  }
  return -1;
}
static uint8_t btnIndex(uint8_t mask) { return mask == 1 ? 0 : mask == 2 ? 1 : 2; }
static bool isSingle(uint8_t mask) { return mask == 1 || mask == 2 || mask == 4; }

// ---------- input engine ----------
static const uint8_t btnPin[NUM_BTN] = { PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_TOP };
static bool rawState[NUM_BTN], stable[NUM_BTN];
static uint32_t rawT[NUM_BTN];

static uint8_t scanButtons(uint32_t now) {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < NUM_BTN; i++) {
    bool raw = digitalRead(btnPin[i]) == LOW;
    if (raw != rawState[i]) { rawState[i] = raw; rawT[i] = now; }
    else if (raw != stable[i] && now - rawT[i] >= cfg.debounceMs) stable[i] = raw;
    if (stable[i]) mask |= 1 << i;
  }
  return mask;
}

static bool sessionActive = false, consumed = false, fallbackDone = false;
static uint8_t peak = 0;
static uint32_t startT = 0;
static int8_t pendMask = 0;           // single-button short press waiting to see if a double follows
static uint32_t pendT = 0;

static void flushPending(uint32_t now) {
  if (pendMask) { fireSlot(slotFor(pendMask, PRESS_SHORT), now); pendMask = 0; }
}

static void onShort(uint8_t mask, uint32_t now) {
  if (isSingle(mask) && (cfg.doubleEnable[curMode] & (1 << btnIndex(mask)))) {
    if (pendMask == mask) { pendMask = 0; fireSlot(slotFor(mask, PRESS_DOUBLE), now); return; }
    flushPending(now);
    pendMask = mask; pendT = now;    // window runs from this release
    return;
  }
  flushPending(now);
  fireSlot(slotFor(mask, PRESS_SHORT), now);
}

static void inputTick(uint32_t now) {
  uint8_t down = scanButtons(now);

  if (pendMask && now - pendT >= cfg.doubleWindowMs) flushPending(now);

  if (down) {
    if (!sessionActive) { sessionActive = true; peak = down; startT = now; consumed = false; fallbackDone = false; }
    else peak |= down;
    uint32_t held = now - startT;
    if (!consumed && held >= cfg.longPressMs) {
      consumed = true;
      flushPending(now);
      fireSlot(slotFor(peak, PRESS_LONG), now);
    }
    // Hard-coded escape hatch: hold all three for 3 s -> back to mode 0, whatever the config says.
    if (!fallbackDone && down == 7 && held >= 3000) {
      fallbackDone = true; consumed = true;
      setMode(0); flashUntil = now + 300;
    }
  } else if (sessionActive) {
    sessionActive = false;
    if (!consumed) onShort(peak, now);
  }
}

// ---------- config mode (USB serial, line protocol) ----------
// Boot: listen at 115200 for "JETCFG?" for ~1.2 s, transmitting NOTHING (the UART is also the MIDI out).
// No handshake -> switch to 31250 baud and run normally.
// While in config mode all replies also leave the MIDI DIN at 115200: unplug the MIDI cable
// while editing (plug it in only for TEST).
#define FW_VERSION 1

static bool inConfig = false;

static int hexNib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
static void printHex(uint8_t b) { if (b < 16) Serial.print('0'); Serial.print(b, HEX); }

static void cmdTest(uint8_t mode, uint8_t slot) {
  uint8_t saved = curMode;
  curMode = mode;
  Serial.flush();
  Serial.begin(31250);                       // MIDI baud while the messages go out
  fireSlot(slot, millis());
  uint32_t t0 = millis();
  while (qh != qt && millis() - t0 < 3000) pumpMidi(millis());
  Serial.flush();
  Serial.begin(115200);
  curMode = saved;
}

static void handleLine(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;
  if (!strcmp(cmd, "PING")) { Serial.println(F("OK")); }
  else if (!strcmp(cmd, "INFO")) {
    Serial.print(F("OK fw=")); Serial.print(FW_VERSION);
    Serial.print(F(" layout=")); Serial.print(EE_VERSION);
    Serial.print(F(" size=")); Serial.print(sizeof(Config));
    Serial.print(F(" modes=")); Serial.print(NUM_MODES);
    Serial.print(F(" slots=")); Serial.print(NUM_SLOTS);
    Serial.print(F(" acts=")); Serial.println(ACTS_PER_SLOT);
  }
  else if (!strcmp(cmd, "GET")) {
    Serial.print(F("OK "));
    const uint8_t *p = (const uint8_t*)&stage;
    for (uint16_t i = 0; i < sizeof(Config); i++) printHex(p[i]);
    Serial.println();
  }
  else if (!strcmp(cmd, "PUT")) {           // PUT <offset> <hex bytes>
    char *o = strtok(NULL, " "), *h = strtok(NULL, " ");
    if (!o || !h) { Serial.println(F("ERR args")); return; }
    uint16_t off = atoi(o), len = strlen(h) / 2;
    if (strlen(h) & 1 || off + len > sizeof(Config)) { Serial.println(F("ERR range")); return; }
    uint8_t *p = (uint8_t*)&stage + off;
    for (uint16_t i = 0; i < len; i++) {
      int hi = hexNib(h[2*i]), lo = hexNib(h[2*i+1]);
      if (hi < 0 || lo < 0) { Serial.println(F("ERR hex")); return; }
      p[i] = (hi << 4) | lo;
    }
    Serial.println(F("OK"));
  }
  else if (!strcmp(cmd, "COMMIT")) {        // validate staged config and apply it live
    if (validateConfig(stage)) { cfg = stage; Serial.println(F("OK")); }
    else Serial.println(F("ERR invalid"));
  }
  else if (!strcmp(cmd, "SAVE")) {          // persist the committed config
    saveConfig(); Serial.println(F("OK"));
  }
  else if (!strcmp(cmd, "RESET")) {         // back to built-in defaults (not saved until SAVE)
    setDefaults(); stage = cfg; Serial.println(F("OK"));
  }
  else if (!strcmp(cmd, "TEST")) {          // TEST <mode> <slot>: commit first; MIDI cable should be plugged in
    char *m = strtok(NULL, " "), *s = strtok(NULL, " ");
    if (!m || !s || atoi(m) >= NUM_MODES || atoi(s) >= NUM_SLOTS) { Serial.println(F("ERR args")); return; }
    cmdTest(atoi(m), atoi(s));
    Serial.println();                         // terminate any MIDI garbage the host saw
    Serial.println(F("OK"));
  }
  else if (!strcmp(cmd, "EXIT")) {
    Serial.println(F("OK")); Serial.flush();
    inConfig = false; curMode = 0;
    Serial.begin(31250);
  }
  else Serial.println(F("ERR unknown"));
}

static bool waitHandshake() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  char buf[16]; uint8_t n = 0;
  while (millis() - t0 < 1200) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        buf[n] = 0;
        if (!strcmp(buf, "JETCFG?")) return true;
        n = 0;
      } else if (n < sizeof(buf) - 1) buf[n++] = c;
    }
  }
  return false;
}

static void configLoop(uint32_t now) {
  static char buf[112]; static uint8_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (n) { buf[n] = 0; n = 0; handleLine(buf); } }
    else if (n < sizeof(buf) - 1) buf[n++] = c;
    else n = 0;                                  // overlong line: drop it
  }
  // purple breathing = config mode
  uint8_t ph = (now / 6) & 0x1FF;                // 0..511
  uint8_t v = ph < 256 ? ph : 511 - ph;
  ledWrite(v / 2, 0, v);
}

// ---------- main ----------
void setup() {
  for (uint8_t i = 0; i < NUM_BTN; i++) { pinMode(btnPin[i], INPUT_PULLUP); }
  pinMode(PIN_LED_R, OUTPUT); pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_B, OUTPUT);
  if (!loadConfig()) setDefaults();
  ledUpdate(millis());
  if (waitHandshake()) {
    inConfig = true; stage = cfg;
    Serial.println(F("OK JETCFG 1"));
  } else {
    Serial.begin(31250);             // normal operation: MIDI out
  }
}

void loop() {
  uint32_t now = millis();
  if (inConfig) { configLoop(now); return; }
  inputTick(now);
  pumpMidi(now);
  ledUpdate(now);
}
