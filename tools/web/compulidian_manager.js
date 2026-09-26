// ============================================================================
// GM percussion map
// ============================================================================
const GM_PERC = {
  35:'Acoustic BD', 36:'Bass Drum',    37:'Rim Shot',    38:'Snare',
  39:'Hand Clap',   40:'Elec Snare',   41:'Lo Floor Tom',42:'Closed HH',
  43:'Hi Floor Tom',44:'Pedal HH',     45:'Low Tom',     46:'Open HH',
  47:'Lo-Mid Tom',  48:'Hi-Mid Tom',   49:'Crash Cym 1', 50:'High Tom',
  51:'Ride Cym 1',  52:'Chinese Cym',  53:'Ride Bell',   54:'Tambourine',
  55:'Splash Cym',  56:'Cowbell',      57:'Crash Cym 2', 58:'Vibra',
  59:'Ride Cym 2',  60:'Hi Bongo',     61:'Lo Bongo',    62:'Mute Hi Conga',
  63:'Open Hi Conga', 64:'Lo Conga',   65:'Hi Timbale',  66:'Lo Timbale',  
  69:'Cabasa',      70:'Maracas',      75:'Claves',      76:'Hi Wood Blk', 
  77:'Lo Wood Blk', 80:'Mute Triangle',81:'Open Triangle',
};

function noteLabel(note, channel) {
  if (note == null || note < 0) return '—';
  const gm = GM_PERC[note];
  return gm ? `${note} (${gm}, ch ${channel})` : `${note} (ch ${channel})`;
}

function escapeHtml(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, c => (
    { '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;' }[c]
  ));
}

// ============================================================================
// Bank config format  (must match include/audio/bank_config.h / bank_config_pack.py)
// ============================================================================
// A bank is a small saveloadlib-format text file (loaded by BankManager from
// "/save/bank_N.txt") that references samples already present in the shared
// content-addressed SampleStore by content hash - it never embeds PCM data
// itself. See tools/bank_config_pack.py for the reference implementation
// this must stay byte-compatible with.
const BANK_CONFIG_MAX_SLOTS = 32;
// Reinterprets sample_id as a 1-based compiled-in-voice index (0 stays
// reserved for "unassigned") rather than a SampleStore hash when set - see
// include/audio/bank_config.h. Superseded the old opt-in "fallback if
// unresolved" meaning of this bit; that safety net is now unconditional and
// needs no flag/checkbox at all.
const BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX = 0x01;
const BANK_NAME_MAX = 24;  // includes terminating NUL

function bytesToHex(bytes) {
  let s = '';
  for (const b of bytes) s += b.toString(16).padStart(2, '0');
  return s;
}

function hexToBytes(hex) {
  const clean = (hex || '').trim();
  const out = new Uint8Array(Math.floor(clean.length / 2));
  for (let i = 0; i < out.length; i++) {
    out[i] = parseInt(clean.substr(i * 2, 2), 16) & 0xFF;
  }
  return out;
}

// bankSlotsArr: parallel to the device's slots[] - {sampleId, volume, midiNote, isCompiled}.
// sampleId is a SampleStore content hash when isCompiled is false, or a
// 0-based compiled-voice index (see include/audio/bank_config.h) when true.
function buildSlotConfigBytes(bankSlotsArr) {
  const buf = new Uint8Array(BANK_CONFIG_MAX_SLOTS * 7);  // zero-filled = unassigned
  const dv  = new DataView(buf.buffer);
  const n   = Math.min(bankSlotsArr.length, BANK_CONFIG_MAX_SLOTS);
  for (let i = 0; i < n; i++) {
    const s = bankSlotsArr[i];
    if (!s) continue;
    if (!s.isCompiled && !s.sampleId) continue;  // truly unassigned
    const off = i * 7;
    const wireId = s.isCompiled ? ((s.sampleId >>> 0) + 1) : (s.sampleId >>> 0);
    dv.setUint32(off, wireId >>> 0, true);
    buf[off + 4] = s.volume & 0xFF;
    buf[off + 5] = s.midiNote & 0xFF;
    buf[off + 6] = s.isCompiled ? BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX : 0;
  }
  return buf;
}

function buildBankNameBytes(name) {
  const buf = new Uint8Array(BANK_NAME_MAX);
  const enc = new TextEncoder().encode((name || '').substring(0, BANK_NAME_MAX - 1));
  buf.set(enc);
  return buf;
}

// Produces the exact saveloadlib text file BankManager expects - must stay
// line-for-line compatible with tools/bank_config_pack.py's write_config_file().
function buildBankConfigFileText(bankName, bankSlotsArr) {
  const slotsHex = bytesToHex(buildSlotConfigBytes(bankSlotsArr));
  const nameHex  = bytesToHex(buildBankNameBytes(bankName));
  const lines = [
    '#saveloadlib_version=compulidian_manager.html',
    '#saveloadlib_format=1',
    '#saveloadlib_scope=0x02:SL_SCOPE_PROJECT',
    `slots=${slotsHex}`,
    `name=${nameHex}`,
  ];
  return lines.join('\n') + '\n';
}

function parseBankConfigFileText(text) {
  const map = {};
  for (const rawLine of text.split('\n')) {
    const line = rawLine.trim();
    if (!line || line.startsWith('#')) continue;
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    map[line.substring(0, eq)] = line.substring(eq + 1);
  }

  const slotsBytes = hexToBytes(map['slots'] || '');
  const nameBytes  = hexToBytes(map['name'] || '');
  const dv = new DataView(slotsBytes.buffer, slotsBytes.byteOffset, slotsBytes.byteLength);

  const parsedSlots = [];
  const count = Math.floor(slotsBytes.length / 7);
  for (let i = 0; i < count; i++) {
    const off = i * 7;
    const flags = slotsBytes[off + 6];
    const wireId = dv.getUint32(off, true);
    const isCompiled = !!(flags & BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX);
    parsedSlots.push({
      sampleId: isCompiled ? Math.max(0, wireId - 1) : wireId,
      volume: slotsBytes[off + 4],
      midiNote: slotsBytes[off + 5],
      isCompiled,
    });
  }

  const dec = new TextDecoder();
  const bankName = dec.decode(nameBytes).replace(/\0/g, '').trim();
  return { bankName, slots: parsedSlots };
}

// ============================================================================
// Slot state
// ============================================================================
// slots: device-reported descriptors, fetched via SYSEX_TYPE_GET_SLOTS_REQ.
// Positional and authoritative - never invented/edited by this tool.
let slots = [];  // [{ label, note, channel }]
// bankSlots: the bank being edited, same length/order as slots. sampleId
// references an entry in sampleList by content hash (0 = unassigned), or a
// compiled-voice index when isCompiled is true - see buildSlotConfigBytes.
let bankSlots = [];  // [{ sampleId, volume, midiNote, isCompiled }]
// sampleList: samples currently present in the device's SampleStore, fetched
// via SYSEX_TYPE_GET_SAMPLES_INFO_REQ/GET_SAMPLES_CHUNK_REQ.
let sampleList = [];  // [{ hash, name, rate, numSamples, bitDepth, dataOffset }]
// Hashes staged for removal from the on-device sample store on the next
// "Build Update UF2" - excluded when the index is rewritten. Does not free
// their PCM bytes in flash (see buildAndDownloadIncrementalUf2 comment).
let pendingDeletions = new Set();

// Sample type categories (Phase F.4) - index 0 is the implicit "unset" state.
// Declared early (rather than alongside the other SYSEX_* constants further
// down) because updateSlotOptions() references it via a synchronous call at
// script load time, before those later consts would otherwise be initialized.
const SAMPLE_TYPES = ['Unset', 'Kick', 'Stick', 'Clap', 'Snare', 'Cymbal', 'Tamb', 'HiTom', 'LoTom', 'PHH', 'OHH', 'CHH', 'Ride', 'Perc'];

// Sample metadata (Phase F.4): type + tags per sample, keyed by content hash
// (hex string, e.g. "1a2b3c4d") for SampleStore entries, or "compiled:<label>"
// for built-in compiled voices. Loaded from/saved to the device's single
// /save/sample_meta.txt file via GET/SET_SAMPLE_META_CHUNK_REQ. Firmware
// treats this file as opaque bytes - format and meaning live entirely here.
// { [key]: { type: 'Kick'|...|'Unset', tags: string[] } }
let sampleMeta = {};

function sampleMetaKeyForHash(hash) {
  return hash.toString(16);
}
function sampleMetaKeyForCompiled(label) {
  return `compiled:${label}`;
}

function getSampleMeta(key) {
  if (sampleMeta[key]) return sampleMeta[key];
  // Compiled-in voices have no stored metadata by default (nothing to load
  // from the device for them) - derive sensible defaults from their label
  // instead of showing them as blank/unset: they're always the built-in 808
  // kit (see SAMPLESET=R808 in platformio.ini), and their label already
  // matches the same keywords guessSampleType() uses for uploaded samples.
  if (typeof key === 'string' && key.startsWith('compiled:')) {
    const label = key.slice('compiled:'.length);
    return { type: guessSampleType(label), tags: ['808'] };
  }
  return { type: 'Unset', tags: [] };
}

function setSampleMetaType(key, type) {
  const m = sampleMeta[key] || { type: 'Unset', tags: [] };
  m.type = type;
  sampleMeta[key] = m;
}

function setSampleMetaTags(key, tags) {
  const m = sampleMeta[key] || { type: 'Unset', tags: [] };
  m.tags = tags;
  sampleMeta[key] = m;
}

// Parses/serializes /save/sample_meta.txt: one line per entry,
// "<key>,<type_id>,<tag1>|<tag2>|..." (type_id is an index into SAMPLE_TYPES;
// tags list may be empty). Blank lines and lines starting with '#' ignored.
function parseSampleMetaText(text) {
  const map = {};
  for (const rawLine of text.split('\n')) {
    const line = rawLine.trim();
    if (!line || line.startsWith('#')) continue;
    const parts = line.split(',');
    if (parts.length < 2) continue;
    const key = parts[0];
    const typeIdx = parseInt(parts[1], 10);
    const type = SAMPLE_TYPES[typeIdx] || 'Unset';
    const tags = (parts[2] || '').split('|').map(t => t.trim()).filter(Boolean);
    map[key] = { type, tags };
  }
  return map;
}

function buildSampleMetaText(metaMap) {
  const lines = ['#compulidian_sample_meta_version=1'];
  for (const key of Object.keys(metaMap)) {
    const m = metaMap[key];
    const typeIdx = Math.max(0, SAMPLE_TYPES.indexOf(m.type || 'Unset'));
    const tags = (m.tags || []).join('|');
    lines.push(`${key},${typeIdx},${tags}`);
  }
  return lines.join('\n') + '\n';
}

// Filename-keyword heuristic to pre-fill a sensible type guess for newly
// staged/imported samples. Order matters - more specific checks first (e.g.
// CHH's generic "hat" pattern is checked last so "open hat"/"closed hat"
// aren't misclassified).
function guessSampleType(filename) {
  const n = (filename || '').toLowerCase();
  if (/\bkick\b|\bbd\b/.test(n)) return 'Kick';
  if (/\bstick\b|\brim\b/.test(n)) return 'Stick';
  if (/\bclap\b|\bcp\b/.test(n)) return 'Clap';
  if (/\bsnare\b|\bsd\b|\bsn\b/.test(n)) return 'Snare';
  if (/\bcym(bal)?\b|\bcrash\b|\bsplash\b/.test(n)) return 'Cymbal';
  if (/\btamb\b/.test(n)) return 'Tamb';
  if (/\bhi.?tom\b/.test(n)) return 'HiTom';
  if (/\blo.?tom\b/.test(n)) return 'LoTom';
  if (/\bphh\b|\bpedal\b/.test(n)) return 'PHH';
  if (/\bohh\b|\bopen.?hat\b/.test(n)) return 'OHH';
  if (/\bchh\b|\bclosed.?hat\b|\bhihat\b|\bhi.?hat\b|\bhat\b/.test(n)) return 'CHH';
  if (/\bride\b/.test(n)) return 'Ride';
  if (/\bperc\b|\bshaker\b|\bconga\b|\bbongo\b|\bcowbell\b/.test(n)) return 'Perc';
  return 'Unset';
}

async function fetchSampleMetaFromDevice() {
  if (!midiOutput) throw new Error('Not connected via MIDI');

  const info = await requestSysexResponse(
    SYSEX_TYPE_GET_SAMPLE_META_INFO_REQ,
    [],
    [SYSEX_TYPE_SAMPLE_META_INFO_RESP],
    MIDI_BANK_CHUNK_TIMEOUT_MS,
    MIDI_MAX_RETRIES
  );
  const totalBytes = decodeU32_7bit(info.payload || [], 0);
  if (totalBytes === 0) {
    sampleMeta = {};
    return sampleMeta;
  }

  const full = new Uint8Array(totalBytes);
  let offset = 0;
  let chunkIdx = 0;
  while (offset < totalBytes) {
    const idxLo = chunkIdx & 0x7F;
    const idxHi = (chunkIdx >> 7) & 0x7F;
    const resp = await requestSysexResponse(
      SYSEX_TYPE_GET_SAMPLE_META_CHUNK_REQ,
      [idxLo, idxHi],
      [SYSEX_TYPE_SAMPLE_META_CHUNK_RESP],
      MIDI_BANK_CHUNK_TIMEOUT_MS,
      MIDI_MAX_RETRIES
    );
    const c = resp.payload || [];
    if (c.length < 5) throw new Error(`Short sample meta chunk response (${c.length} bytes)`);
    const gotIdx = (c[0] & 0x7F) | ((c[1] & 0x7F) << 7);
    const rawLen = (c[2] & 0x7F) | ((c[3] & 0x7F) << 7);
    const isLast = (c[4] & 0x7F) !== 0;
    const hexLen = rawLen * 2;
    if (gotIdx !== chunkIdx) throw new Error(`Sample meta chunk index mismatch (got ${gotIdx}, expected ${chunkIdx})`);
    if (c.length < 5 + hexLen) throw new Error('Sample meta chunk hex payload truncated');
    if (offset + rawLen > full.length) throw new Error('Sample meta chunk exceeds expected size');

    for (let i = 0; i < rawLen; i++) {
      const hi = hexNibble(c[5 + i * 2]);
      const lo = hexNibble(c[5 + i * 2 + 1]);
      full[offset + i] = (hi << 4) | lo;
    }
    offset += rawLen;
    chunkIdx++;
    if (isLast) break;
  }

  const text = new TextDecoder().decode(full.slice(0, offset));
  sampleMeta = parseSampleMetaText(text);
  return sampleMeta;
}

async function writeSampleMetaToDevice() {
  if (!midiOutput) { setStatus('Connect via Web MIDI first.', true); return; }

  const text = buildSampleMetaText(sampleMeta);
  const bytes = new TextEncoder().encode(text);
  if (bytes.length > SYSEX_META_WRITE_MAX_BYTES) {
    setStatus(`Sample metadata too large (${bytes.length} bytes, max ${SYSEX_META_WRITE_MAX_BYTES}).`, true);
    return;
  }

  setStatus('Saving sample metadata to device…');
  showProgress(0);
  try {
    const chunkSize = SYSEX_META_WRITE_RAW_CHUNK_BYTES;
    const totalChunks = Math.max(1, Math.ceil(bytes.length / chunkSize));
    for (let chunkIdx = 0; chunkIdx < totalChunks; chunkIdx++) {
      const offset = chunkIdx * chunkSize;
      const chunk = bytes.slice(offset, offset + chunkSize);
      const isLast = (offset + chunk.length) >= bytes.length;
      const rawLen = chunk.length;
      const payload = [
        chunkIdx & 0x7F, (chunkIdx >> 7) & 0x7F,
        rawLen & 0x7F, (rawLen >> 7) & 0x7F,
        isLast ? 1 : 0,
      ];
      for (const b of chunk) {
        payload.push(HEX_CHARS.charCodeAt((b >> 4) & 0xF), HEX_CHARS.charCodeAt(b & 0xF));
      }
      await requestSysexResponse(
        SYSEX_TYPE_SET_SAMPLE_META_CHUNK_REQ,
        payload,
        [SYSEX_TYPE_ACK],
        MIDI_BANK_CHUNK_TIMEOUT_MS,
        MIDI_MAX_RETRIES
      );
      showProgress((offset + chunk.length) / bytes.length);
    }
    hideProgress();
    setStatus('✓ Sample metadata saved to device.', false, true);
  } catch (e) {
    hideProgress();
    setStatus(`Save failed: ${(e && e.message) || e}`, true);
  }
}
window.writeSampleMetaToDevice = writeSampleMetaToDevice;

function emptyBankSlot() {
  return { sampleId: 0, volume: 127, midiNote: 0, isCompiled: false };
}

// midiNote always mirrors the device's current slot mapping for that
// position - there is no user-editable per-slot override (removed: it was
// confusing and easy to leave stale/wrong after re-pulling a bank).
function syncBankSlotNotesToDevice() {
  for (let i = 0; i < bankSlots.length; i++) {
    if (!bankSlots[i]) bankSlots[i] = emptyBankSlot();
    const slot = slots[i];
    bankSlots[i].midiNote = (slot && slot.note >= 0) ? slot.note : 0;
  }
}

function resetBankSlotsForCurrentSlots() {
  bankSlots = slots.map(() => emptyBankSlot());
  syncBankSlotNotesToDevice();
}

// Bank-editing state, shown in the bank-management banners:
// - bankDirty: true once bankSlots has been edited in-browser since the last
//   successful save/pull, i.e. there are changes not yet on the device.
// - loadedForSlot: which device bank-slot number (1-4) the in-memory
//   bankSlots actually came from (via a pull), or null if it's just a blank
//   template that was never read from/written to a real slot.
let bankDirty = false;
let loadedForSlot = null;
// bankValidity[slot] = true/false once known (via GET_BANK_INFO_REQ), absent if unknown.
let bankValidity = {};

function markBankDirty() {
  bankDirty = true;
  updateBankStateBanner();
}
window.markBankDirty = markBankDirty;

function updateBankStateBanner() {
  const activeEl = document.getElementById('bank-active-indicator');
  const stateEl  = document.getElementById('bank-state-banner');
  if (!activeEl || !stateEl) return;

  const selectedSlot = parseInt(document.getElementById('bank-slot').value);
  const activeBankEl = document.getElementById('cfg-active-bank');
  const activeBank = activeBankEl ? parseInt(activeBankEl.value) : NaN;

  if (!isNaN(activeBank)) {
    activeEl.textContent = (selectedSlot === activeBank)
      ? `Editing slot ${selectedSlot} — this is the device's currently active bank.`
      : `Editing slot ${selectedSlot} — the device is currently playing bank ${activeBank}.`;
  } else {
    activeEl.textContent = '';
  }

  if (bankDirty) {
    stateEl.textContent = '● Unsaved changes — click 💾 Save Bank to Device to apply them (live, no reboot needed).';
    stateEl.style.color = 'var(--accent)';
  } else if (loadedForSlot !== selectedSlot) {
    stateEl.textContent = 'This bank slot has not been read from the device yet — showing a blank/unrelated template. Use ⬆ Read Bank from Device first if you want to see what’s actually there before editing.';
    stateEl.style.color = 'var(--muted)';
  } else {
    stateEl.textContent = '';
  }
}

function updateBankSlotOptionLabels() {
  const sel = document.getElementById('bank-slot');
  if (!sel) return;
  for (const opt of sel.options) {
    const slot = parseInt(opt.value);
    opt.textContent = (bankValidity[slot] === false) ? `Slot ${slot} (empty)` : `Slot ${slot}`;
  }
}

function updateBankActionAvailability() {
  const slot = parseInt(document.getElementById('bank-slot').value);
  const pullBtn = document.getElementById('pull-from-device-btn');
  if (!pullBtn) return;
  const known = bankValidity[slot];
  pullBtn.disabled = (known === false);
  pullBtn.title = (known === false) ? 'This bank slot is empty on the device — nothing to read.' : '';
}

// Best-effort: query each bank slot's validity so the slot dropdown and Read
// button can reflect empty slots instead of failing with a raw NACK later.
async function refreshBankValidity() {
  if (!midiOutput) return;
  for (let slot = 1; slot <= 4; slot++) {
    try {
      await requestSysexResponse(SYSEX_TYPE_GET_BANK_INFO_REQ, [slot & 0x7F], [SYSEX_TYPE_BANK_INFO_RESP], 1200, 1);
      bankValidity[slot] = true;
    } catch (e) {
      const msg = (e && e.message) || '';
      if (msg.includes('NACK 4')) bankValidity[slot] = false;
      // Any other failure (timeout etc.) is inconclusive - leave unknown so we
      // don't block the user with a false negative.
    }
  }
  updateBankSlotOptionLabels();
  updateBankActionAvailability();
}

function onBankSlotChanged() {
  updateBankActionAvailability();
  updateBankStateBanner();
}
window.onBankSlotChanged = onBankSlotChanged;

function updateSlotsGateUI() {
  const gate   = document.getElementById('slots-gate');
  const editor = document.getElementById('bank-editor');
  const have   = slots.length > 0;
  gate.style.display   = have ? 'none' : 'block';
  editor.style.display = have ? 'block' : 'none';
}

// ============================================================================
// Sample store listing
// ============================================================================
// Lists the device's shared content-addressed SampleStore so voices can be
// assigned an already-uploaded sample by name instead of a raw content hash.
async function fetchSampleList() {
  if (!midiOutput) throw new Error('Not connected via MIDI');

  const info = await requestSysexResponse(SYSEX_TYPE_GET_SAMPLES_INFO_REQ, [], [SYSEX_TYPE_SAMPLES_INFO_RESP]);
  const totalEntries = decodeU32_7bit(info.payload || [], 0);

  const entries = [];
  let chunkIdx = 0;
  while (entries.length < totalEntries) {
    const idxLo = chunkIdx & 0x7F;
    const idxHi = (chunkIdx >> 7) & 0x7F;
    const resp = await requestSysexResponse(
      SYSEX_TYPE_GET_SAMPLES_CHUNK_REQ,
      [idxLo, idxHi],
      [SYSEX_TYPE_SAMPLES_CHUNK_RESP],
      MIDI_BANK_CHUNK_TIMEOUT_MS,
      MIDI_MAX_RETRIES
    );
    const c = resp.payload || [];
    if (c.length < 5) throw new Error(`Short samples chunk response (${c.length} bytes)`);
    const rawLen = (c[2] & 0x7F) | ((c[3] & 0x7F) << 7);
    const isLast = (c[4] & 0x7F) !== 0;
    if (c.length < 5 + rawLen * 2) throw new Error('Samples chunk hex payload truncated');

    const raw = new Uint8Array(rawLen);
    for (let i = 0; i < rawLen; i++) {
      raw[i] = (hexNibble(c[5 + i * 2]) << 4) | hexNibble(c[5 + i * 2 + 1]);
    }

    const dv = new DataView(raw.buffer);
    const dec = new TextDecoder();
    for (let off = 0; off + 48 <= raw.length; off += 48) {
      const hash       = dv.getUint32(off, true);
      const dataOffset = dv.getUint32(off + 4, true);
      const numSamples = dv.getUint32(off + 8, true);
      const sampleRate = dv.getUint32(off + 12, true);
      const bitDepth   = raw[off + 16];
      const name       = dec.decode(raw.slice(off + 20, off + 44)).replace(/\0/g, '').trim();
      entries.push({ hash, name, rate: sampleRate, numSamples, bitDepth, dataOffset });
    }

    chunkIdx++;
    if (isLast) break;
  }

  sampleList = entries;
}

async function refreshSampleList() {
  if (!midiOutput) { setStatus('Connect via Web MIDI first.', true); return; }
  setStatus('Refreshing sample list…');
  try {
    await fetchSampleList();
    try { await fetchSampleMetaFromDevice(); } catch (e) { /* best-effort - metadata file may not exist yet */ }
    document.getElementById('samples-empty-hint').style.display = sampleList.length ? 'none' : 'block';
    setStatus(`✓ Found ${sampleList.length} sample(s) in the device's sample store.`, false, true);
  } catch (e) {
    setStatus('Failed to read sample list: ' + (e && e.message), true);
  }
  updateSlotTable();
  renderDeviceSamplesList();
}
window.refreshSampleList = refreshSampleList;

function formatBytes(n) {
  if (n >= 1024 * 1024) return (n / (1024 * 1024)).toFixed(2) + ' MB';
  if (n >= 1024) return (n / 1024).toFixed(1) + ' KB';
  return n + ' B';
}

function sampleTypeSelectHtml(key) {
  const current = getSampleMeta(key).type || 'Unset';
  let html = `<select onchange="onSampleTypeChanged('${key}', this.value)" style="width:80px">`;
  for (const t of SAMPLE_TYPES) {
    html += `<option value="${t}"${t === current ? ' selected' : ''}>${t}</option>`;
  }
  html += `</select>`;
  return html;
}

function onSampleTypeChanged(key, type) {
  setSampleMetaType(key, type);
}
window.onSampleTypeChanged = onSampleTypeChanged;

function onSampleTagsChanged(key, value) {
  const tags = value.split(',').map(t => t.trim()).filter(Boolean);
  setSampleMetaTags(key, tags);
}
window.onSampleTagsChanged = onSampleTagsChanged;

function renderDeviceSamplesList() {
  const tbody = document.getElementById('device-samples-list');
  const summary = document.getElementById('device-samples-summary');
  if (!tbody) return;
  tbody.innerHTML = '';
  for (const s of sampleList) {
    const bytesPerSample = (s.bitDepth || 16) / 8;
    const sizeBytes = s.numSamples * bytesPerSample;
    const dur = s.rate ? (s.numSamples / s.rate).toFixed(2) + 's' : '?';
    const replacement = stagedSamples.find(ss => ss.replacesHash === s.hash);
    const pendingDelete = !replacement && pendingDeletions.has(s.hash);
    const tr = document.createElement('tr');
    if (pendingDelete || replacement) tr.style.opacity = '.6';
    let actionsHtml;
    if (replacement) {
      actionsHtml = `<span style="color:var(--accent);font-size:.8rem">will be replaced by "${escapeHtml(replacement.name)}"</span> `
        + `<button class="btn" onclick="undoReplaceDeviceSample(${s.hash})">Undo</button>`;
    } else if (pendingDelete) {
      actionsHtml = `<span style="color:var(--danger);font-size:.8rem">marked for deletion</span> `
        + `<button class="btn" onclick="undeleteDeviceSample(${s.hash})">Undo</button>`;
    } else {
      actionsHtml = `<button class="btn" onclick="deleteDeviceSample(${s.hash})" title="Mark for deletion (freed on next Build Update UF2)">🗑 Delete</button> `
        + `<button class="btn" onclick="replaceDeviceSample(${s.hash})" title="Replace this sample's audio, keeping the same reference so bank slots don't need reassigning">⇄ Replace</button>`;
    }
    tr.innerHTML = `
      <td style="${pendingDelete ? 'text-decoration:line-through' : ''}">${escapeHtml(s.name || '(unnamed)')}</td>
      <td>0x${s.hash.toString(16).padStart(8, '0')}</td>
      <td>${s.rate} Hz</td>
      <td>${dur}</td>
      <td>${formatBytes(sizeBytes)}</td>
      <td>${sampleTypeSelectHtml(sampleMetaKeyForHash(s.hash))}</td>
      <td><input type="text" value="${escapeHtml((getSampleMeta(sampleMetaKeyForHash(s.hash)).tags || []).join(', '))}"
           placeholder="tag1, tag2" style="width:110px"
           onchange="onSampleTagsChanged('${sampleMetaKeyForHash(s.hash)}', this.value)"></td>
      <td style="white-space:nowrap">${actionsHtml}</td>
    `;
    tbody.appendChild(tr);
  }
  if (summary) {
    const totalBytes = sampleList.reduce((sum, s) => sum + s.numSamples * ((s.bitDepth || 16) / 8), 0);
    summary.textContent = sampleList.length
      ? `${sampleList.length} sample(s), ${formatBytes(totalBytes)} total`
      : 'No samples found on device yet.';
  }
}

function deleteDeviceSample(hash) {
  pendingDeletions.add(hash >>> 0);
  renderDeviceSamplesList();
}
window.deleteDeviceSample = deleteDeviceSample;

function undeleteDeviceSample(hash) {
  pendingDeletions.delete(hash >>> 0);
  renderDeviceSamplesList();
}
window.undeleteDeviceSample = undeleteDeviceSample;

// Explicit Replace (Phase F.3): deliberately reuses the target entry's
// existing content_hash for the new audio (rather than computing a fresh
// one), so any bank slot already referencing it picks up the new sample
// automatically - no manual re-linking needed. Implemented as a delete of
// the original (same mechanism as deleteDeviceSample) plus a staged sample
// whose hash is forced to match, so buildAndDownloadIncrementalUf2 needs no
// special-casing beyond what Phase F.2 already added.
function replaceDeviceSample(hash) {
  const original = sampleList.find(s => s.hash === hash);
  if (!original) return;
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.wav,audio/wav';
  input.onchange = async () => {
    const file = input.files[0];
    if (!file) return;
    try {
      const buf = await file.arrayBuffer();
      const { sampleRate, numSamples, pcm } = parseWavFile(buf);
      const pcmBytes = pcmBytesFromInt16(pcm);
      stagedSamples = stagedSamples.filter(s => s.replacesHash !== original.hash);
      stagedSamples.push({
        name: original.name, rate: sampleRate, numSamples, pcm, pcmBytes,
        hash: original.hash, fileSize: file.size, replacesHash: original.hash,
      });
      pendingDeletions.add(original.hash);
      renderStagedSamplesList();
      renderDeviceSamplesList();
      setStatus(`✓ Staged replacement audio for "${original.name}" — build an update UF2 to apply.`, false, true);
    } catch (e) {
      setStatus(`Failed to read ${file.name}: ${e.message}`, true);
    }
  };
  input.click();
}
window.replaceDeviceSample = replaceDeviceSample;

function undoReplaceDeviceSample(hash) {
  const h = hash >>> 0;
  pendingDeletions.delete(h);
  stagedSamples = stagedSamples.filter(s => s.replacesHash !== h);
  renderDeviceSamplesList();
  renderStagedSamplesList();
}
window.undoReplaceDeviceSample = undoReplaceDeviceSample;

// Compiled-in (built-in firmware) samples are exposed as first-class picker
// entries alongside SampleStore ones (Phase F.6) - their names come from the
// already-fetched slots[]/GET_SLOTS_REQ labels, positionally: compiled voice
// index N corresponds to slots[N]. Any slot can pick any compiled voice's
// sample, not just its own.
function sampleOptionsHtml(selectedId, isCompiledSelected) {
  let html = `<option value="0"${(!selectedId && !isCompiledSelected) ? ' selected' : ''}>— none / unassigned —</option>`;
  if (slots.length) {
    html += `<optgroup label="Compiled-in (built-in kit)">`;
    for (let ci = 0; ci < slots.length; ci++) {
      const sel = (isCompiledSelected && selectedId === ci) ? ' selected' : '';
      const key = sampleMetaKeyForCompiled((slots[ci] && slots[ci].label) || `voice ${ci}`);
      if (!sel && !sampleMatchesFilter(key)) continue;
      const label = escapeHtml((slots[ci] && slots[ci].label) || `voice ${ci}`);
      html += `<option value="c:${ci}"${sel}>Compiled: ${label}</option>`;
    }
    html += `</optgroup>`;
  }
  if (sampleList.length) {
    html += `<optgroup label="Sample store">`;
    for (const s of sampleList) {
      const sel = (!isCompiledSelected && selectedId === s.hash) ? ' selected' : '';
      if (!sel && !sampleMatchesFilter(sampleMetaKeyForHash(s.hash))) continue;
      const dur = s.rate ? (s.numSamples / s.rate).toFixed(2) + 's' : '?';
      const label = escapeHtml(s.name || `0x${s.hash.toString(16)}`);
      html += `<option value="${s.hash}"${sel}>${label} — ${dur}, ${s.rate}Hz</option>`;
    }
    html += `</optgroup>`;
  }
  return html;
}

// Handles the sample <select>'s onchange - value is either "c:<index>" for a
// compiled-in voice or a numeric SampleStore content hash.
function onSampleSelectChanged(i, rawValue) {
  if (!bankSlots[i]) bankSlots[i] = emptyBankSlot();
  const wasUnassigned = !bankSlots[i].isCompiled && !bankSlots[i].sampleId;
  if (typeof rawValue === 'string' && rawValue.startsWith('c:')) {
    bankSlots[i].isCompiled = true;
    bankSlots[i].sampleId = parseInt(rawValue.slice(2), 10) || 0;
  } else {
    bankSlots[i].isCompiled = false;
    bankSlots[i].sampleId = parseInt(rawValue, 10) || 0;
  }
  const nowAssigned = bankSlots[i].isCompiled || !!bankSlots[i].sampleId;
  // A slot's volume byte defaults to 0 in the packed wire format while
  // unassigned (see buildSlotConfigBytes) - that 0 was never a deliberate
  // choice by the bank or the user, so don't let it carry over as a silent
  // voice the moment a sample gets assigned.
  if (wasUnassigned && nowAssigned && !bankSlots[i].volume) {
    bankSlots[i].volume = 127;
  }
  markBankDirty();
  updateSlotTable();
}
window.onSampleSelectChanged = onSampleSelectChanged;


// ============================================================================
// Sample store upload builder (Phase F) — WAV parsing, content hashing and an
// incremental UF2 image builder that only ever appends new samples, matching
// tools/sample_store_pack.py's on-disk format exactly so bank_config_pack.py
// and this tool stay interoperable. Runs entirely client-side; no server.
// ============================================================================

// stagedSamples: WAVs the user has added this session but not yet flashed.
// [{ name, rate, numSamples, pcm(Int16Array mono), hash(uint32), fileSize }]
let stagedSamples = [];

// Minimal WAV (RIFF/PCM) parser — deliberately avoids AudioContext.decodeAudioData()
// because that silently resamples to the context's output rate; we need the
// file's own native sample rate preserved untouched, to match the Python tool.
function parseWavFile(arrayBuffer) {
  const dv = new DataView(arrayBuffer);
  if (dv.getUint32(0, false) !== 0x52494646 /* 'RIFF' */ || dv.getUint32(8, false) !== 0x57415645 /* 'WAVE' */) {
    throw new Error('Not a RIFF/WAVE file');
  }

  let fmt = null;
  let dataOffset = -1, dataLen = 0;
  let pos = 12;
  while (pos + 8 <= dv.byteLength) {
    const id = dv.getUint32(pos, false);
    const size = dv.getUint32(pos + 4, true);
    const body = pos + 8;
    if (id === 0x666D7420 /* 'fmt ' */) {
      fmt = {
        formatCode: dv.getUint16(body, true),
        numChannels: dv.getUint16(body + 2, true),
        sampleRate: dv.getUint32(body + 4, true),
        bitsPerSample: dv.getUint16(body + 14, true),
      };
    } else if (id === 0x64617461 /* 'data' */) {
      dataOffset = body;
      dataLen = Math.min(size, dv.byteLength - body);
    }
    pos = body + size + (size & 1);  // chunks are word-aligned
  }
  if (!fmt) throw new Error('Missing fmt chunk');
  if (dataOffset < 0) throw new Error('Missing data chunk');

  const { formatCode, numChannels, sampleRate, bitsPerSample } = fmt;
  const bytesPerSample = bitsPerSample / 8;
  const frameCount = Math.floor(dataLen / (bytesPerSample * numChannels));
  const mono = new Float32Array(frameCount);

  for (let i = 0; i < frameCount; i++) {
    let sum = 0;
    for (let ch = 0; ch < numChannels; ch++) {
      const off = dataOffset + (i * numChannels + ch) * bytesPerSample;
      let v;
      if (formatCode === 3 && bitsPerSample === 32) {          // IEEE float
        v = dv.getFloat32(off, true);
      } else if (bitsPerSample === 8) {                        // unsigned 8-bit
        v = (dv.getUint8(off) - 128) / 128;
      } else if (bitsPerSample === 16) {
        v = dv.getInt16(off, true) / 32768;
      } else if (bitsPerSample === 24) {
        const b0 = dv.getUint8(off), b1 = dv.getUint8(off + 1), b2 = dv.getUint8(off + 2);
        let raw = (b2 << 16) | (b1 << 8) | b0;
        if (raw & 0x800000) raw -= 0x1000000;
        v = raw / 8388608;
      } else if (bitsPerSample === 32) {
        v = dv.getInt32(off, true) / 2147483648;
      } else {
        throw new Error(`Unsupported bit depth ${bitsPerSample}`);
      }
      sum += v;
    }
    mono[i] = sum / numChannels;
  }

  let peak = 0;
  for (let i = 0; i < mono.length; i++) peak = Math.max(peak, Math.abs(mono[i]));
  const scale = peak > 0 ? (0.95 / peak) : 1;

  // Matches numpy's `(data * 32767.0).clip(-32768, 32767).astype(int16)` in
  // sample_store_pack.py exactly (truncates toward zero, not round-to-nearest)
  // so identical WAV input produces an identical content hash in both tools.
  const pcm = new Int16Array(mono.length);
  for (let i = 0; i < mono.length; i++) {
    const clipped = Math.max(-32768, Math.min(32767, mono[i] * scale * 32767));
    pcm[i] = Math.trunc(clipped);
  }

  return { sampleRate, numSamples: pcm.length, pcm };
}

const CRC32_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(bytes) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) c = CRC32_TABLE[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

function fnv1a32(bytes) {
  let h = 0x811C9DC5;
  for (let i = 0; i < bytes.length; i++) {
    h ^= bytes[i];
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h >>> 0;
}

// Extracts the little-endian PCM byte view used both for content hashing and
// for packing into a sample-store entry.
function pcmBytesFromInt16(pcmInt16) {
  const pcmBytes = new Uint8Array(pcmInt16.length * 2);
  const pdv = new DataView(pcmBytes.buffer);
  for (let i = 0; i < pcmInt16.length; i++) pdv.setInt16(i * 2, pcmInt16[i], true);
  return pcmBytes;
}

// Content hash over {name, numSamples, PCM checksum} — must match
// tools/sample_store_pack.py's compute_content_hash() exactly, so hashes
// computed here match anything already flashed by the Python tool.
function computeContentHash(name, numSamples, pcmInt16) {
  const pcmBytes = pcmBytesFromInt16(pcmInt16);
  const pcmCrc = crc32(pcmBytes);

  const nameBytes = new TextEncoder().encode(name);
  const blob = new Uint8Array(nameBytes.length + 8);
  blob.set(nameBytes, 0);
  new DataView(blob.buffer).setUint32(nameBytes.length, numSamples >>> 0, true);
  new DataView(blob.buffer).setUint32(nameBytes.length + 4, pcmCrc, true);
  return { hash: fnv1a32(blob), pcmBytes };
}

async function handleSampleFiles(fileList) {
  for (const file of fileList) {
    if (!/\.wav$/i.test(file.name)) continue;
    try {
      const buf = await file.arrayBuffer();
      const { sampleRate, numSamples, pcm } = parseWavFile(buf);
      const name = file.name.replace(/\.wav$/i, '').substring(0, 23);
      const { hash, pcmBytes } = computeContentHash(name, numSamples, pcm);
      stagedSamples.push({ name, rate: sampleRate, numSamples, pcm, pcmBytes, hash, fileSize: file.size });
      const metaKey = sampleMetaKeyForHash(hash);
      if (!sampleMeta[metaKey]) {
        setSampleMetaType(metaKey, guessSampleType(file.name));
      }
    } catch (e) {
      setStatus(`Failed to read ${file.name}: ${e.message}`, true);
    }
  }
  renderStagedSamplesList();
}

function handleSampleDrop(ev) {
  ev.preventDefault();
  ev.currentTarget.classList.remove('over');
  handleSampleFiles(ev.dataTransfer.files);
}
window.handleSampleDrop = handleSampleDrop;

function handleSampleFileInput(ev) {
  handleSampleFiles(ev.target.files);
  ev.target.value = '';
}
window.handleSampleFileInput = handleSampleFileInput;

function removeStagedSample(idx) {
  const s = stagedSamples[idx];
  if (s && s.replacesHash) pendingDeletions.delete(s.replacesHash);
  stagedSamples.splice(idx, 1);
  renderStagedSamplesList();
  renderDeviceSamplesList();
}
window.removeStagedSample = removeStagedSample;

function renameStagedSample(idx, newName) {
  const s = stagedSamples[idx];
  if (!s) return;
  s.name = (newName || '').substring(0, 23);
  // Replacement entries deliberately keep the hash of the sample they're
  // replacing (see replaceDeviceSample) - renaming must not recompute it.
  if (s.replacesHash) { renderStagedSamplesList(); return; }
  const { hash, pcmBytes } = computeContentHash(s.name, s.numSamples, s.pcm);
  s.hash = hash;
  s.pcmBytes = pcmBytes;
  renderStagedSamplesList();
}
window.renameStagedSample = renameStagedSample;

// In-browser playback preview, decoded straight from the parsed PCM (not
// re-decoded from the file), so what you hear is exactly what will be
// packed — this is the "preview before committing" step.
let previewAudioCtx = null;
function previewStagedSample(idx) {
  const s = stagedSamples[idx];
  if (!s) return;
  if (!previewAudioCtx) {
    try { previewAudioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: s.rate }); }
    catch (e) { previewAudioCtx = new (window.AudioContext || window.webkitAudioContext)(); }
  }
  const ctx = previewAudioCtx;
  const buffer = ctx.createBuffer(1, s.pcm.length, s.rate === ctx.sampleRate ? s.rate : ctx.sampleRate);
  const chan = buffer.getChannelData(0);
  for (let i = 0; i < s.pcm.length; i++) chan[i] = s.pcm[i] / 32768;
  const src = ctx.createBufferSource();
  src.buffer = buffer;
  src.connect(ctx.destination);
  src.start();
}
window.previewStagedSample = previewStagedSample;

function renderStagedSamplesList() {
  const tbody = document.getElementById('staged-samples-list');
  if (!tbody) return;
  tbody.innerHTML = '';
  const deviceHashes = new Set(sampleList.map(s => s.hash));
  const seenHashes = new Set();

  stagedSamples.forEach((s, idx) => {
    const dur = (s.numSamples / s.rate).toFixed(2) + 's';
    let status = 'ready';
    if (s.replacesHash) status = `replacing 0x${s.replacesHash.toString(16).padStart(8, '0')}`;
    else if (deviceHashes.has(s.hash)) status = 'already on device';
    else if (seenHashes.has(s.hash)) status = 'duplicate (skipped)';
    seenHashes.add(s.hash);

    const nameCell = s.replacesHash
      ? escapeHtml(s.name)
      : `<input type="text" value="${escapeHtml(s.name)}" maxlength="23" style="width:140px"
            onchange="renameStagedSample(${idx}, this.value)">`;
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${nameCell}</td>
      <td>${s.rate} Hz</td>
      <td>${dur}</td>
      <td>${formatBytes(s.pcm.length * 2)}</td>
      <td style="color:${status === 'ready' ? 'var(--muted)' : 'var(--accent)'}">${status}</td>
      <td>
        <button class="btn" onclick="previewStagedSample(${idx})" title="Preview">▶</button>
        <button class="btn" onclick="removeStagedSample(${idx})" title="Remove">✕</button>
      </td>
    `;
    tbody.appendChild(tr);
  });
}

function packEntryHeader(contentHash, dataOffset, numSamples, sampleRate, bitDepth, name) {
  const buf = new Uint8Array(SAMPLESTORE_ENTRY_SIZE);
  const dv = new DataView(buf.buffer);
  dv.setUint32(0, contentHash >>> 0, true);
  dv.setUint32(4, dataOffset >>> 0, true);
  dv.setUint32(8, numSamples >>> 0, true);
  dv.setUint32(12, sampleRate >>> 0, true);
  buf[16] = bitDepth & 0xFF;
  buf[17] = SAMPLESTORE_ENTRY_FLAG_USED;
  const nameBytes = new TextEncoder().encode((name || '').substring(0, 23));
  buf.set(nameBytes, 20);
  return buf;
}

function makeUf2Block(targetAddr, data, blockIdx, totalBlocks) {
  const block = new Uint8Array(UF2_BLOCK_SIZE);
  const dv = new DataView(block.buffer);
  dv.setUint32(0, UF2_MAGIC_START0, true);
  dv.setUint32(4, UF2_MAGIC_START1, true);
  dv.setUint32(8, 0x00002000, true);   // flags: family ID present
  dv.setUint32(12, targetAddr, true);
  dv.setUint32(16, UF2_DATA_SIZE, true);
  dv.setUint32(20, blockIdx, true);
  dv.setUint32(24, totalBlocks, true);
  dv.setUint32(28, RP2040_FAMILY_ID, true);
  block.set(data, 32);
  dv.setUint32(UF2_BLOCK_SIZE - 4, UF2_MAGIC_END, true);
  return block;
}

function bytesToUf2Blocks(bytes, baseAddr) {
  const padLen = (UF2_DATA_SIZE - (bytes.length % UF2_DATA_SIZE)) % UF2_DATA_SIZE;
  const padded = new Uint8Array(bytes.length + padLen);
  padded.set(bytes);
  padded.fill(0xFF, bytes.length);

  const totalBlocks = padded.length / UF2_DATA_SIZE;
  const blocks = [];
  for (let i = 0; i < totalBlocks; i++) {
    const chunk = padded.subarray(i * UF2_DATA_SIZE, (i + 1) * UF2_DATA_SIZE);
    blocks.push({ addr: baseAddr + i * UF2_DATA_SIZE, data: chunk });
  }
  return blocks;
}

function triggerDownload(bytes, filename, mime) {
  const blob = new Blob([bytes], { type: mime || 'application/octet-stream' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

// Builds a UF2 that only ever touches: (a) the fixed 16 KB index region
// (fully rewritten each time, containing existing entries verbatim plus the
// new ones appended), and (b) the new PCM bytes themselves, starting at the
// next free offset rounded UP to the next flash sector boundary so the write
// never shares a sector with — and therefore never risks erasing — any
// already-flashed sample. Existing on-device samples are never re-read or
// re-written, so building/flashing this multiple times over time is safe as
// long as you refresh the on-device sample list (reconnect) between flashes.
async function buildAndDownloadIncrementalUf2() {
  if (!midiOutput) { setStatus('Connect via Web MIDI first to read the current sample store state.', true); return; }

  setStatus('Reading current device sample list…');
  try {
    await fetchSampleList();
  } catch (e) {
    setStatus('Failed to read device sample list: ' + e.message, true);
    return;
  }
  renderDeviceSamplesList();

  // Entries that survive into the rebuilt index: existing device entries
  // minus anything marked for deletion. Deleted hashes are treated as if
  // absent for dedup purposes too, so re-staging identical content re-adds it.
  const keptExisting = sampleList.filter(s => !pendingDeletions.has(s.hash));
  const removedCount = sampleList.length - keptExisting.length;

  const seen = new Set(keptExisting.map(s => s.hash));
  const toAdd = [];
  for (const s of stagedSamples) {
    if (seen.has(s.hash)) continue;  // already kept on device, or duplicate within this batch
    seen.add(s.hash);
    toAdd.push(s);
  }

  if (toAdd.length === 0 && removedCount === 0) {
    setStatus('Nothing to change — all staged samples are already on the device (or empty), and nothing is marked for deletion.', true);
    return;
  }
  if (keptExisting.length + toAdd.length > MAX_SAMPLESTORE_ENTRIES) {
    setStatus(`Too many samples: keeping ${keptExisting.length}, adding ${toAdd.length} would exceed the ${MAX_SAMPLESTORE_ENTRIES} limit.`, true);
    return;
  }

  let existingNextFree = 0;
  for (const s of sampleList) {
    existingNextFree = Math.max(existingNextFree, s.dataOffset + s.numSamples * ((s.bitDepth || 16) / 8));
  }
  const startOffset = (existingNextFree % FLASH_SECTOR_SIZE === 0)
    ? existingNextFree
    : Math.ceil(existingNextFree / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;

  // Build the full entry list: kept-existing (verbatim, original offsets) + new (appended).
  let offset = startOffset;
  const newPcmParts = [];
  const newEntryHeaders = [];
  const nameToHash = {};
  for (const s of toAdd) {
    newEntryHeaders.push(packEntryHeader(s.hash, offset, s.numSamples, s.rate, 16, s.name));
    newPcmParts.push(s.pcmBytes);
    nameToHash[s.name] = `0x${s.hash.toString(16).toUpperCase().padStart(8, '0')}`;
    offset += s.pcmBytes.length;
  }
  const nextFreeDataOffset = offset;

  const totalEntries = keptExisting.length + toAdd.length;
  const index = new Uint8Array(SAMPLESTORE_INDEX_SIZE);
  const idv = new DataView(index.buffer);
  idv.setUint32(0, SAMPLESTORE_MAGIC, true);
  idv.setUint32(4, SAMPLESTORE_VERSION, true);
  idv.setUint32(8, totalEntries, true);
  idv.setUint32(12, nextFreeDataOffset, true);

  let entryPos = SAMPLESTORE_HEADER_FIXED_SIZE;
  for (const s of keptExisting) {
    index.set(packEntryHeader(s.hash, s.dataOffset, s.numSamples, s.rate, s.bitDepth || 16, s.name), entryPos);
    entryPos += SAMPLESTORE_ENTRY_SIZE;
  }
  for (const h of newEntryHeaders) {
    index.set(h, entryPos);
    entryPos += SAMPLESTORE_ENTRY_SIZE;
  }

  let totalPcm = 0;
  for (const p of newPcmParts) totalPcm += p.length;
  const newPcm = new Uint8Array(totalPcm);
  let pcmPos = 0;
  for (const p of newPcmParts) { newPcm.set(p, pcmPos); pcmPos += p.length; }

  if (FLASH_SAMPLESTORE_DATA_OFFSET + nextFreeDataOffset - FLASH_SAMPLESTORE_OFFSET > FLASH_SAMPLESTORE_SIZE_16MB) {
    setStatus('Error: this update would exceed the sample store\'s flash region (16 MB boards only, 12 MB max).', true);
    return;
  }

  const baseAddr = XIP_BASE + FLASH_SAMPLESTORE_OFFSET;
  const dataBaseAddr = XIP_BASE + FLASH_SAMPLESTORE_DATA_OFFSET;
  const indexBlocks = bytesToUf2Blocks(index, baseAddr);
  const pcmBlocks = bytesToUf2Blocks(newPcm, dataBaseAddr + startOffset);
  const allBlocks = indexBlocks.concat(pcmBlocks);

  const out = new Uint8Array(allBlocks.length * UF2_BLOCK_SIZE);
  allBlocks.forEach((b, i) => {
    out.set(makeUf2Block(b.addr, b.data, i, allBlocks.length), i * UF2_BLOCK_SIZE);
  });

  triggerDownload(out, 'sample_store_update.uf2');
  triggerDownload(new TextEncoder().encode(JSON.stringify(nameToHash, null, 2)), 'sample_store_update.uf2.map.json', 'application/json');

  const deletedNote = removedCount ? `, removed ${removedCount} (index-only, flash space not reclaimed)` : '';
  setStatus(`✓ Built update UF2 with ${toAdd.length} new sample(s)${deletedNote} (${formatBytes(totalPcm)}). `
    + `Flash it via BOOTSEL drag-and-drop, then reconnect/refresh before building another.`, false, true);
  pendingDeletions.clear();
}
window.buildAndDownloadIncrementalUf2 = buildAndDownloadIncrementalUf2;

// Full rebuild, discarding the device's entire current sample store index and
// PCM region: only stagedSamples (in-browser right now, e.g. from an
// imported .samplib.json) survive. This is the only way to actually reclaim
// flash space wasted by prior deletions/replacements - unlike the
// incremental builder above, it does NOT preserve any existing device entry.
async function rebuildStoreFromLibrary() {
  if (stagedSamples.length === 0) {
    setStatus('Nothing staged - import a library or add WAVs first, so there is something to rebuild from.', true);
    return;
  }
  const proceed = confirm(
    `This discards the device's ENTIRE current sample store and replaces it with only the ` +
    `${stagedSamples.length} sample(s) currently staged in this tool. Any device sample not ` +
    `also staged here will be permanently lost. Continue?`
  );
  if (!proceed) return;

  const seen = new Set();
  const kept = [];
  for (const s of stagedSamples) {
    if (seen.has(s.hash)) continue;
    seen.add(s.hash);
    kept.push(s);
  }
  if (kept.length > MAX_SAMPLESTORE_ENTRIES) {
    setStatus(`Too many samples: ${kept.length} exceeds the ${MAX_SAMPLESTORE_ENTRIES} limit.`, true);
    return;
  }

  let offset = 0;
  const pcmParts = [];
  const entryHeaders = [];
  const nameToHash = {};
  for (const s of kept) {
    entryHeaders.push(packEntryHeader(s.hash, offset, s.numSamples, s.rate, 16, s.name));
    pcmParts.push(s.pcmBytes);
    nameToHash[s.name] = `0x${s.hash.toString(16).toUpperCase().padStart(8, '0')}`;
    offset += s.pcmBytes.length;
  }
  const nextFreeDataOffset = offset;

  const index = new Uint8Array(SAMPLESTORE_INDEX_SIZE);
  const idv = new DataView(index.buffer);
  idv.setUint32(0, SAMPLESTORE_MAGIC, true);
  idv.setUint32(4, SAMPLESTORE_VERSION, true);
  idv.setUint32(8, kept.length, true);
  idv.setUint32(12, nextFreeDataOffset, true);

  let entryPos = SAMPLESTORE_HEADER_FIXED_SIZE;
  for (const h of entryHeaders) {
    index.set(h, entryPos);
    entryPos += SAMPLESTORE_ENTRY_SIZE;
  }

  let totalPcm = 0;
  for (const p of pcmParts) totalPcm += p.length;
  const pcm = new Uint8Array(totalPcm);
  let pcmPos = 0;
  for (const p of pcmParts) { pcm.set(p, pcmPos); pcmPos += p.length; }

  if (nextFreeDataOffset > FLASH_SAMPLESTORE_SIZE_16MB) {
    setStatus('Error: this rebuild would exceed the sample store\'s flash region (16 MB boards only, 12 MB max).', true);
    return;
  }

  const baseAddr = XIP_BASE + FLASH_SAMPLESTORE_OFFSET;
  const dataBaseAddr = XIP_BASE + FLASH_SAMPLESTORE_DATA_OFFSET;
  const indexBlocks = bytesToUf2Blocks(index, baseAddr);
  const pcmBlocks = bytesToUf2Blocks(pcm, dataBaseAddr);
  const allBlocks = indexBlocks.concat(pcmBlocks);

  const out = new Uint8Array(allBlocks.length * UF2_BLOCK_SIZE);
  allBlocks.forEach((b, i) => {
    out.set(makeUf2Block(b.addr, b.data, i, allBlocks.length), i * UF2_BLOCK_SIZE);
  });

  triggerDownload(out, 'sample_store_full_rebuild.uf2');
  triggerDownload(new TextEncoder().encode(JSON.stringify(nameToHash, null, 2)), 'sample_store_full_rebuild.uf2.map.json', 'application/json');

  setStatus(`✓ Built full rebuild UF2 with ${kept.length} sample(s) (${formatBytes(totalPcm)}), reclaiming all ` +
    `previously-wasted flash space. Flash it via BOOTSEL drag-and-drop - this REPLACES the entire store.`, false, true);
  pendingDeletions.clear();
}
window.rebuildStoreFromLibrary = rebuildStoreFromLibrary;

// "Library" export/import: a portable bundle of staged (not-yet-flashed)
// samples for backup or sharing between users — bundles raw WAV-equivalent
// PCM + metadata as JSON (base64), independent of what's actually on any
// particular device.
function exportSampleLibrary() {
  if (stagedSamples.length === 0) { setStatus('Nothing staged to export.', true); return; }
  const bundle = {
    format: 'compulidian-sample-library',
    version: 1,
    samples: stagedSamples.map(s => ({
      name: s.name,
      rate: s.rate,
      numSamples: s.numSamples,
      pcmBase64: bytesToBase64(s.pcmBytes),
    })),
  };
  triggerDownload(new TextEncoder().encode(JSON.stringify(bundle)), 'sample_library.samplib.json', 'application/json');
  setStatus(`✓ Exported ${stagedSamples.length} sample(s) to sample_library.samplib.json`, false, true);
}
window.exportSampleLibrary = exportSampleLibrary;

function bytesToBase64(bytes) {
  let binary = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

function base64ToBytes(b64) {
  const binary = atob(b64);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out;
}

async function handleLibraryFileInput(ev) {
  const file = ev.target.files[0];
  ev.target.value = '';
  if (!file) return;
  try {
    const text = await file.text();
    const bundle = JSON.parse(text);
    if (bundle.format !== 'compulidian-sample-library') throw new Error('Not a recognised sample library file');
    for (const item of bundle.samples) {
      const pcmBytes = base64ToBytes(item.pcmBase64);
      const pcm = new Int16Array(pcmBytes.length / 2);
      const pdv = new DataView(pcmBytes.buffer, pcmBytes.byteOffset, pcmBytes.byteLength);
      for (let i = 0; i < pcm.length; i++) pcm[i] = pdv.getInt16(i * 2, true);
      const { hash } = computeContentHash(item.name, item.numSamples, pcm);
      stagedSamples.push({ name: item.name, rate: item.rate, numSamples: item.numSamples, pcm, pcmBytes, hash, fileSize: pcmBytes.length });
    }
    renderStagedSamplesList();
    setStatus(`✓ Imported ${bundle.samples.length} sample(s) from library.`, false, true);
  } catch (e) {
    setStatus('Failed to import library: ' + e.message, true);
  }
}
window.handleLibraryFileInput = handleLibraryFileInput;

function updateSlotTable() {
  const tbody = document.getElementById('slot-list');
  tbody.innerHTML = '';

  for (let i = 0; i < slots.length; i++) {
    const slot = slots[i];
    const s    = bankSlots[i] || emptyBankSlot();
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${i + 1}</td>
      <td><strong>${escapeHtml(slot.label)}</strong></td>
      <td style="color:var(--muted);font-size:.8rem">${noteLabel(slot.note, slot.channel)}</td>
      <td class="name-cell"><select onchange="onSampleSelectChanged(${i}, this.value)">${sampleOptionsHtml(s.sampleId, s.isCompiled)}</select></td>
      <td><input type="number" min="0" max="127" value="${s.volume}" style="width:56px"
           onchange="setSlotField(${i}, 'volume', parseInt(this.value)||0)"></td>
      <td style="white-space:nowrap">
        <button class="btn audition-button" data-note="${slot.note}" style="padding:.2rem .5rem" title="Audition on device (active bank only)" onclick="auditionSlotDevice(${i})">🔊</button>
      </td>`;
    tbody.appendChild(tr);
  }
  updateBankStateBanner();
}

function setSlotField(i, field, value) {
  if (!bankSlots[i]) bankSlots[i] = emptyBankSlot();
  bankSlots[i][field] = value;
  markBankDirty();
  updateSlotTable();
}
window.setSlotField = setSlotField;

function auditionSlotDevice(i) {
  const slot = slots[i];
  const bankSlot = bankSlots[i];
  if (!slot || slot.note < 0) { setStatus('This slot has no MIDI note to trigger.', true); return; }
  if (!midiOutput) { setStatus('Connect via Web MIDI to audition on the device.', true); return; }
  try {
    console.log(`Auditioning slot ${i + 1} on device: note ${slot.note}, channel ${slot.channel}, velocity ${bankSlot.volume}`);
    midiOutput.playNote(
      slot.note, 
      { 
        channels: slot.channel > 0 ? slot.channel : 10, 
        rawAttack: bankSlot.volume, 
        duration: 200
      }
    );
  } catch(e) {
    setStatus('Audition failed: ' + e.message, true);
  }
}
window.auditionSlotDevice = auditionSlotDevice;

// ============================================================================
// Bank slot options
// ============================================================================
function updateSlotOptions() {
  const sel = document.getElementById('bank-slot');
  sel.innerHTML = '';
  for (let i = 1; i <= 4; i++) {
    const opt = document.createElement('option');
    opt.value = i; opt.textContent = `Slot ${i}`;
    sel.appendChild(opt);
  }
  updateBankSlotOptionLabels();
  updateBankActionAvailability();
  updateBankStateBanner();

  const filterSel = document.getElementById('bank-filter-type');
  if (filterSel && !filterSel.dataset.populated) {
    filterSel.dataset.populated = '1';
    let html = '<option value="">Any type</option>';
    for (const t of SAMPLE_TYPES) html += `<option value="${t}">${t}</option>`;
    filterSel.innerHTML = html;
  }
}
window.updateSlotOptions = updateSlotOptions;
updateSlotOptions();

// Filter bar (Phase F.5) narrowing the sample picker's options in each slot
// row - purely a display filter, never affects what's actually assigned.
function bankFilterActive() {
  const showAll = document.getElementById('bank-filter-show-all');
  return !(showAll && showAll.checked);
}
function bankFilterValues() {
  const typeSel = document.getElementById('bank-filter-type');
  const tagInput = document.getElementById('bank-filter-tag');
  return {
    type: typeSel ? typeSel.value : '',
    tag: tagInput ? tagInput.value.trim().toLowerCase() : '',
  };
}
function sampleMatchesFilter(key) {
  if (!bankFilterActive()) return true;
  const { type, tag } = bankFilterValues();
  if (!type && !tag) return true;
  const meta = getSampleMeta(key);
  if (type && meta.type !== type) return false;
  if (tag && !(meta.tags || []).some(t => t.toLowerCase().includes(tag))) return false;
  return true;
}
function onBankFilterChanged() {
  updateSlotTable();
}
window.onBankFilterChanged = onBankFilterChanged;


// ============================================================================
// Live bank-config save/read
// ============================================================================
async function writeBankToDevice() {
  if (!midiOutput) { setStatus('Connect via Web MIDI first.', true); return; }
  if (slots.length === 0) { setStatus('Connect via Web MIDI first so slot notes are known.', true); return; }

  const slot = parseInt(document.getElementById('bank-slot').value);
  const bankName = document.getElementById('bank-name').value || '';
  const text = buildBankConfigFileText(bankName, bankSlots);
  const bytes = new TextEncoder().encode(text);

  if (bytes.length > SYSEX_BANK_WRITE_MAX_BYTES) {
    setStatus(`Bank config too large (${bytes.length} bytes, max ${SYSEX_BANK_WRITE_MAX_BYTES}) — this shouldn't normally happen.`, true);
    return;
  }

  setStatus(`Saving bank ${slot} to device…`);
  showProgress(0);
  try {
    const chunkSize = SYSEX_BANK_WRITE_RAW_CHUNK_BYTES;
    const totalChunks = Math.max(1, Math.ceil(bytes.length / chunkSize));
    for (let chunkIdx = 0; chunkIdx < totalChunks; chunkIdx++) {
      const offset = chunkIdx * chunkSize;
      const chunk = bytes.slice(offset, offset + chunkSize);
      const isLast = (offset + chunk.length) >= bytes.length;
      const rawLen = chunk.length;

      const payload = [
        slot & 0x7F,
        chunkIdx & 0x7F, (chunkIdx >> 7) & 0x7F,
        rawLen & 0x7F, (rawLen >> 7) & 0x7F,
        isLast ? 1 : 0,
      ];
      for (const b of chunk) {
        payload.push(HEX_CHARS.charCodeAt((b >> 4) & 0xF), HEX_CHARS.charCodeAt(b & 0xF));
      }

      await requestSysexResponse(
        SYSEX_TYPE_SET_BANK_CHUNK_REQ,
        payload,
        [SYSEX_TYPE_ACK],
        MIDI_BANK_CHUNK_TIMEOUT_MS,
        MIDI_MAX_RETRIES
      );
      showProgress((offset + chunk.length) / bytes.length);
    }

    hideProgress();
    bankDirty = false;
    loadedForSlot = slot;
    bankValidity[slot] = true;
    updateBankSlotOptionLabels();
    updateBankActionAvailability();
    updateBankStateBanner();
    setStatus(`✓ Bank ${slot} saved to device — applied immediately, no reboot needed.`, false, true);
  } catch (e) {
    hideProgress();
    setStatus(`Save failed: ${(e && e.message) || e}`, true);
  }
}
window.writeBankToDevice = writeBankToDevice;

async function readFromDevice(confirmFirst = true) {
  const slot = parseInt(document.getElementById('bank-slot').value);

  if (confirmFirst && !confirm(`Overwrite changes in browser with data from device bank ${slot}?`)) {
    return;
  }

  if (!midiOutput) {
    setStatus('Read from Device uses runtime MIDI. Connect via Web MIDI (top of page) first.', true);
    return;
  }
  try {
    await readBankConfigViaMidiRuntime(slot);
    setStatus(`✓ Read bank ${slot} via MIDI runtime`, false, true);
  } catch (e) {
    const msg = (e && e.message) || String(e);
    if (msg.includes('NACK 4')) {
      bankValidity[slot] = false;
      updateBankSlotOptionLabels();
      updateBankActionAvailability();
      setStatus(`Bank ${slot} appears to be empty on the device — nothing to read.`, true);
    } else {
      setStatus(`Runtime MIDI read failed: ${msg}`, true);
    }
  }
}
window.readFromDevice = readFromDevice;

async function rebootToBootloader() {
  // Try via SysEx first (device stays running), fall back to manual instruction.
  if (midiOutput) {
    sendCompulidianSysexFrame(SYSEX_TYPE_REBOOT_REQ, 0, 0, []);
    setStatus('Reboot command sent. Device will appear as a USB drive (RPI-RP2) for firmware updates.');
  } else {
    setStatus('Not connected via MIDI. Hold BOOTSEL and press RESET on the Pico to enter flash mode manually.');
  }
}
window.rebootToBootloader = rebootToBootloader;

// ============================================================================
// Helpers
// ============================================================================
function readCString(uint8, offset, maxLen) {
  let s = '';
  for (let i = 0; i < maxLen; i++) {
    const c = uint8[offset + i];
    if (!c) break;
    s += String.fromCharCode(c);
  }
  return s;
}

function setStatus(msg, isError = false, isOk = false) {
  const el = document.getElementById('status');
  el.textContent = msg;
  el.className   = isError ? 'err' : isOk ? 'ok' : '';
}

function showProgress(pct) {
  document.getElementById('progress-bar-wrap').style.display = 'block';
  document.getElementById('progress-bar').style.width = (pct * 100).toFixed(0) + '%';
}
function hideProgress() {
  document.getElementById('progress-bar-wrap').style.display = 'none';
}

// ============================================================================
// Tab switching
// ============================================================================
function switchTab(name, btn) {
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  document.querySelectorAll('nav button').forEach(b => b.classList.remove('active'));
  document.getElementById(`tab-${name}`).classList.add('active');
  if (btn) btn.classList.add('active');
}
window.switchTab = switchTab;

// ============================================================================
// Config transport — Web MIDI
// ============================================================================
// Transport map (v2 SysEx):
// - Browser sends GET/SET/REBOOT requests with a request ID.
// - Device replies with CONFIG_RESP (and ACK/NACK where applicable).
// - Pending requests are resolved by request ID, with a safe fallback when a
//   single in-flight request receives a remapped response ID.
// - Connection state is maintained by both WebMidi state events and a periodic
//   watchdog that detects silent disconnects.
//
// Maintenance knobs:
// - MIDI_DEBUG: mirrors logs to browser console.
// - MIDI_BIND_ALL_INPUTS: diagnostic mode to listen on all inputs.
// - MIDI_REQUEST_TIMEOUT_MS / MIDI_MAX_RETRIES: request reliability tuning.
// - MIDI_RECONNECT_* and MIDI_WATCHDOG_INTERVAL_MS: reconnect behavior.

const SYSEX_MFR_ID    = 0x7D;
const SYSEX_DEVICE_ID = 0x43;
const SYSEX_PROTO_VER = 0x02;

const SYSEX_TYPE_GET_CONFIG_REQ = 0x10;
const SYSEX_TYPE_SET_CONFIG_REQ = 0x11;
const SYSEX_TYPE_GET_BANK_INFO_REQ = 0x12;
const SYSEX_TYPE_GET_BANK_CHUNK_REQ = 0x13;
const SYSEX_TYPE_GET_SLOTS_REQ  = 0x14;
const SYSEX_TYPE_SET_BANK_CHUNK_REQ = 0x15;
const SYSEX_TYPE_GET_SAMPLES_INFO_REQ  = 0x16;
const SYSEX_TYPE_GET_SAMPLES_CHUNK_REQ = 0x17;
const SYSEX_TYPE_GET_SAMPLE_META_INFO_REQ  = 0x18;
const SYSEX_TYPE_GET_SAMPLE_META_CHUNK_REQ = 0x19;
const SYSEX_TYPE_SET_SAMPLE_META_CHUNK_REQ = 0x1A;
const SYSEX_TYPE_REBOOT_REQ     = 0x20;
const SYSEX_TYPE_CONFIG_RESP    = 0x30;
const SYSEX_TYPE_BANK_INFO_RESP = 0x40;
const SYSEX_TYPE_BANK_CHUNK_RESP = 0x41;
const SYSEX_TYPE_SLOTS_INFO_RESP = 0x42;
const SYSEX_TYPE_SAMPLES_INFO_RESP  = 0x43;
const SYSEX_TYPE_SAMPLES_CHUNK_RESP = 0x44;
const SYSEX_TYPE_SAMPLE_META_INFO_RESP  = 0x45;
const SYSEX_TYPE_SAMPLE_META_CHUNK_RESP = 0x46;
const SYSEX_TYPE_ACK            = 0x31;
const SYSEX_TYPE_NACK           = 0x32;

const SYSEX_STATUS_OK = 0x00;

// Upload (browser -> device) chunk size for SET_BANK_CHUNK_REQ - must match
// include/configurator.h's SYSEX_BANK_WRITE_RAW_CHUNK_BYTES/MAX_BYTES
// exactly (incoming SysEx is capped at 128 bytes total by the bundled MIDI
// library, unlike the much larger download-direction chunks).
const SYSEX_BANK_WRITE_RAW_CHUNK_BYTES = 32;
const SYSEX_BANK_WRITE_MAX_BYTES = 2048;

// Upload chunk size for SET_SAMPLE_META_CHUNK_REQ - must match
// include/configurator.h's SYSEX_META_WRITE_RAW_CHUNK_BYTES/MAX_BYTES.
const SYSEX_META_WRITE_RAW_CHUNK_BYTES = 32;
const SYSEX_META_WRITE_MAX_BYTES = 8192;

const HEX_CHARS = '0123456789abcdef';

// ============================================================================
// Sample store / flash layout constants (must match include/audio/flash_layout.h
// and include/audio/sample_store.h — only relevant to 16 MB boards).
// ============================================================================
const XIP_BASE                     = 0x10000000;
const FLASH_FIRMWARE_SIZE          = 0x001F0000;
const FLASH_SETTINGS_SIZE          = 0x00010000;
const FLASH_BANKS_OFFSET           = FLASH_FIRMWARE_SIZE + FLASH_SETTINGS_SIZE;
const FLASH_SAMPLESTORE_OFFSET     = FLASH_BANKS_OFFSET;
const FLASH_BANK_SIZE_16MB         = 0x00300000;
const FLASH_MAX_BANKS_16MB         = 4;
const FLASH_SAMPLESTORE_SIZE_16MB  = FLASH_BANK_SIZE_16MB * FLASH_MAX_BANKS_16MB;  // 12 MB
const FLASH_SECTOR_SIZE            = 4096;  // RP2040 minimum flash erase granularity

const SAMPLESTORE_MAGIC            = 0x73746F72;  // 'stor'
const SAMPLESTORE_VERSION          = 1;
const MAX_SAMPLESTORE_ENTRIES      = 256;
const SAMPLESTORE_ENTRY_SIZE       = 48;
const SAMPLESTORE_INDEX_SIZE       = 0x00004000;  // 16 KB
const SAMPLESTORE_HEADER_FIXED_SIZE = 32;         // magic+version+num_entries+next_free+reserved[16]
const SAMPLESTORE_ENTRY_FLAG_USED  = 0x01;
const FLASH_SAMPLESTORE_DATA_OFFSET = FLASH_SAMPLESTORE_OFFSET + SAMPLESTORE_INDEX_SIZE;

const UF2_BLOCK_SIZE    = 512;
const UF2_DATA_SIZE     = 256;
const UF2_MAGIC_START0  = 0x0A324655;
const UF2_MAGIC_START1  = 0x9E5D5157;
const UF2_MAGIC_END     = 0x0AB16F30;
const RP2040_FAMILY_ID  = 0xE48BFF56;

const OUTPUT_TYPES   = ['Audio', 'Clock', 'Gate', 'CV'];
const INPUT_ROLES    = ['None', 'BPM', 'Density', 'Pitch', 'Bank'];
const OUTPUT_LABELS  = ['Pulse 1', 'Pulse 2', 'CV 1', 'CV 2'];
const CV_LABELS      = ['CV 1', 'CV 2'];
const KNOB_LABELS    = ['Main', 'X', 'Y'];

let midiOutput  = null;
let midiInput   = null;
let midiInputsBound = [];
let currentCfg  = null;  // raw config payload from last SYSEX_TYPE_CONFIG_RESP

const midiPendingRequests = new Map();
let midiNextRequestId = 1;
let midiReconnectTimer = null;
let midiReconnectAttempt = 0;
let webMidiReady = false;
let midiConnectionWatchdog = null;

const MIDI_REQUEST_TIMEOUT_MS = 1500;
const MIDI_MAX_RETRIES = 2;
const MIDI_BANK_CHUNK_TIMEOUT_MS = 5000;
const MIDI_RECONNECT_MAX_ATTEMPTS = 8;
const MIDI_RECONNECT_BASE_DELAY_MS = 400;
const MIDI_WATCHDOG_INTERVAL_MS = 800;
const MIDI_DEBUG = true;
const MIDI_BIND_ALL_INPUTS = false;
const MIDI_LOG_MAX_LINES = 220;
const MIDI_RUNTIME_TAG = 'midi-runtime-2026-08-02';

// ---------------------------------------------------------------------------
// Logging helpers
// ---------------------------------------------------------------------------
function serializeLogArg(value) {
  if (typeof value === 'string') return value;
  try {
    return JSON.stringify(value);
  } catch (_) {
    return String(value);
  }
}

function appendMidiLogLine(line) {
  const el = document.getElementById('midi-debug-log');
  if (!el) return;
  const lines = (el.textContent || '').split('\n').filter(Boolean);
  lines.push(line);
  if (lines.length > MIDI_LOG_MAX_LINES) {
    lines.splice(0, lines.length - MIDI_LOG_MAX_LINES);
  }
  el.textContent = lines.join('\n');
  el.scrollTop = el.scrollHeight;
}

function clearMidiLog() {
  const el = document.getElementById('midi-debug-log');
  if (el) el.textContent = '';
}
window.clearMidiLog = clearMidiLog;

function midiDebug(...args) {
  const ts = new Date().toISOString().split('T')[1].replace('Z', '');
  const body = args.map(serializeLogArg).join(' ');
  const line = `[${ts}] ${body}`;
  appendMidiLogLine(line);
  if (MIDI_DEBUG) {
    console.log('[compulidian-midi]', ...args);
  }
}

function midiDot(on) {
  document.getElementById('midi-dot').className = 'midi-status' + (on ? ' on' : '');
}

function setConfigStatus(msg, isError = false, isOk = false) {
  // The IO/Other tabs no longer have their own status readouts - everything
  // is unified into the single #status element in the shared status bar.
  setStatus(msg, isError, isOk);
}

function matchesCompulidianName(device) {
  return !!device && !!device.name && device.name.toLowerCase().includes('compulidian');
}

function getCompulidianInputs(inputs) {
  return (inputs || []).filter(matchesCompulidianName);
}

function getCompulidianOutputs(outputs) {
  return (outputs || []).filter(matchesCompulidianName);
}

function getMidiOutputs() {
  return WebMidi.outputs || [];
}

function getMidiInputs() {
  return WebMidi.inputs || [];
}

function clearPendingRequests(errMsg) {
  const err = new Error(errMsg);
  for (const [reqId, pending] of midiPendingRequests.entries()) {
    clearTimeout(pending.timeoutId);
    pending.reject(err);
    midiPendingRequests.delete(reqId);
  }
}

function clearReconnectTimer() {
  if (midiReconnectTimer) {
    clearTimeout(midiReconnectTimer);
    midiReconnectTimer = null;
  }
}

function clearConnectionWatchdog() {
  if (midiConnectionWatchdog) {
    clearInterval(midiConnectionWatchdog);
    midiConnectionWatchdog = null;
  }
}

// ---------------------------------------------------------------------------
// Binding and connection lifecycle
// ---------------------------------------------------------------------------
function clearInputBindings() {
  for (const input of midiInputsBound) {
    try {
      if (input && typeof input.removeListener === 'function') {
        input.removeListener('sysex');
      }
    } catch (e) {
      console.warn('Failed to detach MIDI listener:', e);
    }
  }
  midiInputsBound = [];
}

function resetMidiBinding() {
  clearConnectionWatchdog();
  clearInputBindings();
  midiInput = null;
  midiOutput = null;
  midiDot(false);
  document.getElementById('midi-device-name').textContent = 'Not connected';
}

function bindMidiInputListener(input) {
  if (!input) return;
  input.removeListener('sysex');
  input.addListener('sysex', e => {
    const raw = (e && e.message && e.message.rawData) ? Array.from(e.message.rawData)
      : (e && e.rawData) ? Array.from(e.rawData)
      : (e && e.data) ? Array.from(e.data)
      : [];
    midiDebug('rx raw sysex', raw);
    handleIncomingSysex(raw);
  });

  input.addListener('noteon', e => {
    midiDebug('rx noteon', { note: e.midiNote, name: e.note.name, octave: e.note.octave, velocity: e.velocity, channel: e.channel });
    // find the button that matches this note number, if any, and add a temporary border to indicate it is playing
    const btn = document.querySelector(`.audition-button[data-note="${e.note.number}"]`);
    if (btn) {
      btn.classList.add('playing-slot');
      setTimeout(() => btn.classList.remove('playing-slot'), 2000);  // default 2s timeout to remove the border in case noteoff is missed
    }
  });

  input.addListener('noteoff', e => {
    midiDebug('rx noteoff', { note: e.midiNote, name: e.note.name, octave: e.note.octave, velocity: e.velocity, channel: e.channel });
    // find the button that matches this note number, if any, and remove the temporary border
    const btn = document.querySelector(`.audition-button[data-note="${e.note.number}"]`);
    if (btn) {
      btn.classList.remove('playing-slot');
    }
  });

}

function bindAllMatchingInputs(inputs) {
  clearInputBindings();
  const matches = MIDI_BIND_ALL_INPUTS
    ? (inputs || [])
    : getCompulidianInputs(inputs);
  for (const input of matches) {
    bindMidiInputListener(input);
  }
  midiInputsBound = matches;

  return matches;
}

function onAnyMidiStateChange() {
  const oldInIds = midiInputsBound.map(i => i.id).sort().join('|');
  const oldOutId = midiOutput ? midiOutput.id : null;
  const ports = pickCompulidianPorts();
  if (!ports.input || !ports.output) {
    resetMidiBinding();
    clearPendingRequests('Device disconnected');
    setConfigStatus('Device disconnected. Reconnecting…', true, false);
    scheduleReconnect();
    return;
  }

  const newInputs = ports.inputMatches;
  const newInIds = newInputs.map(i => i.id).sort().join('|');
  const changed = newInIds !== oldInIds || ports.output.id !== oldOutId;
  if (changed) {
    bindAllMatchingInputs(ports.ins);
    midiInput = newInputs.length > 0 ? newInputs[0] : ports.input;
    midiOutput = ports.output;
    midiDot(true);
    document.getElementById('midi-device-name').textContent = midiOutput.name;
    midiDebug('state change rebound', {
      output: { name: midiOutput.name, id: midiOutput.id },
      inputs: midiInputsBound.map(i => ({ name: i.name, id: i.id }))
    });
    setConfigStatus('MIDI device changed. Ready.', false, true);
  }
}

function scheduleReconnect() {
  if (midiReconnectTimer || midiReconnectAttempt >= MIDI_RECONNECT_MAX_ATTEMPTS) {
    if (midiReconnectAttempt >= MIDI_RECONNECT_MAX_ATTEMPTS) {
      setConfigStatus('Reconnect failed. Click Connect via Web MIDI.', true, false);
    }
    return;
  }
  const delay = MIDI_RECONNECT_BASE_DELAY_MS * Math.pow(2, Math.min(midiReconnectAttempt, 4));
  midiReconnectAttempt++;
  midiReconnectTimer = setTimeout(async () => {
    midiReconnectTimer = null;
    try {
      await midiConnect(true);
    } catch (e) {
      console.warn('Reconnect attempt failed:', e);
      scheduleReconnect();
    }
  }, delay);
}

function allocRequestId() {
  for (let i = 0; i < 127; i++) {
    const reqId = midiNextRequestId;
    midiNextRequestId = (midiNextRequestId % 127) + 1;
    if (!midiPendingRequests.has(reqId)) return reqId;
  }
  throw new Error('No free request IDs available');
}

function sendCompulidianSysexFrame(type, reqId = 0, status = 0, payload = []) {
  if (!midiOutput) throw new Error('No MIDI output selected');
  midiDebug('tx sysex', { type, reqId, status, payloadLen: payload.length, payload });
  midiOutput.sendSysex(SYSEX_MFR_ID, [
    SYSEX_DEVICE_ID,
    SYSEX_PROTO_VER,
    type & 0x7F,
    reqId & 0x7F,
    status & 0x7F,
    ...payload
  ]);
}

function decodeU32_7bit(bytes, offset) {
  if (!bytes || bytes.length < offset + 5) throw new Error('u32 decode underrun');
  return ((bytes[offset] & 0x7F)
    | ((bytes[offset + 1] & 0x7F) << 7)
    | ((bytes[offset + 2] & 0x7F) << 14)
    | ((bytes[offset + 3] & 0x7F) << 21)
    | ((bytes[offset + 4] & 0x0F) << 28)) >>> 0;
}

function hexNibble(c) {
  if (c >= 0x30 && c <= 0x39) return c - 0x30; // 0-9
  if (c >= 0x41 && c <= 0x46) return c - 0x41 + 10; // A-F
  if (c >= 0x61 && c <= 0x66) return c - 0x61 + 10; // a-f
  throw new Error(`Invalid hex nibble 0x${c.toString(16)}`);
}

async function readBankConfigViaMidiRuntime(slot) {
  if (!midiOutput) throw new Error('Not connected via MIDI');

  setStatus(`Requesting bank ${slot} info via MIDI…`);
  const info = await requestSysexResponse(
    SYSEX_TYPE_GET_BANK_INFO_REQ,
    [slot & 0x7F],
    [SYSEX_TYPE_BANK_INFO_RESP],
    MIDI_BANK_CHUNK_TIMEOUT_MS,
    MIDI_MAX_RETRIES
  );

  const p = info.payload || [];
  if (p.length < 12) throw new Error(`Short bank info response (${p.length} bytes)`);
  if ((p[0] & 0x7F) !== (slot & 0x7F)) throw new Error('Bank info slot mismatch');

  const totalBytes = decodeU32_7bit(p, 7);
  if (totalBytes === 0) throw new Error(`Invalid bank config size ${totalBytes}`);

  setStatus(`Reading bank ${slot} config over MIDI (${totalBytes} bytes)…`);
  showProgress(0);

  const full = new Uint8Array(totalBytes);
  let offset = 0;
  let chunkIdx = 0;

  while (offset < totalBytes) {
    const idxLo = chunkIdx & 0x7F;
    const idxHi = (chunkIdx >> 7) & 0x7F;
    const resp = await requestSysexResponse(
      SYSEX_TYPE_GET_BANK_CHUNK_REQ,
      [slot & 0x7F, idxLo, idxHi],
      [SYSEX_TYPE_BANK_CHUNK_RESP],
      MIDI_BANK_CHUNK_TIMEOUT_MS,
      MIDI_MAX_RETRIES
    );

    const c = resp.payload || [];
    if (c.length < 6) throw new Error(`Short bank chunk response (${c.length} bytes)`);
    const gotSlot = c[0] & 0x7F;
    const gotIdx  = (c[1] & 0x7F) | ((c[2] & 0x7F) << 7);
    const rawLen  = (c[3] & 0x7F) | ((c[4] & 0x7F) << 7);
    const isLast  = (c[5] & 0x7F) !== 0;
    const hexLen  = rawLen * 2;

    if (gotSlot !== (slot & 0x7F)) throw new Error('Bank chunk slot mismatch');
    if (gotIdx !== chunkIdx) throw new Error(`Bank chunk index mismatch (got ${gotIdx}, expected ${chunkIdx})`);
    if (c.length < 6 + hexLen) throw new Error('Bank chunk hex payload truncated');
    if (offset + rawLen > full.length) throw new Error('Bank chunk exceeds expected bank size');

    for (let i = 0; i < rawLen; i++) {
      const hi = hexNibble(c[6 + i * 2]);
      const lo = hexNibble(c[6 + i * 2 + 1]);
      full[offset + i] = (hi << 4) | lo;
    }

    offset += rawLen;
    chunkIdx++;
    showProgress(offset / totalBytes);

    if (isLast) break;
  }

  showProgress(1);
  hideProgress();

  if (offset < full.length) {
    throw new Error(`Incomplete bank data (${offset}/${full.length} bytes)`);
  }

  const text = new TextDecoder().decode(full);
  const { bankName, slots: parsedSlots } = parseBankConfigFileText(text);

  if (bankName) {
    document.getElementById('bank-name').value = bankName;
  }

  bankSlots = slots.map((_, i) => parsedSlots[i] || emptyBankSlot());
  // Ignore whatever midi_note byte was actually stored on disk - always
  // reflect the device's current slot mapping (see syncBankSlotNotesToDevice).
  syncBankSlotNotesToDevice();
  bankDirty = false;
  loadedForSlot = slot;
  bankValidity[slot] = true;
  updateBankSlotOptionLabels();
  updateBankActionAvailability();
  updateSlotTable();

  const assignedCount = bankSlots.filter(s => s.sampleId).length;
  setStatus(`✓ Loaded "${bankName || 'Bank'}" — ${assignedCount} sample(s) assigned`, false, true);
}

// ---------------------------------------------------------------------------
// Port discovery and watchdog
// ---------------------------------------------------------------------------
function pickCompulidianPorts() {
  const outs = getMidiOutputs();
  const ins = getMidiInputs();
  const outputMatches = getCompulidianOutputs(outs);
  const inputMatches = getCompulidianInputs(ins);
  const output = outputMatches[0] || null;
  const input = inputMatches[0] || null;
  return { output, input, outs, ins, outputMatches, inputMatches };
}

function ensureConnectionWatchdog() {
  if (midiConnectionWatchdog) return;
  midiConnectionWatchdog = setInterval(() => {
    const ports = pickCompulidianPorts();
    const hasDevice = !!ports.output && !!ports.input;
    const isConnected = !!midiOutput && !!midiInput;
    if (!hasDevice && isConnected) {
      midiDebug('watchdog detected disconnect');
      resetMidiBinding();
      clearPendingRequests('Device disconnected');
      setConfigStatus('Device disconnected. Reconnecting…', true, false);
      scheduleReconnect();
    }
  }, MIDI_WATCHDOG_INTERVAL_MS);
}

// ---------------------------------------------------------------------------
// SysEx frame decoding
// ---------------------------------------------------------------------------
function normalizeSysexData(data) {
  if (!data || data.length < 4) return null;
  const bytes = Array.from(data);
  if (bytes[0] === 0xF0) {
    if (bytes[bytes.length - 1] !== 0xF7) return null;
    return bytes.slice(1, bytes.length - 1);
  }
  return bytes;
}

function decodeV2Frame(bytes) {
  if (bytes[0] === SYSEX_MFR_ID && bytes[1] === SYSEX_DEVICE_ID && bytes.length >= 6) {
    return {
      ver: bytes[2],
      msgType: bytes[3],
      reqId: bytes[4] & 0x7F,
      status: bytes[5] & 0x7F,
      payload: bytes.slice(6),
      layout: 'mfr-device-ver'
    };
  }
  if (bytes[0] === SYSEX_DEVICE_ID && bytes.length >= 5) {
    return {
      ver: bytes[1],
      msgType: bytes[2],
      reqId: bytes[3] & 0x7F,
      status: bytes[4] & 0x7F,
      payload: bytes.slice(5),
      layout: 'device-ver'
    };
  }
  if (bytes[0] === SYSEX_PROTO_VER && bytes.length >= 4) {
    return {
      ver: bytes[0],
      msgType: bytes[1],
      reqId: bytes[2] & 0x7F,
      status: bytes[3] & 0x7F,
      payload: bytes.slice(4),
      layout: 'ver-only'
    };
  }
  return null;
}

// ---------------------------------------------------------------------------
// Incoming frame dispatch
// ---------------------------------------------------------------------------
function handleIncomingSysex(rawData) {
  const bytes = normalizeSysexData(rawData);
  if (!bytes) return;

  const frame = decodeV2Frame(bytes);
  if (!frame) {
    midiDebug('rx unrecognized sysex layout', bytes);
    return;
  }
  if (frame.ver !== SYSEX_PROTO_VER) {
    midiDebug('rx wrong protocol version', frame);
    return;
  }

  const { msgType, reqId, status, payload } = frame;
  midiDebug('rx decoded frame', { msgType, reqId, status, payloadLen: payload.length, layout: frame.layout });

  if (msgType === SYSEX_TYPE_CONFIG_RESP) {
    currentCfg = payload;
    applyCfgToUI(currentCfg);
    setConfigStatus('Config loaded from device.', false, true);
  }

  let pending = midiPendingRequests.get(reqId);
  let effectiveReqId = reqId;
  if (!pending && msgType === SYSEX_TYPE_CONFIG_RESP && midiPendingRequests.size === 1) {
    // Some browser/device stacks can remap request IDs across bridges.
    // If only one request is in flight, safely treat config response as that request.
    const first = midiPendingRequests.entries().next().value;
    if (first) {
      effectiveReqId = first[0];
      pending = first[1];
      midiDebug('reqId remap fallback', { incomingReqId: reqId, mappedReqId: effectiveReqId });
    }
  }
  if (!pending) return;

  if (msgType === SYSEX_TYPE_NACK) {
    clearTimeout(pending.timeoutId);
    midiPendingRequests.delete(effectiveReqId);
    pending.reject(new Error('Device NACK ' + status));
    return;
  }

  const expected = pending.expectedTypes.includes(msgType);
  if (!expected) return;

  clearTimeout(pending.timeoutId);
  midiPendingRequests.delete(effectiveReqId);

  pending.resolve({ type: msgType, status, payload });
}

// ---------------------------------------------------------------------------
// Transport bootstrap and request/response API
// ---------------------------------------------------------------------------
async function ensureMidiEnabled() {
  if (typeof WebMidi === 'undefined' || typeof WebMidi.enable !== 'function') {
    throw new Error('webmidi.js failed to load');
  }
  if (!webMidiReady) {
    await WebMidi.enable({ sysex: true });
    midiDebug('WebMidi enabled', {
      inputs: (WebMidi.inputs || []).map(d => d.name),
      outputs: (WebMidi.outputs || []).map(d => d.name)
    });
    WebMidi.addListener('connected', onAnyMidiStateChange);
    WebMidi.addListener('disconnected', onAnyMidiStateChange);
    webMidiReady = true;
  }
}

function requestSysexResponse(type, payload = [], expectedTypes = [SYSEX_TYPE_CONFIG_RESP], timeoutMs = MIDI_REQUEST_TIMEOUT_MS, retries = MIDI_MAX_RETRIES) {
  return new Promise((resolve, reject) => {
    const reqId = allocRequestId();
    const trySend = (attempt) => {
      const timeoutId = setTimeout(() => {
        midiPendingRequests.delete(reqId);
        midiDebug('request timeout', { type, reqId, attempt, retries });
        if (attempt < retries) {
          trySend(attempt + 1);
        } else {
          reject(new Error('Timed out waiting for MIDI SysEx response'));
        }
      }, timeoutMs);

      midiPendingRequests.set(reqId, { expectedTypes, resolve, reject, timeoutId });

      try {
        sendCompulidianSysexFrame(type, reqId, SYSEX_STATUS_OK, payload);
      } catch (e) {
        clearTimeout(timeoutId);
        midiPendingRequests.delete(reqId);
        reject(e);
      }
    };

    trySend(0);
  });
}

async function midiConnect(isReconnectAttempt = false) {
  clearReconnectTimer();
  try {
    await ensureMidiEnabled();
  } catch(e) {
    setConfigStatus('MIDI access failed: ' + e.message, true, false);
    return;
  }

  const ports = pickCompulidianPorts();
  midiDebug('connect ports', {
    outputs: ports.outs.map(o => ({ name: o.name, id: o.id })),
    inputs: ports.ins.map(i => ({ name: i.name, id: i.id }))
  });
  midiOutput = ports.output;
  const inputMatches = ports.inputMatches;
  midiInput = inputMatches.length > 0 ? inputMatches[0] : ports.input;

  if (!midiOutput || !midiInput) {
    const outs = ports.outs.map(o => `"${o.name}"`).join(', ');
    const ins  = ports.ins.map(i => `"${i.name}"`).join(', ');
    setConfigStatus(
      `Compulidian not found. Available outputs: [${outs||'none'}]  inputs: [${ins||'none'}].`,
      true,
      false
    );
    if (!isReconnectAttempt) {
      scheduleReconnect();
    }
    return;
  }

  bindAllMatchingInputs(ports.ins);
  midiInput = midiInputsBound.length > 0 ? midiInputsBound[0] : midiInput;
  midiDot(true);
  ensureConnectionWatchdog();
  midiReconnectAttempt = 0;
  document.getElementById('midi-device-name').textContent = midiOutput.name;
  midiDebug('selected ports', {
    output: { name: midiOutput.name, id: midiOutput.id },
    inputs: midiInputsBound.map(i => ({ name: i.name, id: i.id }))
  });
  setConfigStatus('Connected. Requesting config…', false, false);

  try {
    await readConfig();
  } catch (e) {
    setConfigStatus('Connected, but config read failed: ' + e.message, true, false);
  }

  try {
    await fetchDeviceSlots();
    setStatus(`✓ Loaded ${slots.length} slot(s) from device.`, false, true);
  } catch (e) {
    setStatus('Failed to read device slots: ' + e.message, true);
  }

  try {
    await fetchSampleList();
    const hint = document.getElementById('samples-empty-hint');
    if (hint) hint.style.display = sampleList.length ? 'none' : 'block';
    updateSlotTable();
    renderDeviceSamplesList();
  } catch (e) {
    midiDebug('sample list fetch failed', e && e.message);
  }

  try {
    await refreshBankValidity();
  } catch (e) {
    midiDebug('bank validity refresh failed', e && e.message);
  }
}
window.midiConnect = midiConnect;

// Fetches the active output processor's slots generically (name/note/channel
// per position) - the web tool never hardcodes which MIDIOutputProcessor
// subclass is compiled into the firmware.
async function fetchDeviceSlots() {
  if (!midiOutput) throw new Error('Not connected via MIDI');
  const resp = await requestSysexResponse(
    SYSEX_TYPE_GET_SLOTS_REQ,
    [],
    [SYSEX_TYPE_SLOTS_INFO_RESP]
  );
  const p = resp.payload || [];
  if (p.length < 1) throw new Error('Empty slots response');

  const count = p[0] & 0x7F;
  const dec = new TextDecoder();
  const parsedSlots = [];
  let pos = 1;
  for (let i = 0; i < count; i++) {
    if (pos + 3 > p.length) throw new Error('Truncated slots response');
    const note = p[pos] & 0x7F;
    const channel = p[pos + 1] & 0x7F;
    const labelLen = p[pos + 2] & 0x7F;
    pos += 3;
    if (pos + labelLen > p.length) throw new Error('Truncated slot label');
    const label = dec.decode(Uint8Array.from(p.slice(pos, pos + labelLen))) || `Slot ${i + 1}`;
    pos += labelLen;
    parsedSlots.push({ note: note > 0x7E ? -1 : note, channel, label });
  }

  const sameLength = parsedSlots.length === slots.length;
  slots = parsedSlots;
  if (!sameLength) resetBankSlotsForCurrentSlots();
  else syncBankSlotNotesToDevice();
  updateSlotsGateUI();
  updateSlotTable();
}


async function readConfig() {
  if (!midiOutput) {
    setConfigStatus('Not connected.', true, false);
    throw new Error('Not connected');
  }
  setConfigStatus('Requesting config…', false, false);
  await requestSysexResponse(
    SYSEX_TYPE_GET_CONFIG_REQ,
    [],
    [SYSEX_TYPE_CONFIG_RESP]
  );
}
window.readConfig = () => readConfig().catch(e => {
  setConfigStatus('Config read failed: ' + e.message, true, false);
  console.warn('readConfig failed:', e);
});

async function saveConfig() {
  if (!midiOutput) {
    setConfigStatus('Not connected.', true, false);
    throw new Error('Not connected');
  }
  const d = buildCfgFromUI();
  d.push(1); // persist=1 -> write to flash so it survives a reboot
  setConfigStatus('Config sent. Waiting for confirmation…', false, false);
  try {
    await requestSysexResponse(
      SYSEX_TYPE_SET_CONFIG_REQ,
      d,
      [SYSEX_TYPE_CONFIG_RESP]
    );
    setConfigStatus('Config saved and confirmed by device.', false, true);
  } catch (e) {
    setConfigStatus('Config send failed: ' + e.message, true, false);
    throw e;
  }
}
window.saveConfig = () => saveConfig().catch(e => {
  console.warn('saveConfig failed:', e);
});

// Applies the current UI settings to the device immediately without writing
// to flash - lets the user "try out" an active-bank change or output/CV/knob
// role tweak without wearing the flash's limited rewrite lifetime. An
// explicit "Save to Device" is still required to persist the change.
async function applyConfigLive() {
  updateBankStateBanner();
  if (!midiOutput) return;
  const d = buildCfgFromUI();
  d.push(0); // persist=0 -> apply only, do not write to flash
  try {
    await requestSysexResponse(
      SYSEX_TYPE_SET_CONFIG_REQ,
      d,
      [SYSEX_TYPE_CONFIG_RESP]
    );
    setConfigStatus('Applied (not saved to flash — click 💾 Save to Device to make it permanent).', false, true);
  } catch (e) {
    setConfigStatus('Live apply failed: ' + e.message, true, false);
  }
}
window.applyConfigLive = applyConfigLive;

// ============================================================================
// Config UI rendering
// ============================================================================
function selectHtml(id, opts, val, onchange = '') {
  const optsHtml = opts.map((o, i) => `<option value="${i}"${i===val?' selected':''}>${o}</option>`).join('');
  const oc = onchange ? ` onchange="${onchange}"` : '';
  return `<select id="${id}"${oc} style="width:100%">${optsHtml}</select>`;
}

function renderOutputs() {
  const cont = document.getElementById('cfg-outputs');
  cont.innerHTML = '';
  for (let i = 0; i < 4; i++) {
    const card = document.createElement('div');
    card.className = 'card';
    card.innerHTML = `
      <h3>${OUTPUT_LABELS[i]}</h3>
      <label>Type</label>${selectHtml(`out-type-${i}`, OUTPUT_TYPES, 0, 'applyConfigLive()')}
      <label style="margin-top:.5rem">Channel / Voice</label>
      <input type="number" id="out-ch-${i}" min="0" max="16" value="1" style="width:100%" onchange="applyConfigLive()">`;
    cont.appendChild(card);
  }
}

function renderCVInputs() {
  const cont = document.getElementById('cfg-cv-inputs');
  cont.innerHTML = '';
  for (let i = 0; i < 2; i++) {
    const card = document.createElement('div');
    card.className = 'card';
    card.innerHTML = `<h3>${CV_LABELS[i]}</h3><label>Role</label>${selectHtml(`cv-role-${i}`, INPUT_ROLES, 0, 'applyConfigLive()')}`;
    cont.appendChild(card);
  }
}

function renderKnobs() {
  const cont = document.getElementById('cfg-knobs');
  cont.innerHTML = '';
  for (let i = 0; i < 3; i++) {
    const card = document.createElement('div');
    card.className = 'card';
    card.innerHTML = `<h3>${KNOB_LABELS[i]} Knob</h3><label>Role</label>${selectHtml(`knob-role-${i}`, INPUT_ROLES, 0, 'applyConfigLive()')}`;
    cont.appendChild(card);
  }
}

function applyCfgToUI(d) {
  // d = [active_bank, num_valid_banks, out0.type, out0.ch, out0.note, ..., cv0, cv1, k0, k1, k2,
  //      bankName1[32], bankName2[32], bankName3[32], bankName4[32]]
  if (d.length < 19) return;
  document.getElementById('cfg-active-bank').value = d[0];
  document.getElementById('cfg-banks-available').textContent =
    `${d[1]} bank(s) available`;
  for (let i = 0; i < 4; i++) {
    document.getElementById(`out-type-${i}`).value = d[2 + i * 3];
    document.getElementById(`out-ch-${i}`).value   = d[2 + i * 3 + 1];
  }
  document.getElementById('cv-role-0').value   = d[14];
  document.getElementById('cv-role-1').value   = d[15];
  document.getElementById('knob-role-0').value = d[16];
  document.getElementById('knob-role-1').value = d[17];
  document.getElementById('knob-role-2').value = d[18];
  // Bank names appended after settings data (32 bytes each, ASCII)
  const nameBase = 19;
  const dec = new TextDecoder();
  for (let slot = 1; slot <= 4; slot++) {
    const start = nameBase + (slot - 1) * 32;
    if (d.length < start + 1) break;
    const slice = d.slice(start, start + 32);
    const name  = dec.decode(Uint8Array.from(slice)).replace(/\0/g, '').trim();
    const opt   = document.querySelector(`#cfg-active-bank option[value="${slot}"]`);
    if (opt) opt.textContent = name ? `${slot} \u2014 ${name}` : `${slot} \u2014 Flash bank ${slot}`;
  }
  updateBankStateBanner();
}

function buildCfgFromUI() {
  const d = new Array(19).fill(0);
  d[0] = parseInt(document.getElementById('cfg-active-bank').value);
  d[1] = 0; // num_valid_banks — read-only, device ignores it
  for (let i = 0; i < 4; i++) {
    d[2 + i * 3]     = parseInt(document.getElementById(`out-type-${i}`).value);
    d[2 + i * 3 + 1] = parseInt(document.getElementById(`out-ch-${i}`).value);
    d[2 + i * 3 + 2] = 0; // note (not editable in basic UI)
  }
  d[14] = parseInt(document.getElementById('cv-role-0').value);
  d[15] = parseInt(document.getElementById('cv-role-1').value);
  d[16] = parseInt(document.getElementById('knob-role-0').value);
  d[17] = parseInt(document.getElementById('knob-role-1').value);
  d[18] = parseInt(document.getElementById('knob-role-2').value);
  return d;
}

// Initial render
renderOutputs();
renderCVInputs();
renderKnobs();

const midiRuntimeTagEl = document.getElementById('midi-runtime-tag');
if (midiRuntimeTagEl) {
  midiRuntimeTagEl.textContent = `Runtime ${MIDI_RUNTIME_TAG}`;
}
midiDebug('runtime', MIDI_RUNTIME_TAG);
midiDebug('page loaded', new Date().toISOString());

// Show warning when opened as file:// (ES modules and WebUSB/MIDI won't work)
if (location.protocol === 'file:') {
  const w = document.getElementById('server-warning');
  w.style.display = 'block';
  w.innerHTML += `<br><br><strong>Quick fix:</strong> open a terminal in the
    <code>tools/web/</code> folder and run:<br>
    <code>python3 -m http.server 8080</code><br>
    then open <code>http://localhost:8080/compulidian_manager.html</code>`;
}
