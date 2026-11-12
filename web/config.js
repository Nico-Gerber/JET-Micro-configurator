// JET Micro config codec: mirrors firmware/jetmicro/config.h (layout v1). Pure functions, no DOM.
// Works in the browser (window.JM) and in Node (module.exports) so it can be tested against a real device.
(function (root) {
  const NUM_MODES = 3, NUM_SLOTS = 17, ACTS = 2;
  const SIZE = 19 + NUM_MODES * NUM_SLOTS * ACTS * 4;   // 427
  const FLAG_FEEDBACK = 1;

  const T = { NONE: 0, CC: 1, CC_TOGGLE: 2, PC: 3, PRESET_STEP: 4, BANK_STEP: 5, SNAPSHOT: 6, TUNER: 7,
              TAP: 8, FS_EMU: 9, MODE_GOTO: 10, MODE_NEXT: 11, MODE_PREV: 12, DELAY: 13 };

  const BUTTONS = ['Left', 'Right', 'Top'];
  // slot index layout (see docs/protocol.md)
  const SLOTS = [];
  BUTTONS.forEach((b, i) => ['Short', 'Double', 'Long'].forEach((k, j) =>
    SLOTS[i * 3 + j] = { label: `${b} · ${k.toLowerCase()} press`, group: b, kind: k, button: i }));
  [['Left + Right', 3], ['Right + Top', 6], ['Left + Top', 5], ['All three', 7]].forEach(([n, mask], i) =>
    ['Short', 'Long'].forEach((k, j) => SLOTS[9 + i * 2 + j] = { label: `${n} · ${k.toLowerCase()} press`, group: n, kind: k, mask }));

  const BANK_METHODS = ['CC72 ×3 (stateless, may blip)', 'Single PC (tracked, can drift)', 'FS4/FS5 emulation (CC52/53)'];
  const SNAP = { 0: 'Snapshot 1', 1: 'Snapshot 2', 2: 'Snapshot 3', 8: 'Next snapshot', 9: 'Previous snapshot' };

  function blankAction() { return { type: 0, ch: 0, a: 0, b: 0 }; }
  function blank() {
    return {
      doubleWindowMs: 500, longPressMs: 750, debounceMs: 15, bankDelayMs: 60, flags: FLAG_FEEDBACK,
      doubleEnable: [0, 0, 0],
      color: [[255, 0, 0], [0, 255, 0], [0, 0, 255]],
      slot: Array.from({ length: NUM_MODES }, () => Array.from({ length: NUM_SLOTS }, () => [blankAction(), blankAction()]))
    };
  }
  const sl = (mode, slot) => ({ mode, slot });
  // Mirrors setDefaults() in jetmicro.ino
  function defaults() {
    const c = blank();
    const set = (m, s, k, type, ch, a, b) => { c.slot[m][s][k] = { type, ch, a, b }; };
    for (let m = 0; m < NUM_MODES; m++) set(m, 15, 0, T.MODE_NEXT, 0, 0, 0);
    set(0, 0, 0, T.BANK_STEP, 0, 0, 0); set(0, 3, 0, T.BANK_STEP, 0, 1, 0); set(0, 6, 0, T.TUNER, 0, 0, 0);
    set(1, 0, 0, T.SNAPSHOT, 0, 0, 0);  set(1, 3, 0, T.SNAPSHOT, 0, 1, 0);  set(1, 6, 0, T.SNAPSHOT, 0, 2, 0);
    set(2, 0, 0, T.PRESET_STEP, 0, 0, 0); set(2, 3, 0, T.PRESET_STEP, 0, 1, 0); set(2, 6, 0, T.TUNER, 0, 0, 0);
    return c;
  }

  function encode(c) {
    const out = new Uint8Array(SIZE), dv = new DataView(out.buffer);
    dv.setUint16(0, c.doubleWindowMs, true); dv.setUint16(2, c.longPressMs, true);
    out[4] = c.debounceMs; out[5] = c.bankDelayMs; out[6] = c.flags;
    for (let i = 0; i < 3; i++) out[7 + i] = c.doubleEnable[i];
    for (let m = 0; m < NUM_MODES; m++) for (let j = 0; j < 3; j++) out[10 + m * 3 + j] = c.color[m][j];
    let o = 19;
    for (let m = 0; m < NUM_MODES; m++) for (let s = 0; s < NUM_SLOTS; s++) for (let k = 0; k < ACTS; k++) {
      const a = c.slot[m][s][k]; out[o++] = a.type; out[o++] = a.ch; out[o++] = a.a; out[o++] = a.b;
    }
    return out;
  }
  function decode(bytes) {
    if (bytes.length !== SIZE) throw new Error(`config is ${bytes.length} bytes, expected ${SIZE}`);
    const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.length), c = blank();
    c.doubleWindowMs = dv.getUint16(0, true); c.longPressMs = dv.getUint16(2, true);
    c.debounceMs = bytes[4]; c.bankDelayMs = bytes[5]; c.flags = bytes[6];
    for (let i = 0; i < 3; i++) c.doubleEnable[i] = bytes[7 + i];
    for (let m = 0; m < NUM_MODES; m++) for (let j = 0; j < 3; j++) c.color[m][j] = bytes[10 + m * 3 + j];
    let o = 19;
    for (let m = 0; m < NUM_MODES; m++) for (let s = 0; s < NUM_SLOTS; s++) for (let k = 0; k < ACTS; k++) {
      c.slot[m][s][k] = { type: bytes[o], ch: bytes[o + 1], a: bytes[o + 2], b: bytes[o + 3] }; o += 4;
    }
    return c;
  }
  const toHex = u8 => Array.from(u8, b => b.toString(16).padStart(2, '0')).join('');
  const fromHex = s => Uint8Array.from(s.match(/../g) || [], h => parseInt(h, 16));

  // Same rules as validateConfig() in the firmware; returns a list of problems (empty = ok).
  function validate(c) {
    const p = [];
    if (!(c.doubleWindowMs >= 100 && c.doubleWindowMs <= 2000)) p.push('Double-press window must be 100–2000 ms');
    if (!(c.longPressMs >= 200 && c.longPressMs <= 3000)) p.push('Long-press time must be 200–3000 ms');
    if (!(c.debounceMs >= 2 && c.debounceMs <= 60)) p.push('Debounce must be 2–60 ms');
    c.slot.forEach((m, mi) => m.forEach((s, si) => s.forEach(a => {
      if (a.type >= 14 || a.ch > 15) p.push(`Mode ${mi + 1}, ${SLOTS[si].label}: invalid action`);
      if (a.type === T.MODE_GOTO && a.a >= NUM_MODES) p.push(`Mode ${mi + 1}, ${SLOTS[si].label}: go-to mode out of range`);
    })));
    return p;
  }

  const pcName = n => String(Math.floor(n / 3) + 1).padStart(2, '0') + 'ABC'[n % 3];
  function describe(a) {
    const ch = `ch${a.ch + 1}`;
    switch (a.type) {
      case T.NONE: return '';
      case T.CC: return `CC ${a.a} = ${a.b} (${ch})`;
      case T.CC_TOGGLE: return `CC ${a.a} toggle ${a.b}/0 (${ch})`;
      case T.PC: return `Preset ${pcName(a.a)} (PC ${a.a}, ${ch})`;
      case T.PRESET_STEP: return `Preset ${a.a ? 'up' : 'down'} (${ch})`;
      case T.BANK_STEP: return `Bank ${a.a ? 'up' : 'down'} (${['CC72×3', 'PC', 'FS emu'][a.b] || '?'}, ${ch})`;
      case T.SNAPSHOT: return `${SNAP[a.a] || 'Snapshot ' + a.a} (${ch})`;
      case T.TUNER: return `Tuner on/off (${ch})`;
      case T.TAP: return `Tap tempo (${ch})`;
      case T.FS_EMU: return `Footswitch ${a.a} (${ch})`;
      case T.MODE_GOTO: return `Go to mode ${a.a + 1}`;
      case T.MODE_NEXT: return 'Next mode';
      case T.MODE_PREV: return 'Previous mode';
      case T.DELAY: return `Wait ${a.a * 10} ms`;
    }
    return '?';
  }

  const JM = { NUM_MODES, NUM_SLOTS, ACTS, SIZE, FLAG_FEEDBACK, T, BUTTONS, SLOTS, BANK_METHODS, SNAP,
               blank, blankAction, defaults, encode, decode, toHex, fromHex, validate, describe, pcName };
  if (typeof module !== 'undefined' && module.exports) module.exports = JM; else root.JM = JM;
})(typeof window !== 'undefined' ? window : globalThis);
