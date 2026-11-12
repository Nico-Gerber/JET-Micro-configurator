// JET Micro configurator UI (Web Serial). Protocol: ../docs/protocol.md. Codec: config.js (window.JM).
(() => {
  const { T, SLOTS, BUTTONS, NUM_MODES } = JM;
  const $ = s => document.querySelector(s);
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, Number.isFinite(v) ? Math.round(v) : lo));
  function h(tag, attrs = {}, ...kids) {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === 'class') e.className = v; else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else if (v !== false && v != null) e.setAttribute(k, v === true ? '' : v);
    }
    for (const k of kids.flat()) if (k != null) e.append(k.nodeType ? k : document.createTextNode(k));
    return e;
  }

  let cfg = JM.defaults(), mode = 0, dirty = false, link = null, busy = false;

  // ---------- serial link ----------
  class Link {
    constructor(port) { this.port = port; this.lines = []; this.buf = ''; this.dec = new TextDecoder(); }
    async open() {
      await this.port.open({ baudRate: 115200 });          // opening toggles DTR -> the Nano resets
      this.writer = this.port.writable.getWriter();
      this.reader = this.port.readable.getReader();
      this.pump();
    }
    async pump() {
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          this.buf += this.dec.decode(value, { stream: true });
          let i;
          while ((i = this.buf.indexOf('\n')) >= 0) {
            const l = this.buf.slice(0, i).replace(/\r/g, '').trim(); this.buf = this.buf.slice(i + 1);
            if (l) this.lines.push(l);
          }
        }
      } catch (e) { /* port closed */ }
    }
    send(text) { return this.writer.write(new TextEncoder().encode(text + '\n')); }
    async waitFor(pred, timeout) {
      const end = Date.now() + timeout;
      while (Date.now() < end) {
        while (this.lines.length) { const r = pred(this.lines.shift()); if (r) return r; }
        await sleep(15);
      }
      return null;
    }
    async handshake() {
      const end = Date.now() + 12000;
      while (Date.now() < end) {
        await this.send('JETCFG?');
        if (await this.waitFor(l => l.startsWith('OK JETCFG') || null, 350)) return;
      }
      throw new Error('No reply from the Micro. Is it plugged in via USB (data cable) and running our firmware?');
    }
    async cmd(text, timeout = 4000) {
      this.lines.length = 0;
      await this.send(text);
      const name = text.split(' ')[0];
      // after TEST the host may see MIDI-baud garbage glued to the reply, so also accept a line ending in OK
      const r = await this.waitFor(l => /^(OK|ERR)/.test(l) ? l : /OK$/.test(l) ? 'OK' : null, timeout);
      if (r === null) throw new Error(`No reply to ${name}`);
      if (r.startsWith('ERR')) throw new Error(`${name}: ${r}`);
      return r;
    }
    async close() {
      try { await this.cmd('EXIT', 1500); } catch (e) {}
      try { await this.reader.cancel(); } catch (e) {}
      try { this.reader.releaseLock(); this.writer.releaseLock(); } catch (e) {}
      try { await this.port.close(); } catch (e) {}
    }
  }

  // ---------- state / status ----------
  function log(msg, isErr) { const l = $('#log'); l.append(h('div', { class: isErr ? 'err' : '' }, new Date().toLocaleTimeString() + '  ' + msg)); l.scrollTop = l.scrollHeight; }
  function setDirty(v = true) { dirty = v; $('#dirtyNote').hidden = !dirty; }
  function refreshControls() {
    const on = !!link;
    $('#btnConnect').disabled = on || busy; $('#btnDisconnect').disabled = !on || busy;
    $('#btnRead').disabled = !on || busy; $('#btnWrite').disabled = !on || busy;
    $('#edTest').disabled = !on || busy;
    $('#status').classList.toggle('on', on);
    $('#statusText').textContent = on ? 'Connected (config mode)' : 'Not connected';
  }
  async function guarded(label, fn) {
    if (busy) return; busy = true; refreshControls();
    try { await fn(); } catch (e) { log(`${label} failed: ${e.message}`, true); alert(`${label} failed:\n${e.message}`); }
    finally { busy = false; refreshControls(); }
  }

  // ---------- device I/O ----------
  async function readDevice() {
    const r = await link.cmd('GET');
    cfg = JM.decode(JM.fromHex(r.slice(3).trim()));
    setDirty(false); renderAll(); log('Read config from Micro.');
  }
  async function stageToDevice() {
    const problems = JM.validate(cfg);
    if (problems.length) throw new Error(problems.join('\n'));
    const bytes = JM.encode(cfg);
    for (let o = 0; o < bytes.length; o += 40) await link.cmd(`PUT ${o} ${JM.toHex(bytes.slice(o, o + 40))}`);
    await link.cmd('COMMIT');
  }
  async function writeDevice() {
    await stageToDevice();
    await link.cmd('SAVE', 8000);       // EEPROM write takes ~1.5 s
    setDirty(false); log('Saved to Micro. It runs this config now and after power-up.');
  }

  $('#btnConnect').onclick = () => guarded('Connect', async () => {
    if (!navigator.serial) throw new Error('This browser has no Web Serial. Use Chrome or Edge.');
    let port;
    try { port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x1a86 }] }); }
    catch (e) { return; }                                // chooser cancelled
    const l = new Link(port);
    log('Opening port… (the Micro resets, handshake takes ~2 s)');
    await l.open();
    try { await l.handshake(); } catch (e) { await l.close(); throw e; }
    link = l; log('Connected.');
    refreshControls();
    if (!dirty || confirm('Replace the edits in this page with the config stored on the Micro?')) await readDevice();
  });
  $('#btnDisconnect').onclick = () => guarded('Disconnect', async () => {
    const l = link; link = null; await l.close(); log('Disconnected; the Micro is back to normal operation.');
  });
  $('#btnRead').onclick = () => guarded('Read', async () => {
    if (dirty && !confirm('Discard your unsaved edits and read from the Micro?')) return;
    await readDevice();
  });
  $('#btnWrite').onclick = () => guarded('Write', writeDevice);
  if (navigator.serial) navigator.serial.addEventListener('disconnect', () => { if (link) { link = null; log('USB device removed.', true); refreshControls(); } });
  else $('#unsupported').hidden = false;

  // ---------- import / export / defaults ----------
  $('#btnExport').onclick = () => {
    const blob = new Blob([JSON.stringify({ format: 'jetmicro-config', layout: 1, config: cfg }, null, 2)], { type: 'application/json' });
    const a = h('a', { href: URL.createObjectURL(blob), download: 'jetmicro-config.json' }); a.click(); URL.revokeObjectURL(a.href);
  };
  $('#btnImport').onclick = () => $('#fileIn').click();
  $('#fileIn').onchange = async e => {
    const f = e.target.files[0]; e.target.value = ''; if (!f) return;
    try {
      const j = JSON.parse(await f.text());
      if (j.format !== 'jetmicro-config' || j.layout !== 1) throw new Error('Not a JET Micro config (layout 1).');
      const c = JM.decode(JM.encode(j.config));            // throws on missing fields
      const nums = JSON.stringify(j.config).match(/-?\d+(\.\d+)?/g) || [];
      if (nums.some(n => Number(n) < 0 || Number(n) > 65535 || !Number.isInteger(Number(n)))) throw new Error('Values out of range.');
      const p = JM.validate(c); if (p.length) throw new Error(p.join('\n'));
      cfg = c; setDirty(); renderAll(); log('Imported ' + f.name);
    } catch (err) { alert('Import failed: ' + err.message); }
  };
  $('#btnDefaults').onclick = () => { if (confirm('Replace the edits on this page with the built-in defaults? (Nothing is written until you press Write.)')) { cfg = JM.defaults(); setDirty(); renderAll(); } };

  // ---------- rendering ----------
  const hex = c => '#' + c.map(v => v.toString(16).padStart(2, '0')).join('');
  function renderTabs() {
    const t = $('#tabs'); t.replaceChildren();
    for (let m = 0; m < NUM_MODES; m++)
      t.append(h('button', { class: 'tab', role: 'tab', 'aria-selected': m === mode, onclick: () => { mode = m; renderAll(); } },
        h('span', { class: 'sw', style: `background:${hex(cfg.color[m])}` }), `Mode ${m + 1}`));
    $('#colour').value = hex(cfg.color[mode]);
  }
  $('#colour').oninput = e => { const v = e.target.value; cfg.color[mode] = [1, 3, 5].map(i => parseInt(v.slice(i, i + 2), 16)); setDirty(); renderTabs(); };

  function cellFor(slotIdx, off) {
    const acts = cfg.slot[mode][slotIdx].map(JM.describe).filter(Boolean);
    return h('td', {}, h('button', { class: 'cell' + (off ? ' off' : ''), onclick: () => openEditor(slotIdx) },
      acts.length ? acts.map(a => h('span', { class: 'a' }, a)) : h('span', { class: 'a none' }, off ? 'off' : '—')));
  }
  function renderGrid() {
    const g = $('#mainGrid'); g.replaceChildren(h('tr', {}, h('th', {}), h('th', {}, 'Short press'), h('th', {}, 'Double press'), h('th', {}, 'Long press')));
    BUTTONS.forEach((b, i) => {
      const dbl = !!(cfg.doubleEnable[mode] & (1 << i));
      g.append(h('tr', {},
        h('td', { class: 'name' }, b, h('label', {}, h('input', { type: 'checkbox', checked: dbl, onchange: e => {
          cfg.doubleEnable[mode] = (cfg.doubleEnable[mode] & ~(1 << i)) | (e.target.checked ? 1 << i : 0); setDirty(); renderAll(); } }), ' enable double')),
        cellFor(i * 3, false), cellFor(i * 3 + 1, !dbl), cellFor(i * 3 + 2, false)));
    });
    const c = $('#comboGrid'); c.replaceChildren(h('tr', {}, h('th', {}), h('th', {}, 'Short press'), h('th', {}, 'Long press')));
    for (let i = 0; i < 4; i++) c.append(h('tr', {}, h('td', { class: 'name' }, SLOTS[9 + i * 2].group), cellFor(9 + i * 2, false), cellFor(10 + i * 2, false)));
  }
  function numField(label, key, lo, hi, hint) {
    return h('label', { class: 'f' }, label, h('input', { type: 'number', min: lo, max: hi, value: cfg[key], onchange: e => {
      cfg[key] = clamp(+e.target.value, lo, hi); e.target.value = cfg[key]; setDirty(); renderNotices(); } }), hint ? h('small', {}, hint) : null);
  }
  function renderGlobals() {
    const g = $('#globals'); g.replaceChildren(
      numField('Long-press time (ms)', 'longPressMs', 200, 3000, 'Long actions fire as soon as this passes, while still held.'),
      numField('Double-press window (ms)', 'doubleWindowMs', 100, 2000, 'Time to press a second time.'),
      numField('Debounce (ms)', 'debounceMs', 2, 60, 'Raise only if you get ghost presses.'),
      numField('Bank step gap (ms)', 'bankDelayMs', 0, 255, 'Gap between the 3 CC72 messages.'),
      h('label', { class: 'f' }, 'LED feedback', h('span', { class: 'row' }, h('input', { type: 'checkbox', checked: !!(cfg.flags & 1), onchange: e => {
        cfg.flags = e.target.checked ? cfg.flags | 1 : cfg.flags & ~1; setDirty(); } }), 'Flash white when an action fires')));
  }
  function renderNotices() {
    const n = $('#notices'); n.replaceChildren(h('h2', {}, 'Notes'));
    const add = (txt, warn) => n.append(h('div', { class: warn ? 'warn' : '', style: warn ? '' : 'margin:6px 0; color:var(--muted); font-size:13px' }, txt));
    for (let m = 0; m < NUM_MODES; m++) BUTTONS.forEach((b, i) => {
      if (cfg.doubleEnable[m] & (1 << i)) add(`Mode ${m + 1} · ${b}: double press is on. A single press on this button now waits up to ${cfg.doubleWindowMs} ms before firing, in case a second press follows. Leave it off on buttons where you need instant response (e.g. bank switching).`, true);
    });
    const usesMode = cfg.slot.some(m => m.some(s => s.some(a => a.type >= T.MODE_GOTO && a.type <= T.MODE_PREV)));
    if (!usesMode) add('No slot changes mode, so you would stay in mode 1. Holding all three buttons for 3 s always returns to mode 1.', true);
    if (cfg.slot.some(m => m.some(s => s.some(a => a.type === T.BANK_STEP && a.b === 1))))
      add('Bank step "Single PC" tracks the preset on the Micro. It cannot see changes you make on the Stomp, so it can drift. Use the CC72 ×3 method if that bites.', true);
    add('Pressing several buttons together fires only the combo, not the single-button actions. Holding all three for 3 s always returns to mode 1, whatever the config says.');
    add('Unplug the MIDI cable while editing if you can; config traffic also leaves the MIDI port. Plug it back in to use "Test on Stomp".');
  }
  function renderAll() { renderTabs(); renderGrid(); renderGlobals(); renderNotices(); refreshControls(); }

  // ---------- slot editor ----------
  const TYPES = [[0, 'None'], [1, 'MIDI CC'], [2, 'MIDI CC toggle'], [3, 'Program change (preset)'], [4, 'Preset step'], [5, 'Bank step'], [6, 'Snapshot'],
    [7, 'Tuner on/off'], [8, 'Tap tempo'], [9, 'Footswitch emulation'], [10, 'Go to mode'], [11, 'Next mode'], [12, 'Previous mode'], [13, 'Wait (delay)']];
  const TYPE_DEFAULT = { 1: { a: 0, b: 127 }, 2: { a: 0, b: 127 }, 3: { a: 0, b: 0 }, 4: { a: 1, b: 0 }, 5: { a: 1, b: 0 }, 6: { a: 0, b: 0 }, 9: { a: 1, b: 0 }, 10: { a: 0, b: 0 }, 13: { a: 10, b: 0 } };
  const opts = (pairs) => pairs;
  const FIELDS = {
    ch: { label: 'MIDI channel', key: 'ch', min: 1, max: 16, shift: 1 },
    cc: { label: 'CC number', key: 'a', min: 0, max: 127 },
    val: { label: 'Value', key: 'b', min: 0, max: 127 },
    valOn: { label: '"On" value (off sends 0)', key: 'b', min: 1, max: 127 },
    pc: { label: 'Program (0–125)', key: 'a', min: 0, max: 125, hint: v => 'Preset ' + JM.pcName(v) },
    dir: { label: 'Direction', key: 'a', options: [[0, 'Down'], [1, 'Up']] },
    method: { label: 'Bank method', key: 'b', options: JM.BANK_METHODS.map((n, i) => [i, n]) },
    snap: { label: 'Snapshot', key: 'a', options: Object.entries(JM.SNAP).map(([k, v]) => [+k, v]) },
    fs: { label: 'Footswitch', key: 'a', options: [1, 2, 3, 4, 5].map(i => [i, 'FS' + i]) },
    modeSel: { label: 'Mode', key: 'a', options: [0, 1, 2].map(i => [i, 'Mode ' + (i + 1)]) },
    ms: { label: 'Delay (ms, before next message)', key: 'a', min: 0, max: 2550, scale: 10 }
  };
  const TYPE_FIELDS = { 1: ['ch', 'cc', 'val'], 2: ['ch', 'cc', 'valOn'], 3: ['ch', 'pc'], 4: ['ch', 'dir'], 5: ['ch', 'dir', 'method'], 6: ['ch', 'snap'],
    7: ['ch'], 8: ['ch'], 9: ['ch', 'fs'], 10: ['modeSel'], 13: ['ms'] };
  let edSlot = 0;

  function fieldEl(f, a) {
    const d = FIELDS[f];
    if (d.options) {
      return h('label', { class: 'f' }, d.label, h('select', { onchange: e => { a[d.key] = +e.target.value; changed(); } },
        d.options.map(([v, n]) => h('option', { value: v, selected: a[d.key] === v }, n))));
    }
    const sc = d.scale || 1, sh = d.shift || 0;
    const hint = h('small', {}, d.hint ? d.hint(a[d.key]) : '');
    return h('label', { class: 'f' }, d.label, h('input', { type: 'number', min: d.min, max: d.max, step: sc, value: a[d.key] * sc + sh, onchange: e => {
      const v = clamp(+e.target.value, d.min, d.max); e.target.value = v; a[d.key] = Math.round((v - sh) / sc); if (d.hint) hint.textContent = d.hint(a[d.key]); changed(); } }), hint);
  }
  function changed() { setDirty(); renderGrid(); renderNotices(); }
  function renderEditor() {
    const body = $('#edBody'); body.replaceChildren();
    cfg.slot[mode][edSlot].forEach((a, k) => {
      body.append(h('div', { class: 'act' }, h('h3', {}, `Action ${k + 1}${k ? ' (runs after action 1)' : ''}`),
        h('div', { class: 'grid2' },
          h('label', { class: 'f' }, 'Type', h('select', { onchange: e => {
            const t = +e.target.value; a.type = t; const d = TYPE_DEFAULT[t] || { a: 0, b: 0 }; a.a = d.a; a.b = d.b; changed(); renderEditor(); } },
            TYPES.map(([v, n]) => h('option', { value: v, selected: a.type === v }, n)))),
          (TYPE_FIELDS[a.type] || []).map(f => fieldEl(f, a)))));
    });
  }
  function openEditor(slotIdx) {
    edSlot = slotIdx; $('#edTitle').textContent = `Mode ${mode + 1} · ${SLOTS[slotIdx].label}`;
    if (SLOTS[slotIdx].kind === 'Double' && !(cfg.doubleEnable[mode] & (1 << SLOTS[slotIdx].button)))
      $('#edTitle').textContent += ' (double press is disabled for this button: enable it in the grid)';
    renderEditor(); refreshControls(); $('#editor').showModal();
  }
  $('#edDone').onclick = () => $('#editor').close();
  $('#edClear').onclick = () => { cfg.slot[mode][edSlot] = [JM.blankAction(), JM.blankAction()]; changed(); renderEditor(); };
  $('#edTest').onclick = () => guarded('Test', async () => {
    await stageToDevice();                  // applies (does not save) the current edits so the test matches the screen
    await link.cmd(`TEST ${mode} ${edSlot}`, 8000);
    log(`Tested mode ${mode + 1} · ${SLOTS[edSlot].label}.`);
  });

  window.addEventListener('beforeunload', e => { if (dirty) { e.preventDefault(); e.returnValue = ''; } });
  renderAll();
})();
