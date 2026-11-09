"""Run with python3 test/test_web_bank_management.py; requires Node.js and a native C++ compiler."""

import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_output_batching import extract_block


CPP_HARNESS = r"""
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#define ENABLE_LITTLEFS
constexpr unsigned SYSEX_MAX_SLOTS_REPORTED = 32;
constexpr unsigned SYSEX_SLOT_LABEL_MAX = 23;
constexpr unsigned BANK_MANAGER_MAX_VOICE_SLOTS = 32;
constexpr unsigned BANK_CONFIG_MAX_SLOTS = 32;
constexpr uint8_t BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX = 1;
constexpr char BANK_CONFIG_SAVE_PATH_FMT[] = "/save/bank_%d.txt";
constexpr unsigned SYSEX_SLOTS_MAX_PAYLOAD = 866;
constexpr uint8_t SYSEX_NACK_NOT_READY = 5;
constexpr uint8_t SYSEX_TYPE_SLOTS_INFO_RESP = 0x42;
constexpr uint8_t SYSEX_STATUS_OK = 0;
constexpr int NUM_VOICES = 4;
constexpr int NUM_SAMPLES = 4;
struct SampleDataFlash {
  int identity = 0;
  int size() const { return 10; }
};
SampleDataFlash compiled_data[7];
SampleDataFlash* sample_data[NUM_SAMPLES] = {};
struct Voice {
  int sample;
  uint32_t sampleindex = 0;
} voice[NUM_VOICES] = {{0}, {1}, {2}, {3}};
struct Sample {
  uint8_t MIDINOTE = 0;
  uint8_t play_volume = 0;
  char sname[24] = {};
} sample[NUM_SAMPLES];
@SLOT_CONFIG@;
struct BankConfigHost { SlotConfig slots[BANK_CONFIG_MAX_SLOTS] = {}; };
bool load_bank_config_file_(BankConfigHost&, const char*) { return true; }
struct SampleStoreEntryHeader { char name[24] = "Stored"; } stored_entry;
struct Store {
  bool is_valid() { return true; }
  const SampleStoreEntryHeader* find_by_hash(uint32_t hash) {
    return hash == 100 ? &stored_entry : nullptr;
  }
  bool get_sample_data(uint32_t hash, SampleDataFlash* target) {
    if (hash != 100) return false;
    target->identity = 100;
    return true;
  }
} sampleStore;
struct Console {
  explicit operator bool() const { return false; }
  template <typename... Args> void printf(const char*, Args...) {}
} Serial;
struct Settings { uint8_t active_bank = 1; } persisted_settings;
unsigned settings_write_count = 0;
unsigned delay_count = 0;
Settings settings_load() { return persisted_settings; }
void settings_save(const Settings* settings) {
  persisted_settings = *settings;
  ++settings_write_count;
}
void delay(unsigned) { ++delay_count; }
void delayMicroseconds(unsigned) {}
struct SamplePlayer {
  unsigned begin_count = 0;
  unsigned finish_count = 0;
  void begin_bank_transition() { ++begin_count; }
  bool bank_transition_is_silent() const { return true; }
  void finish_bank_transition() { ++finish_count; }
} sw;
struct BaseOutput {
    const char* label;
    int note;
    int get_note_number() { return note; }
    int get_channel() { return 10; }
};
template <typename Item> struct GenericList {
    std::vector<Item> items;
    size_t size() const { return items.size(); }
    Item get(unsigned index) { return items.at(index); }
};
BaseOutput kick{"Kick", 36}, clap{"Clap", 39}, crash{"Cymbal 1", 49}, ohh{"OHH", 46};
struct Processor {
    GenericList<BaseOutput*> nodes{{&kick, &clap, &crash, &ohh}};
    GenericList<BaseOutput*>* get_available_outputs() { return &nodes; }
} processor;
Processor* output_processor = &processor;
struct BankManager {
  static constexpr int MAX_FLASH_BANKS = 4;
    size_t compiled_num_voices_ = 7;
    uint8_t compiled_midinote_[7] = {36, 37, 40, 49, 42, 46, 75};
  uint8_t compiled_volume_[7] = {127, 127, 127, 127, 127, 127, 127};
  SampleDataFlash* compiled_cache_[7] = {};
  SampleDataFlash flash_pool_[BANK_MANAGER_MAX_VOICE_SLOTS];
  int flash_pool_count_ = 0;
  BankConfigHost bank_config_;
  bool bank_valid_[MAX_FLASH_BANKS + 1] = {true, true, true, true, true};
  int active_bank_ = 1;
    @COUNT@
    @NOTE@
  bool load_bank_config_(int slot);
  void release_flash_pool_();
  void detect_single_bank_(int) {}
  void refresh_bank(int slot);
  bool switch_bank(int slot, bool persist = true);
  void silence_all_voices_();
  void restore_compiled_bank_() { assert(false); }
} bankManager;
@LOADER@
@RELEASE_POOL@
@SWITCH@
@REFRESH@
@SILENCE@
std::vector<uint8_t> sent;
void send_nack(uint8_t, uint8_t) { assert(false); }
void send_sysex_frame(uint8_t type, uint8_t, uint8_t, const uint8_t* payload, uint16_t size) {
    assert(type == SYSEX_TYPE_SLOTS_INFO_RESP);
    sent.assign(payload, payload + size);
}
@SENDER@
int main() {
  for (size_t index = 0; index < 7; ++index) {
    bankManager.compiled_cache_[index] = &compiled_data[index];
  }
  bankManager.bank_config_.slots[0] = {4, 127, 36, BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX};
  bankManager.bank_config_.slots[1] = {6, 96, 39, BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX};
  assert(bankManager.load_bank_config_(1));
  assert(bankManager.flash_pool_count_ == 0);
  assert(sample_data[0] == &compiled_data[3]);
  assert(sample_data[1] == &compiled_data[5]);
  assert(sample[0].MIDINOTE == 36 && sample[1].MIDINOTE == 39);
  assert(sample[1].play_volume == 96);
  assert(sample_data[2] == nullptr);
  bankManager.bank_config_ = {};
  assert(!bankManager.load_bank_config_(1));
  bankManager.bank_config_.slots[0] = {999, 127, 36, 0};
  assert(bankManager.load_bank_config_(1));
  assert(sample_data[0] == &compiled_data[0]);
  bankManager.bank_config_.slots[0] = {999, 127, 36, BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX};
  assert(!bankManager.load_bank_config_(1));
  bankManager.bank_config_.slots[0] = {100, 127, 36, 0};
  assert(bankManager.load_bank_config_(1));
  assert(bankManager.flash_pool_count_ == 1);
  assert(sample_data[0]->identity == 100);
  bankManager.bank_config_.slots[0] = {4, 127, 36, BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX};
  assert(bankManager.load_bank_config_(1));
  assert(bankManager.flash_pool_count_ == 0);
  assert(sample_data[0] == &compiled_data[3]);
  voice[0].sampleindex = 1234;
  const Sample previous_sample = sample[0];
  const unsigned previous_delay_count = delay_count;
  const unsigned previous_transition_count = sw.begin_count;
  for (uint32_t rejected_id : {0u, 999u}) {
    bankManager.bank_config_ = {};
    bankManager.bank_config_.slots[0] = {rejected_id, 127, 36, BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX};
    assert(!bankManager.switch_bank(2));
    assert(bankManager.active_bank_ == 1);
    assert(sample_data[0] == &compiled_data[3]);
    assert(voice[0].sampleindex == 1234);
    assert(sample[0].MIDINOTE == previous_sample.MIDINOTE);
    assert(sample[0].play_volume == previous_sample.play_volume);
    assert(settings_write_count == 0);
    assert(delay_count == previous_delay_count);
  }
  assert(sw.begin_count == previous_transition_count + 2);
  assert(sw.finish_count == sw.begin_count);
  bankManager.bank_config_.slots[0] = {100, 127, 36, 0};
  assert(bankManager.load_bank_config_(1));
  SampleDataFlash* const previous_flash_data = sample_data[0];
  voice[0].sampleindex = 2048;
  bankManager.bank_config_ = {};
  assert(!bankManager.switch_bank(2));
  assert(sample_data[0] == previous_flash_data);
  assert(bankManager.flash_pool_count_ == 1);
  assert(voice[0].sampleindex == 2048);
  assert(bankManager.active_bank_ == 1);
  assert(settings_write_count == 0);
  bankManager.bank_config_.slots[0] = {4, 127, 36, BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX};
  assert(bankManager.switch_bank(3));
  assert(bankManager.active_bank_ == 3);
  assert(sample_data[0] == &compiled_data[3]);
  assert(settings_write_count == 1);
  assert(sw.finish_count == sw.begin_count);
  assert(persisted_settings.active_bank == 3);
  const unsigned before_refresh = sw.begin_count;
  bankManager.refresh_bank(2);
  assert(sw.begin_count == before_refresh);
  bankManager.refresh_bank(3);
  assert(sw.begin_count == before_refresh + 1);
  assert(sw.finish_count == sw.begin_count);
  bankManager.bank_valid_[3] = false;
  bankManager.refresh_bank(3);
  assert(sw.begin_count == before_refresh + 2);
  assert(sw.finish_count == sw.begin_count);
  assert(voice[0].sampleindex == 0xFFFFFFFFu);
  bankManager.bank_valid_[3] = true;
    assert(bankManager.compiled_voice_note(7) == -1);
    send_slots_info_sysex(1);
    std::cout << '[';
    for (size_t index = 0; index < sent.size(); ++index) {
        if (index) std::cout << ',';
        std::cout << unsigned(sent[index]);
    }
    std::cout << ']';
}
"""


AUDIO_HARNESS = r"""
#include <cassert>
#include <cstdint>
#define __not_in_flash_func(name) name
#define IN_AUDIO_WORKER 1
#define IN_MAIN_LOOP 2
#define CALCULATE_SAMPLES_MODE IN_MAIN_LOOP
#define DEFAULT_INTERPOLATION_ENABLED false
constexpr int NUM_VOICES = 1;
struct Voice { int sample = 0; uint32_t sampleindex = 0; } voice[1];
struct Sample { int play_volume = 127; } sample[1];
struct SampleData {
  uint32_t size() const { return 1000; }
  int16_t get_sample(uint32_t) const { return 1000; }
} data;
SampleData* sample_data[1] = {&data};
struct ComputerCard {
  int last_output = 0;
  virtual void CalculateSamples() = 0;
  virtual void AudioWorkerLoop() = 0;
  void AudioOut1(int value) { last_output = value; }
};
@PLAYER@;
int main() {
  SamplePlayer player;
  player.buffer[0][0] = 123;
  player.ProcessSample();
  assert(player.last_output == 123);
  assert(player.audio_callback_count == 1);
  player.read_buffer_id = 1;
  player.CalculateSamples();
  assert(player.audio_buffer_count == 1);
  assert(player.buffer[0][0] != 0);
  player.CalculateSamples();
  assert(player.audio_buffer_count == 1);
  for (int index = 0; index < 127; ++index) player.ProcessSample();
  assert(player.audio_callback_count == 128);
  assert(player.read_buffer_id == 0);
  player.CalculateSamples();
  assert(player.audio_buffer_count == 2);
  sample_data[0] = nullptr;
  player.read_buffer_id = 1;
  player.CalculateSamples();
  assert(player.audio_buffer_count == 3);
  assert(player.buffer[0][0] == 0);

  SamplePlayer transitionPlayer;
  for (int index = 0; index < SamplePlayer::BUFFER_SIZE; ++index)
    transitionPlayer.buffer[0][index] = 16000;
  transitionPlayer.begin_bank_transition();
  assert(!transitionPlayer.bank_transition_is_silent());
  for (int index = 0; index < SamplePlayer::BANK_TRANSITION_FADE_SAMPLES; ++index)
    transitionPlayer.ProcessSample();
  assert(transitionPlayer.last_output == 0);
  assert(transitionPlayer.bank_transition_is_silent());
  transitionPlayer.begin_bank_transition();
  assert(transitionPlayer.bank_transition_is_silent());
  assert(transitionPlayer.output_gain_q15 == 0);
  transitionPlayer.finish_bank_transition();
  sample_data[0] = &data;
  transitionPlayer.read_buffer_id = 1;
  transitionPlayer.CalculateSamples();
  assert(transitionPlayer.bank_transition_state == SamplePlayer::BANK_TRANSITION_BUFFER_READY);
  for (int index = 0; index < SamplePlayer::BUFFER_SIZE; ++index)
    transitionPlayer.ProcessSample();
  assert(transitionPlayer.last_output == 0);
  assert(transitionPlayer.read_buffer_id == 0);
  transitionPlayer.ProcessSample();
  assert(transitionPlayer.bank_transition_state == SamplePlayer::BANK_TRANSITION_FADING_IN);
  for (int index = 1; index < SamplePlayer::BANK_TRANSITION_FADE_SAMPLES; ++index)
    transitionPlayer.ProcessSample();
  assert(transitionPlayer.bank_transition_state == SamplePlayer::BANK_TRANSITION_IDLE);
  assert(transitionPlayer.output_gain_q15 == SamplePlayer::OUTPUT_GAIN_ONE_Q15);
  assert(transitionPlayer.last_output > 0);

  SamplePlayer repeatedTransitionPlayer;
  repeatedTransitionPlayer.begin_bank_transition();
  for (int index = 0; index < 32; ++index) repeatedTransitionPlayer.ProcessSample();
  const uint32_t gain_before_restart = repeatedTransitionPlayer.output_gain_q15;
  repeatedTransitionPlayer.begin_bank_transition();
  assert(repeatedTransitionPlayer.bank_transition_start_gain_q15 == gain_before_restart);
  uint32_t previous_gain = gain_before_restart;
  for (int index = 0; index < SamplePlayer::BANK_TRANSITION_FADE_SAMPLES; ++index) {
    repeatedTransitionPlayer.ProcessSample();
    assert(repeatedTransitionPlayer.output_gain_q15 <= previous_gain);
    previous_gain = repeatedTransitionPlayer.output_gain_q15;
  }
  assert(repeatedTransitionPlayer.bank_transition_is_silent());
}
"""


JS_HARNESS = r"""
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(process.argv[1], 'utf8');
const payload = JSON.parse(fs.readFileSync(0, 'utf8'));
function functionSource(anchor) {
  const start = source.indexOf(anchor);
  assert.notEqual(start, -1, anchor);
  const end = source.indexOf('\n}', start);
  return source.slice(start, end + 2);
}
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    value: '0', textContent: '', style: {}, dataset: {}, options: [],
    appendChild(child) { this.options.push(child); },
    setAttribute(name, value) { this[name] = value; },
    classList: { contains() { return true; } },
  });
  return elements.get(id);
}
const context = {
  TextDecoder, TextEncoder, Uint8Array, DataView, window: {},
  midiOutput: {}, currentCfg: [2, 3, ...new Array(17).fill(0)],
  SYSEX_TYPE_GET_SLOTS_REQ: 20, SYSEX_TYPE_SLOTS_INFO_RESP: 66,
  SYSEX_TYPE_SET_CONFIG_REQ: 17, SYSEX_TYPE_CONFIG_RESP: 48,
  SYSEX_TYPE_SET_BANK_CHUNK_REQ: 25,
  SYSEX_TYPE_GET_CLOCK_STATUS_REQ: 27, SYSEX_TYPE_CLOCK_STATUS_RESP: 71,
  midiPendingRequests: new Map(), clockStatusBusy: false, clockStatusEpoch: 0,
  clockStatusLatest: null, clockRecording: false, clockTrace: [],
  setTimeout() {},
  slots: [], compiledSamples: [], bankSlots: [], sampleList: [],
  bankDirty: false, loadedForSlot: 1,
  bankBusy: false,
  bankValidity: {}, bankNames: {}, settingsBusy: false, settingsDirty: false,
  sampleMeta: {}, sampleMetaDirty: false, sampleMetaBusy: false, sampleMetaRevision: 0,
  SAMPLE_TYPES: ['Unset', 'Kick'],
  updateSettingsState() {}, updateBankSlotOptionLabels() {},
  confirm: () => false,
  updateBankActionAvailability() {}, hideProgress() {},
  showProgress() {}, sleep: async () => {},
  setStatus(message) { context.status = message; },
  document: {
    getElementById: element, querySelector() { return null; },
    createElement() { return {}; },
  },
  requestSysexResponse: async () => ({ payload }),
  updateSlotTable() {}, updateSlotsGateUI() {},
  sampleMetaKeyForCompiled: label => label, sampleMatchesFilter: () => true,
  markBankDirty() {}, setConfigStatus(message) { context.status = message; },
  buildCfgFromUI: () => [Number(element('cfg-active-bank').value)],
};
const startupContext = vm.createContext({ ...context });
const startupEnd = source.indexOf('\nupdateSlotOptions();') + '\nupdateSlotOptions();'.length;
vm.runInContext(source.slice(0, startupEnd), startupContext);
vm.createContext(context);
vm.runInContext(source.match(/const CLOCK_STATUS_FIELDS = \[[\s\S]*?\];/)[0], context);
vm.runInContext(source.match(/^const CLOCK_TRACE_LIMIT =.*;$/m)[0], context);
vm.runInContext(source.slice(0, source.indexOf('// Slot state')), context);
vm.runInContext(source.match(/^const (?:SYSEX_BANK_WRITE_MAX_BYTES|SYSEX_BANK_WRITE_RAW_CHUNK_BYTES|SYSEX_META_WRITE_MAX_BYTES|SYSEX_META_WRITE_RAW_CHUNK_BYTES|HEX_CHARS|MIDI_BANK_CHUNK_TIMEOUT_MS|MIDI_MAX_RETRIES|SYSEX_TYPE_(?:ACK|SET_SAMPLE_META_CHUNK_REQ))\s*=.*;$/gm).join('\n'), context);
for (const anchor of [
  'function emptyBankSlot()', 'function syncBankSlotNotesToDevice()',
  'function resetBankSlotsForCurrentSlots()', 'function sampleOptionsHtml(',
  'function onSampleSelectChanged(', 'async function fetchDeviceSlots()',
  'function updateBankStateBanner()', 'function applyCfgToUI(',
  'function updateBankActionAvailability()',
  'function buildCfgFromUI()',
  'function extendedSettingIds()', 'function buildSettingsRequest(',
  'function decodeClockStatus(', 'function updateClockTraceControls()',
  'function renderClockStatus(', 'async function pollClockStatus()',
  'function toggleClockRecording()', 'function clearClockTrace()', 'function downloadClockTrace()',
  'async function onBankSlotChanged()',
  'async function writeBankToDevice(',
  'function setSampleMetaTags(', 'function buildSampleMetaText(',
  'function updateSampleActions()', 'async function writeSampleMetaToDevice()',
  'async function fetchSampleMetaFromDevice()',
  'async function applyConfigLive()', 'async function saveConfig()',
]) vm.runInContext(functionSource(anchor), context);

(async () => {
  await context.fetchDeviceSlots();
  assert.equal(context.compiledSamples.length, 7);
  const options = context.sampleOptionsHtml(3, true);
  assert.match(options, /value="c:3" selected>Compiled: Cymbal 1/);
  assert.match(options, /value="c:5">Compiled: OHH/);
  assert.match(options, /value="c:6">Compiled: Claves/);
  context.onSampleSelectChanged(0, 'c:3');
  context.onSampleSelectChanged(1, 'c:5');
  const saved = context.parseBankConfigFileText(context.buildBankConfigFileText('Test', context.bankSlots));
  assert.equal(saved.slots[0].sampleId, 3);
  assert.equal(saved.slots[1].sampleId, 5);
  assert.equal(saved.slots[0].isCompiled, true);
  assert.equal(saved.slots[1].isCompiled, true);
  await context.fetchDeviceSlots();
  assert.equal(context.bankSlots[1].sampleId, 5);
  context.requestSysexResponse = async () => ({ payload: payload.slice(0, -1) });
  await assert.rejects(context.fetchDeviceSlots(), /Truncated compiled sample catalog/);
  context.requestSysexResponse = async () => ({ payload: payload.slice(0, -8) });
  await assert.rejects(context.fetchDeviceSlots(), /update device firmware/);
  context.requestSysexResponse = async () => { throw new Error('Device NACK 4'); };
  element('bank-slot').value = '1';
  element('cfg-active-bank').value = '3';
  context.updateBankStateBanner();
  assert.match(element('bank-active-indicator').textContent, /playing bank 2/);
  await context.applyConfigLive();
  assert.equal(element('cfg-active-bank').value, 2);
  assert.match(element('bank-active-indicator').textContent, /playing bank 2/);
  assert.match(context.status, /NACK 4/);
  element('cfg-active-bank').value = '3';
  await assert.rejects(context.saveConfig(), /NACK 4/);
  assert.equal(element('cfg-active-bank').value, 2);
  assert.match(element('bank-active-indicator').textContent, /playing bank 2/);
  context.requestSysexResponse = async () => {
    context.currentCfg[0] = 1;
    context.applyCfgToUI(context.currentCfg);
  };
  element('cfg-active-bank').value = '1';
  await context.applyConfigLive();
  assert.equal(element('cfg-active-bank').value, 1);
  assert.match(element('bank-active-indicator').textContent, /currently active bank/);
  context.currentCfg = [...context.currentCfg, ...new Array(128).fill(0), 1];
  context.applyCfgToUI(context.currentCfg);
  assert.equal(element('cfg-momentary-role').disabled, false);
  assert.equal(element('cfg-momentary-role').value, 1);
  let configSent;
  context.requestSysexResponse = async (type, data) => {
    configSent = Array.from(data);
    context.currentCfg[147] = data.length > 20 ? data[20] : context.currentCfg[147];
    context.applyCfgToUI(context.currentCfg);
  };
  element('cfg-momentary-role').value = '0';
  await context.applyConfigLive();
  assert.equal(configSent.length, 21);
  assert.deepEqual(configSent.slice(19), [0, 0]);
  element('cfg-momentary-role').value = '1';
  await context.saveConfig();
  assert.deepEqual(configSent.slice(19), [1, 1]);
  context.requestSysexResponse = async () => { throw new Error('Device NACK 4'); };
  element('cfg-momentary-role').value = '0';
  await context.applyConfigLive();
  assert.equal(element('cfg-momentary-role').value, 1);
  context.currentCfg = context.currentCfg.slice(0, 147);
  context.applyCfgToUI(context.currentCfg);
  assert.equal(element('cfg-momentary-role').disabled, true);
  context.requestSysexResponse = async (type, data) => { configSent = Array.from(data); };
  await context.saveConfig();
  assert.equal(configSent.length, 20);
  assert.equal(element('cfg-cv-clock-ppqn').disabled, true);
  context.currentCfg = [...context.currentCfg, 1, 4, 1];
  context.applyCfgToUI(context.currentCfg);
  assert.equal(element('cfg-cv-clock-ppqn').disabled, false);
  assert.equal(element('cfg-cv-clock-ppqn').value, 4);
  context.requestSysexResponse = async (type, data) => {
    configSent = Array.from(data);
    context.currentCfg[147] = data[20];
    if (data.length > 21) context.currentCfg[148] = data[21];
    context.applyCfgToUI(context.currentCfg);
  };
  for (const ppqn of [1, 2, 4, 8, 12, 16, 24]) {
    element('cfg-cv-clock-ppqn').value = String(ppqn);
    await context.applyConfigLive();
    assert.equal(configSent.length, 22);
    assert.deepEqual(configSent.slice(19), [0, 1, ppqn]);
    assert.equal(context.currentCfg[148], ppqn);
    assert.equal(context.settingsDirty, true);
  }
  element('cfg-cv-clock-ppqn').value = '12';
  await context.saveConfig();
  assert.deepEqual(configSent.slice(19), [1, 1, 12]);
  assert.equal(context.settingsDirty, false);
  context.requestSysexResponse = async () => { throw new Error('Device NACK 4'); };
  element('cfg-cv-clock-ppqn').value = '2';
  await context.applyConfigLive();
  assert.equal(element('cfg-cv-clock-ppqn').value, 12);
  context.updateBankActionAvailability();
  assert.equal(element('cfg-cv-clock-ppqn').disabled, false);
  context.bankBusy = true;
  context.updateBankActionAvailability();
  assert.equal(element('cfg-cv-clock-ppqn').disabled, true);
  context.bankBusy = false;
  context.settingsBusy = true;
  context.updateBankActionAvailability();
  assert.equal(element('cfg-cv-clock-ppqn').disabled, true);
  context.settingsBusy = false;
  context.currentCfg[149] = 0;
  context.applyCfgToUI(context.currentCfg);
  assert.equal(element('cfg-cv-clock-ppqn').disabled, true);
  assert.match(element('cfg-cv-clock-ppqn').title, /not enabled/);
  context.requestSysexResponse = async (type, data) => { configSent = Array.from(data); };
  await context.saveConfig();
  assert.equal(configSent.length, 21);
  assert.equal(context.currentCfg[148], 12);
  context.currentCfg = [...context.currentCfg, 1, 1, 1, 3, 1, 0];
  context.applyCfgToUI(context.currentCfg);
  assert.equal(element('cfg-reset-required').disabled, true);
  assert.equal(element('cfg-fills').disabled, false);
  assert.equal(element('cfg-reset-song').checked, true);
  assert.equal(element('cfg-mutation-amount').value, 3);
  element('cfg-fills').checked = false;
  element('cfg-mutation-amount').value = '8';
  element('cfg-reset-mutation').checked = false;
  element('cfg-seed-card-id').checked = true;
  await context.applyConfigLive();
  assert.deepEqual(configSent.slice(21), [12, 1, 1, 0, 8, 0, 1]);
  context.currentCfg[149] = 1;
  context.applyCfgToUI(context.currentCfg);
  assert.equal(element('cfg-reset-required').disabled, false);
  element('cfg-reset-required').checked = false;
  element('cfg-reset-song').checked = false;
  await context.saveConfig();
  assert.equal(configSent.length, 28);
  assert.deepEqual(configSent.slice(22), [0, 0, 1, 3, 1, 0]);
  let bankReads = 0;
  context.loadedForSlot = 1;
  context.bankDirty = true;
  element('bank-slot').value = '2';
  context.readBankConfigViaMidiRuntime = async slot => {
    ++bankReads;
    context.loadedForSlot = slot;
    context.bankDirty = false;
  };
  await context.onBankSlotChanged();
  assert.equal(bankReads, 0);
  assert.equal(element('bank-slot').value, 1);
  assert.equal(context.bankDirty, true);
  context.confirm = () => true;
  element('bank-slot').value = '2';
  await context.onBankSlotChanged();
  assert.equal(bankReads, 1);
  assert.equal(context.loadedForSlot, 2);
  context.readBankConfigViaMidiRuntime = async () => { throw new Error('timeout'); };
  element('bank-slot').value = '3';
  await context.onBankSlotChanged();
  assert.equal(element('bank-slot').value, 2);
  assert.equal(context.loadedForSlot, 2);
  assert.equal(context.bankBusy, false);

  context.bankValidity[3] = false;
  element('bank-slot').value = '3';
  await context.onBankSlotChanged();
  assert.equal(context.loadedForSlot, 3);
  assert.equal(context.bankDirty, false);
  assert.equal(element('bank-name').value, 'Bank 3');
  assert.ok(context.bankSlots.every(item => !item.sampleId && !item.isCompiled));

  context.bankSlots[0] = { sampleId: 0, volume: 127, midiNote: 36, isCompiled: true };
  context.bankDirty = true;
  element('bank-name').value = 'Draft kit';
  context.currentCfg[0] = 1;
  context.bankValidity[2] = true;
  let chunks = [];
  context.requestSysexResponse = async (type, data) => {
    assert.equal(type, 25);
    assert.equal(context.bankBusy, true);
    chunks.push(Array.from(data));
  };
  context.confirm = () => false;
  assert.equal(await context.writeBankToDevice(2), false);
  assert.equal(chunks.length, 0);
  assert.equal(context.bankDirty, true);
  context.confirm = () => true;
  assert.equal(await context.writeBankToDevice(2), true);
  assert.ok(chunks.length > 1);
  assert.ok(chunks.every(chunk => chunk[0] === 2));
  const copied = context.parseBankConfigFileText(chunks.map(chunk =>
    Buffer.from(chunk.slice(6).map(byte => String.fromCharCode(byte)).join(''), 'hex').toString('utf8')).join(''));
  assert.equal(copied.bankName, 'Draft kit');
  assert.equal(copied.slots[0].isCompiled, true);
  assert.equal(copied.slots[0].sampleId, 0);
  assert.equal(context.bankDirty, true);
  assert.equal(context.loadedForSlot, 3);
  assert.equal(element('bank-slot').value, '3');
  assert.equal(context.currentCfg[0], 1);

  let warning;
  context.confirm = message => { warning = message; return false; };
  assert.equal(await context.writeBankToDevice(1), false);
  assert.match(warning, /playing.*sound will change/);
  context.confirm = () => true;
  context.requestSysexResponse = async () => { throw new Error('upload interrupted'); };
  assert.equal(await context.writeBankToDevice(2), false);
  assert.equal(context.bankDirty, true);
  assert.equal(context.loadedForSlot, 3);
  assert.equal(context.bankBusy, false);
  assert.match(context.status, /upload interrupted/);

  context.requestSysexResponse = async (type, data) => { chunks.push(Array.from(data)); };
  assert.equal(await context.writeBankToDevice(), true);
  assert.equal(context.bankValidity[3], true);
  assert.equal(context.bankDirty, false);
  assert.equal(context.currentCfg[0], 1);
  context.currentCfg[4] = 42;
  assert.equal(context.buildCfgFromUI()[4], 42);
  const unicodeName = context.buildBankNameBytes('\u00e9'.repeat(23));
  assert.equal(new TextDecoder().decode(unicodeName).replace(/\0/g, ''), '\u00e9'.repeat(11));
  context.bankDirty = true;
  context.requestSysexResponse = async () => ({ payload: [0, 0] });
  await assert.rejects(context.fetchDeviceSlots(), /Device slots changed/);
  assert.equal(context.bankSlots[0].isCompiled, true);
  context.setSampleMetaTags('abc', ['original']);
  assert.equal(context.sampleMetaDirty, true);
  context.requestSysexResponse = async () => { throw new Error('Tag save interrupted'); };
  await context.writeSampleMetaToDevice();
  assert.equal(context.sampleMetaDirty, true);
  assert.equal(context.sampleMetaBusy, false);
  context.bankBusy = true;
  context.updateBankActionAvailability();
  assert.equal(element('bank-form').disabled, true);
  assert.equal(element('cfg-active-bank').disabled, true);
  assert.equal(element('save-settings-btn').disabled, true);
  context.bankBusy = false;
  context.updateBankActionAvailability();
  assert.equal(element('bank-form').disabled, false);
  context.requestSysexResponse = async () => { context.setSampleMetaTags('abc', ['newer']); };
  await context.writeSampleMetaToDevice();
  assert.equal(context.sampleMetaDirty, true);
  assert.equal(context.sampleMetaBusy, false);
  context.requestSysexResponse = async () => { throw new Error('Dirty tags must not be reloaded'); };
  await context.fetchSampleMetaFromDevice();
  assert.deepEqual(context.sampleMeta.abc.tags, ['newer']);
  context.requestSysexResponse = async () => {};
  await context.writeSampleMetaToDevice();
  assert.equal(context.sampleMetaDirty, false);
  assert.equal(context.sampleMetaBusy, false);
  console.log('PASS: sample-tag refresh preservation, failed-save recovery and in-flight edit protection');
  const clockPayload = [1, 1, 2, 1, 0, 1, 0, 2, 24, 0, 0, 1, 3, 1, 1];
  const values = [1234, 120000, 119500, 987654, 0xffffffff, 99, 2, 3, 400, 0xffffffff,
    10000, 9000, 11000, 123, 456, 98, 1, 98, 98, 4, 10000, 400, 0, 0, 24];
  values.forEach(value => { for (let index = 0; index < 5; ++index) clockPayload.push(Math.floor(value / 2 ** (index * 7)) & 127); });
  const clock = context.decodeClockStatus(clockPayload);
  assert.equal(clock.seed, 0xffffffff);
  assert.equal(clock.setBpm, 120);
  assert.equal(clock.engineBpm, 119.5);
  assert.equal(clock.detectedBpm, 250);
  assert.equal(clock.resetAgeUs, 0xffffffff);
  assert.throws(() => context.decodeClockStatus(clockPayload.slice(1)), /Unsupported/);
  const invalidCounter = [...clockPayload]; invalidCounter[19] = 16;
  assert.throws(() => context.decodeClockStatus(invalidCounter), /Invalid clock counter/);
  context.currentCfg = new Array(156).fill(0);
  context.renderClockStatus(clock);
  assert.equal(element('clock-detected-bpm').textContent, '250.00');
  assert.match(element('clock-warning').textContent, /3 CV input events lost/);
  assert.equal(element('clock-position').textContent, '987654 / -1');
  context.toggleClockRecording();
  context.renderClockStatus({ ...clock, rawPulses: 120, rawResets: 3 });
  assert.equal(context.clockTrace.length, 1);
  assert.equal(element('clock-pulse-light').className, 'clock-edge active');
  assert.equal(element('clock-reset-light').className, 'clock-edge active');
  context.clearClockTrace();
  assert.equal(context.clockTrace.length, 1);
  let download;
  context.triggerDownload = (bytes, filename, mime) => { download = { json: JSON.parse(new TextDecoder().decode(bytes)), filename, mime }; };
  context.downloadClockTrace();
  assert.equal(download.json.format, 'compulidian-clock-trace');
  assert.equal(download.json.samples[0].rawPulses, 120);
  assert.equal(download.mime, 'application/json');
  context.toggleClockRecording();
  context.clearClockTrace();
  assert.equal(context.clockTrace.length, 0);
  context.clockTrace = new Array(1199).fill(clock);
  context.toggleClockRecording();
  context.renderClockStatus(clock);
  assert.equal(context.clockTrace.length, 1200);
  assert.equal(context.clockRecording, false);
  assert.equal(element('clock-record').disabled, true);
  context.clearClockTrace();
  let clockReads = 0;
  context.bankBusy = false; context.sampleMetaBusy = false; context.settingsBusy = false;
  context.requestSysexResponse = async (type, payload, types, timeout, retries) => {
    ++clockReads;
    assert.equal(type, 27); assert.equal(types[0], 71); assert.equal(retries, 0);
    return { payload: clockPayload };
  };
  context.bankBusy = true; await context.pollClockStatus(); assert.equal(clockReads, 0);
  context.bankBusy = false;
  context.midiPendingRequests.set(1, {}); await context.pollClockStatus(); assert.equal(clockReads, 0);
  context.midiPendingRequests.clear();
  const polling = context.pollClockStatus(); await context.pollClockStatus(); await polling;
  assert.equal(clockReads, 1);
  context.currentCfg = new Array(150).fill(0); await context.pollClockStatus(); assert.equal(clockReads, 1);
  context.currentCfg = new Array(156).fill(0);
  context.requestSysexResponse = async () => { ++context.clockStatusEpoch; return { payload: clockPayload }; };
  context.clockStatusLatest = null;
  await context.pollClockStatus(); assert.equal(context.clockStatusLatest, null);
  console.log('PASS: MIDI-safe clock decoder, BPM/readouts, edge indicators, bounded JSON traces, polling exclusion and stale-response protection');
  console.log('PASS: automatic empty-bank drafts, copy/cancel/failure, dirty-source preservation, compiled index zero, hidden settings round-trip and reconnect draft protection');
  console.log('PASS: compiled-only bank loading, non-destructive bank rejection, compiled catalog, page startup, bank-config round-trip, rejected/confirmed bank changes');
})().catch(error => { console.error(error); process.exitCode = 1; });
"""


CONTROLS_HARNESS = r"""
#include <cassert>
#include "device_controls.h"
#include "settings.h"
#include <cstddef>
#include <cstring>
#include <initializer_list>
#define ATOMIC_BLOCK(mode) for (bool atomic_once = true; atomic_once; atomic_once = false)
using byte = uint8_t;
struct ComputerCard {
  enum Knob {Main, X, Y};
  enum Switch {Down, Middle, Up};
  static constexpr int numLeds = 6;
  int knobs[3] = {1000, 1000, 1000};
  Switch position = Down;
  bool ready = true;
  bool leds[6] = {};
  int KnobVal(Knob knob) { return knobs[knob]; }
  Switch SwitchVal() { return position; }
  bool SwitchReady() const { return ready; }
  void LedOn(int led, bool on = true) { leds[led] = on; }
  uint64_t UniqueCardID() { return UINT64_C(0x1234567887654321); }
} sw;
class ComputerCardVoltageSource {
public:
  byte channel;
  ComputerCardVoltageSource(int, ComputerCard*, byte channel, float, bool) : channel(channel) {}
  virtual float adcread_to_voltage(int16_t reading) { return reading; }
};
DeviceControls device_controls;
@KNOB_SOURCE@;
int startup_stage = 0;
struct EuclidianPattern {
  bool rebuilt = false;
  void make_euclid() {
    assert(startup_stage == 4 || startup_stage == 5);
    assert(!rebuilt);
    rebuilt = true;
    startup_stage = 5;
  }
};
struct EuclidianSequencer {
  bool locked = false;
  bool fills = true, reset_mutation = true, identity = false;
  int mutation = 3;
  uint32_t identity_value = 0;
  void set_seed_identity(uint32_t value) { identity_value = value; }
  void set_add_identity_enabled(bool value) { identity = value; }
  void set_fills_enabled(bool value) { fills = value; }
  void set_mutation_count(int value) { mutation = value; }
  void set_reset_before_mutate(bool value) { reset_mutation = value; }
  EuclidianPattern patterns[2];
  unsigned int get_number_patterns() { return 2; }
  EuclidianPattern* get_pattern(unsigned int index) { return &patterns[index]; }
  bool is_euclidian_seed_lock() { return locked; }
  void set_euclidian_seed_lock(bool value) { locked = value; }
} sequence;
EuclidianSequencer* sequencer = &sequence;
struct Output {
  bool muted = false;
  bool control_leds_owned = false;
  void set_muted(bool value) { muted = value; }
} output_wrapper;
struct BankManager {
  static constexpr int MAX_FLASH_BANKS = 4;
  int active = 2;
  bool fail = false;
  int active_bank() const { return active; }
  bool is_bank_valid(int bank) { return bank == 0 || bank == 2 || bank == 4; }
  int num_valid_banks() { return 3; }
  const char* get_bank_name(int bank) { return bank == 2 ? "Two" : nullptr; }
  bool switch_bank(int bank, bool persist) {
    assert(!persist);
    if (fail || !is_bank_valid(bank)) return false;
    active = bank;
    return true;
  }
} bankManager;
Settings current_settings{};
unsigned saves = 0;
void settings_save(const Settings* settings) {
  assert(settings->active_bank == bankManager.active_bank());
  ++saves;
}
uint32_t clock_ms = 0;
uint32_t millis() { return clock_ms; }
bool control_feedback_active = false;
bool control_feedback_ok = true;
uint32_t control_feedback_at = 0;
constexpr int FLASH_MAX_BANKS = 4;
@CONFIG_CONSTANTS@
enum { PENDING_SEND_ACK, PENDING_SEND_NACK, PENDING_SEND_CONFIG };
constexpr int SYSEX_NACK_BAD_LENGTH = 1;
constexpr int SYSEX_NACK_INVALID_VALUE = 4;
int last_response = -1;
int last_error = 0;
void enqueue_pending_sysex(int response, uint8_t, int error = 0) {
  last_response = response;
  last_error = error;
}
#define USE_UCLOCK
#define ENABLE_EUCLIDIAN
#ifndef TEST_NO_CV_CLOCK
#define ENABLE_CLOCK_INPUT_CV
#endif
int applied_cv_ppqn = 0;
bool set_external_cv_clock_ppqn(uint16_t ppqn) { applied_cv_ppqn = ppqn; return true; }
Settings g_settings_storage;
void ensure_settings_root() {}
void sl_load_from_file(const char*, int) {}
#define LIVE_SETTINGS_SAVE_PATH "/save/settings.txt"
constexpr int SL_SCOPE_SYSTEM = 0;
@LOAD_SETTINGS@
@APPLY_SEQUENCER@
@SET_CONFIG@
@BUILD_CONFIG@
@HANDLE_CONTROLS@
@CONTROL_LEDS@
@DEFAULT_SETTINGS@
bool playback_started = false;
int clock_starts = 0;
struct VoltageSource {
  int reads = 0;
  void update() { ++reads; startup_stage = 1; }
};
struct VoltageSources {
  VoltageSource values[8];
  unsigned int size() { return 8; }
  VoltageSource* get(unsigned int index) { return &values[index]; }
};
struct Parameters {
  VoltageSources sources;
  VoltageSources* voltage_sources = &sources;
  void update_inputs() {
    assert(startup_stage == 1);
    for (auto& source : sources.values) assert(source.reads == 1);
    startup_stage = 2;
  }
  void update_mixers() { assert(startup_stage == 2); startup_stage = 3; }
  void process_pending() { assert(startup_stage == 3); startup_stage = 4; }
} parameters;
Parameters* parameter_manager = &parameters;
void clock_start() {
  assert(startup_stage == 5);
  for (auto& pattern : sequence.patterns) assert(pattern.rebuilt);
  ++clock_starts;
}
#define USE_UCLOCK
#define ENABLE_PARAMETERS
#define ENABLE_EUCLIDIAN
@START_PLAYBACK@
int main() {
  static_assert(offsetof(Settings, momentary_role) == 9);
  static_assert(offsetof(Settings, cv_clock_ppqn) == 10);
  static_assert(offsetof(Settings, outputs) == 12);
  assert(settings_defaults().momentary_role == MOMENTARY_MUTE);
  assert(settings_defaults().cv_clock_ppqn == 1);
  static_assert(offsetof(Settings, extended_settings_version) == 33);
  auto extended = settings_defaults();
  assert(extended.cv_reset_required && extended.cv_reset_song_position && extended.fills_enabled);
  assert(extended.mutation_amount == 3 && extended.reset_before_mutate && !extended.add_card_id_to_seed);
  extended.extended_settings_version = 0;
  extended.cv_reset_required = extended.cv_reset_song_position = extended.fills_enabled = 0;
  extended.mutation_amount = extended.reset_before_mutate = 0;
  settings_normalize_extended(extended);
  assert(extended.cv_reset_required && extended.cv_reset_song_position && extended.fills_enabled);
  assert(extended.mutation_amount == 3 && extended.reset_before_mutate);
  extended.cv_reset_required = extended.cv_reset_song_position = extended.fills_enabled = 0;
  extended.mutation_amount = extended.reset_before_mutate = 0;
  extended.add_card_id_to_seed = 1;
  settings_normalize_extended(extended);
  assert(!extended.cv_reset_required && !extended.cv_reset_song_position && !extended.fills_enabled);
  assert(!extended.mutation_amount && !extended.reset_before_mutate && extended.add_card_id_to_seed);
  for (int ppqn = 0; ppqn <= 255; ++ppqn)
    assert(settings_valid_cv_clock_ppqn(ppqn) == (ppqn == 1 || ppqn == 2
      || ppqn == 4 || ppqn == 8 || ppqn == 12 || ppqn == 16 || ppqn == 24));
  for (int ppqn : {0, 1, 2, 3, 4, 8, 12, 16, 24, 25, 255}) {
    g_settings_storage = settings_defaults();
    g_settings_storage.cv_clock_ppqn = ppqn;
    assert(settings_load().cv_clock_ppqn == (settings_valid_cv_clock_ppqn(ppqn) ? ppqn : 1));
  }
  DeviceControls controls;
  auto poll = [&](uint32_t now, bool down, uint16_t main, uint16_t knob_x = 1000) {
    controls.update(now, down, main, knob_x, 0x15, 2, MOMENTARY_MUTE);
  };
  poll(0, true, 1000);
  poll(31, true, 1000);
  assert(controls.setup_active());
  assert(!controls.muted(MOMENTARY_MUTE));
  poll(32, false, 1000);
  poll(63, false, 1000);
  assert(!controls.request_pending());
  assert(controls.candidate_bank() == 2);
  poll(64, false, 4095, 4000);
  assert(controls.candidate_bank() == 4);
  assert(controls.candidate_role() == MOMENTARY_BANK);
  assert(controls.filter_main(4095) == 1000);
  assert(controls.filter_x(4000) == 1000);
  poll(65, true, 4095, 4000);
  poll(96, true, 4095, 4000);
  assert(controls.request_pending());
  controls.complete(false, 4095);
  assert(controls.setup_active());
  poll(97, false, 4095, 4000);
  poll(128, false, 4095, 4000);
  poll(129, true, 4095, 4000);
  poll(160, true, 4095, 4000);
  controls.complete(true, 4095);
  poll(161, false, 4095, 4000);
  poll(192, false, 4095, 4000);
  assert(!controls.owns_main());
  assert(controls.filter_main(3000) == 3000);
  assert(controls.filter_main(900) == 900);
  assert(controls.filter_x(3000) == 3000);
  assert(controls.filter_x(900) == 900);
  poll(193, true, 900);
  poll(224, true, 900);
  poll(225, true, 4095);
  assert(controls.muted(MOMENTARY_MUTE));
  assert(!controls.owns_main());
  assert(!controls.request_pending());
  assert(controls.filter_main(4095) == 4095);

  DeviceControls performance;
  auto play = [&](uint32_t now, bool down, uint16_t main) {
    performance.update(now, down, main, 1000, 0x15, 2, MOMENTARY_BANK);
  };
  play(0, false, 1000);
  play(31, false, 1000);
  play(32, true, 1000);
  play(63, true, 1000);
  assert(performance.candidate_bank() == 2);
  assert(!performance.muted(MOMENTARY_BANK));
  play(64, true, 0);
  assert(performance.candidate_bank() == 0);
  play(65, false, 0);
  play(96, false, 0);
  assert(performance.request_pending());
  assert(!performance.setup_request());
  performance.complete(true, 0);
  assert(performance.filter_main(500) == 500);
  assert(performance.filter_main(1100) == 1100);
  play(97, true, 1100);
  play(128, true, 1100);
  assert(performance.owns_main());
  performance.update(129, true, 2000, 1000, 0x15, 2, MOMENTARY_MUTE);
  assert(!performance.owns_main());
  assert(!performance.request_pending());
  assert(performance.muted(MOMENTARY_MUTE));

  DeviceControls noise;
  noise.update(0xffffffd0u, false, 1000, 1000, 1, 0, MOMENTARY_MUTE);
  noise.update(0xfffffff0u, false, 1000, 1000, 1, 0, MOMENTARY_MUTE);
  noise.update(0xfffffff1u, true, 1000, 1000, 1, 0, MOMENTARY_MUTE);
  noise.update(0xfffffff5u, false, 1000, 1000, 1, 0, MOMENTARY_MUTE);
  noise.update(0x20u, false, 1000, 1000, 1, 0, MOMENTARY_MUTE);
  assert(!noise.muted(MOMENTARY_MUTE) && !noise.setup_active());

  current_settings.active_bank = 2;
  auto step = [&](uint32_t now, ComputerCard::Switch position, int main, int knob_x = 1000) {
    clock_ms = now;
    sw.position = position;
    sw.knobs[0] = main;
    sw.knobs[1] = knob_x;
    handle_config_mode();
    start_playback_when_ready();
    update_device_control_leds();
  };
  ControlKnobVoltageSource main_source(0, &sw, 0), x_source(2, &sw, 1);
  sw.ready = false;
  step(0, ComputerCard::Down, 1000);
  step(31, ComputerCard::Down, 1000);
  assert(device_controls.mode() == DeviceControls::BootCheck);
  assert(clock_starts == 0 && startup_stage == 0);
  sw.ready = true;
  step(32, ComputerCard::Middle, 1000);
  assert(clock_starts == 0);
  step(63, ComputerCard::Middle, 1000);
  assert(device_controls.mode() == DeviceControls::Performance);
  assert(clock_starts == 1 && startup_stage == 5);
  device_controls = DeviceControls{};
  clock_ms = 0;
  step(0, ComputerCard::Down, 1000);
  step(31, ComputerCard::Down, 1000);
  step(32, ComputerCard::Middle, 1000);
  step(63, ComputerCard::Middle, 1000);
  assert(sw.leds[2] && !sw.leds[5]);
  assert(saves == 0);
  step(64, ComputerCard::Middle, 4095, 4095);
  assert(main_source.adcread_to_voltage(4095) == 1000);
  assert(x_source.adcread_to_voltage(4095) == 1000);
  assert(sw.leds[4] && sw.leds[5]);
  bankManager.fail = true;
  step(65, ComputerCard::Down, 4095, 4095);
  step(96, ComputerCard::Down, 4095, 4095);
  assert(current_settings.active_bank == 2);
  assert(current_settings.momentary_role == MOMENTARY_MUTE);
  assert(saves == 0 && !output_wrapper.muted);
  bankManager.fail = false;
  step(97, ComputerCard::Middle, 4095, 4095);
  step(128, ComputerCard::Middle, 4095, 4095);
  step(129, ComputerCard::Down, 4095, 4095);
  step(160, ComputerCard::Down, 4095, 4095);
  assert(current_settings.active_bank == 4);
  assert(current_settings.momentary_role == MOMENTARY_BANK);
  assert(saves == 1);
  step(161, ComputerCard::Middle, 4095, 4095);
  step(192, ComputerCard::Middle, 4095, 4095);
  step(1000, ComputerCard::Middle, 1000);
  assert(!output_wrapper.control_leds_owned);
  assert(main_source.adcread_to_voltage(1000) == 1000);
  step(1001, ComputerCard::Down, 1000);
  step(1032, ComputerCard::Down, 1000);
  assert(!output_wrapper.muted);
  step(1033, ComputerCard::Down, 0);
  assert(main_source.adcread_to_voltage(0) == 1000);
  step(1034, ComputerCard::Middle, 0);
  step(1065, ComputerCard::Middle, 0);
  assert(current_settings.active_bank == 0 && saves == 2);
  step(1066, ComputerCard::Down, 0);
  step(1097, ComputerCard::Down, 0);
  step(1098, ComputerCard::Middle, 0);
  step(1129, ComputerCard::Middle, 0);
  assert(saves == 2);
  current_settings.momentary_role = MOMENTARY_MUTE;
  step(2000, ComputerCard::Up, 0);
  assert(sequence.locked);
  step(2001, ComputerCard::Down, 0);
  step(2032, ComputerCard::Down, 0);
  assert(output_wrapper.muted && !sequence.locked);
  assert(!device_controls.owns_main());
  assert(clock_starts == 1);

  current_settings.momentary_role = MOMENTARY_BANK;
  current_settings.cv_clock_ppqn = 8;
  uint8_t request[28] = {};
  request[0] = 0;
  handle_set_config_(1, request, 19);
  assert(current_settings.momentary_role == MOMENTARY_BANK && saves == 3);
  request[0] = 2;
  handle_set_config_(1, request, 20);
  assert(current_settings.active_bank == 2 && saves == 3);
  handle_set_config_(1, request, 21);
  assert(current_settings.momentary_role == MOMENTARY_MUTE && saves == 3);
  request[0] = 4;
  request[20] = 2;
  handle_set_config_(1, request, 21);
  assert(last_response == PENDING_SEND_NACK && last_error == SYSEX_NACK_INVALID_VALUE);
  assert(bankManager.active_bank() == 2 && saves == 3);
  request[20] = 1;
  request[19] = 1;
  handle_set_config_(1, request, 21);
  assert(current_settings.momentary_role == MOMENTARY_BANK && saves == 4);
  assert(last_response == PENDING_SEND_CONFIG);
  bankManager.fail = true;
  request[0] = 2;
  request[20] = 0;
  handle_set_config_(1, request, 21);
  assert(current_settings.active_bank == 4);
  assert(current_settings.momentary_role == MOMENTARY_BANK && saves == 4);
  assert(last_response == PENDING_SEND_NACK);
  handle_set_config_(1, nullptr, 0);
  assert(last_error == SYSEX_NACK_BAD_LENGTH);
  assert(current_settings.cv_clock_ppqn == 8 && applied_cv_ppqn == 0);
  bankManager.fail = false;
  for (uint8_t ppqn : {1, 2, 4, 8, 12, 16, 24}) {
    request[0] = 4;
    request[19] = 0;
    request[20] = MOMENTARY_BANK;
    request[21] = ppqn;
    handle_set_config_(1, request, 22);
    assert(current_settings.cv_clock_ppqn == ppqn && saves == 4);
    #ifdef ENABLE_CLOCK_INPUT_CV
      assert(applied_cv_ppqn == ppqn);
    #else
      assert(applied_cv_ppqn == 0);
    #endif
  }
  for (uint8_t ppqn : {0, 3, 23, 25, 127}) {
    request[0] = 2;
    request[19] = 1;
    request[20] = MOMENTARY_MUTE;
    request[21] = ppqn;
    handle_set_config_(1, request, 22);
    assert(last_response == PENDING_SEND_NACK && last_error == SYSEX_NACK_INVALID_VALUE);
    assert(bankManager.active_bank() == 4 && current_settings.active_bank == 4);
    assert(current_settings.cv_clock_ppqn == 24 && current_settings.momentary_role == MOMENTARY_BANK && saves == 4);
  }
  request[0] = 4;
  request[20] = MOMENTARY_BANK;
  request[21] = 12;
  handle_set_config_(1, request, 22);
  assert(saves == 5 && current_settings.cv_clock_ppqn == 12);
  request[19] = 0;
  request[22] = request[23] = request[24] = request[26] = 0;
  request[25] = 8;
  request[27] = 1;
  handle_set_config_(1, request, 28);
  assert(saves == 5 && !current_settings.cv_reset_required && !current_settings.cv_reset_song_position);
  assert(!sequence.fills && sequence.mutation == 8 && !sequence.reset_mutation && sequence.identity);
  assert(sequence.identity_value == (0x12345678u ^ 0x87654321u));
  for (int index = 22; index < 28; ++index) {
    const uint8_t previous = request[index];
    request[index] = index == 25 ? 9 : 2;
    request[0] = 2;
    handle_set_config_(1, request, 28);
    assert(last_response == PENDING_SEND_NACK && bankManager.active_bank() == 4 && saves == 5);
    assert(sequence.mutation == 8 && sequence.identity);
    request[index] = previous;
  }
  for (int length = 23; length < 28; ++length) {
    handle_set_config_(1, request, length);
    assert(last_response == PENDING_SEND_NACK && last_error == SYSEX_NACK_BAD_LENGTH);
  }
  request[0] = 4;
  request[19] = 1;
  handle_set_config_(1, request, 28);
  assert(saves == 6);
  request[19] = 0;
  handle_set_config_(1, request, 22);
  assert(sequence.mutation == 8 && current_settings.add_card_id_to_seed && !current_settings.cv_reset_required);
  uint8_t response[157];
  memset(response, 0xa5, sizeof(response));
  uint16_t length = 0;
  assert(build_config_payload(response, &length));
  assert(length == 156 && response[147] == MOMENTARY_BANK && response[148] == 12 && response[156] == 0xa5);
  assert(response[150] == 0 && response[151] == 0 && response[152] == 0);
  assert(response[153] == 8 && response[154] == 0 && response[155] == 1);
  #ifdef ENABLE_CLOCK_INPUT_CV
    assert(response[149] == 1);
  #else
    assert(response[149] == 0);
  #endif
  assert(response[0] == 4 && response[1] == 3);
  assert(strcmp(reinterpret_cast<const char*>(response + 51), "Two") == 0);
}
"""


MIXER_STARTUP_HARNESS = r"""
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <type_traits>
template<class Value> Value constrain(Value value, Value lower, Value upper) {
  return std::max(lower, std::min(value, upper));
}
struct Console {
  explicit operator bool() const { return false; }
  template<class... Args> void printf(const char*, Args...) {}
  template<class Value> void print(Value) {}
  template<class Value> void println(Value) {}
  void flush() {}
} Serial;
template<class DataType> struct ProxiedValue { DataType source, effective; };
template<class DataType> struct DataParameterBase {
  bool debug = false, slewing = false;
  const char* label;
  float modulation = 0, modulateNormalValue = 0;
  float lastModulatedNormalValue = 0, lastOutputNormalValue = 0;
  float lastRealOutputNormalValue = 0;
  DataType currentDataValue = 0;
  DataType minimumDataLimit = 0, maximumDataLimit = 1;
  DataType minimumDataRange = 0, maximumDataRange = 1;
  float initialNormalValue = 0;
  @LAST_GETTER@
  @SENT_STATE@
  explicit DataParameterBase(const char* label) : label(label) {}
  void initialise_values(DataType lower, DataType upper) {
    minimumDataLimit = minimumDataRange = lower;
    maximumDataLimit = maximumDataRange = upper;
  }
  virtual DataType getter() = 0;
  virtual DataType getCurrentDataValue() = 0;
  virtual float getCurrentNormalValue() = 0;
  virtual void setTargetValueFromData(DataType, bool = false) = 0;
  virtual void updateValueFromNormal(float value) { currentDataValue = DataType(value); }
  virtual void updateValueFromData(DataType value) { currentDataValue = value; }
  void setInitialValueFromData(DataType value) { currentDataValue = value; }
  float get_modulation_value() { return modulation; }
  float constrainNormal(float value) { return std::max(0.0f, std::min(1.0f, value)); }
  float get_slewed_value(float value) { return value; }
  @RANGE_METHODS@
  @UPDATE_MIXER@
  @SEND_CURRENT@
  @SET_TARGET@
};
template<class DataType>
@PROXY@;
namespace vl { template<class Signature> using Func = std::function<Signature>; }
template<class DataType = float>
@LDATA@;
struct TrackedProxy : ProxyParameter<float> {
  int sends = 0;
  using ProxyParameter<float>::ProxyParameter;
  void setTargetValueFromData(float value, bool force = false) override {
    ++sends;
    ProxyParameter<float>::setTargetValueFromData(value, force);
  }
};
int main() {
  float configured = 0.75f;
  int setter_calls = 0;
  LDataParameter<float> unchanged("Existing control",
    [&](float value) { configured = value; ++setter_calls; },
    [&]() { return configured; });
  unchanged.update_mixer();
  assert(configured == 0.75f && setter_calls == 0);
  int rotation = 0, effective_rotation = 0;
  ProxyParameter<int> matching("Rotation", &rotation, &effective_rotation, 1, 16);
  matching.update_mixer();
  assert(rotation == 0 && effective_rotation == 0);
  for (float knob : {0.0f, 0.5f, 1.0f}) {
    float base = 0.5f, effective = 0.5f;
    TrackedProxy parameter("Density", &base, &effective, 0.0f, 1.0f);
    base = 0.0f;
    parameter.modulation = knob;
    parameter.update_mixer();
    const int initial_sends = knob == 0.5f ? 0 : 1;
    assert(effective == knob && parameter.sends == initial_sends);
    parameter.update_mixer();
    assert(parameter.sends == initial_sends);
    parameter.modulation = 0.25f;
    parameter.update_mixer();
    assert(effective == 0.25f && parameter.sends == initial_sends + 1);
    parameter.modulation = 0.0f;
    parameter.update_mixer();
    assert(effective == 0.0f && parameter.sends == initial_sends + 2);
  }
}
"""


SEED_HARNESS = r"""
#include <cassert>
#include <cstdint>
uint32_t phrase = 3;
#define BPM_CURRENT_PHRASE phrase
struct EuclidianSequencer {
  int seed = 0;
  uint32_t seed_identity = 0;
  bool add_identity_to_seed = false, add_phrase_to_seed = true, seed_locked = false;
  uint32_t last_locked_seed = 0;
  @SEED_METHODS@
};
int main() {
  EuclidianSequencer sequence;
  sequence.set_seed_identity(100);
  assert(sequence.get_euclidian_seed() == 3);
  sequence.set_add_identity_enabled(true);
  assert(sequence.get_euclidian_seed() == 103);
  phrase = 4;
  assert(sequence.get_euclidian_seed() == 104);
  sequence.set_euclidian_seed(12);
  assert(sequence.get_euclidian_seed() == 12);
  sequence.set_euclidian_seed_lock(true);
  phrase = 8;
  sequence.set_seed_identity(200);
  assert(sequence.get_euclidian_seed() == 12);
  sequence.set_euclidian_seed_lock(false);
  assert(sequence.get_euclidian_seed() == 116);
  sequence.set_seed_identity(UINT32_MAX);
  sequence.set_euclidian_seed(INT32_MIN);
  assert(sequence.get_euclidian_seed() == INT32_MIN);
  sequence.set_add_identity_enabled(false);
  sequence.set_add_phrase_enabled(false);
  sequence.set_euclidian_seed(42);
  assert(sequence.get_euclidian_seed() == 42);
}
"""


def main():
    root = Path(__file__).resolve().parents[1]
    compiler = shutil.which('g++') or shutil.which('clang++')
    node = shutil.which('node')
    if not compiler or not node:
        raise SystemExit('A native C++ compiler and Node.js are required')
    header = (root / 'include/audio/bank_manager.h').read_text()
    configurator = (root / 'include/configurator.h').read_text()
    bank_config = (root / 'include/audio/bank_config.h').read_text()
    bank_manager = (root / 'src/audio/bank_manager.cpp').read_text()
    harness = CPP_HARNESS.replace('@COUNT@', extract_block(header, 'size_t compiled_voice_count()'))
    harness = harness.replace('@NOTE@', extract_block(header, 'int compiled_voice_note('))
    harness = harness.replace('@SLOT_CONFIG@', extract_block(bank_config, 'struct SlotConfig {'))
    harness = harness.replace('@LOADER@', extract_block(bank_manager, 'bool BankManager::load_bank_config_('))
    harness = harness.replace('@RELEASE_POOL@', extract_block(bank_manager, 'void BankManager::release_flash_pool_()'))
    harness = harness.replace('@SWITCH@', extract_block(bank_manager, 'bool BankManager::switch_bank('))
    harness = harness.replace('@REFRESH@', extract_block(bank_manager, 'void BankManager::refresh_bank('))
    harness = harness.replace('@SILENCE@', extract_block(bank_manager, 'void BankManager::silence_all_voices_()'))
    harness = harness.replace('@SENDER@', extract_block(configurator, 'static void send_slots_info_sysex(uint8_t req_id) {'))
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'catalog.cpp'
        executable = Path(directory) / 'catalog'
        source.write_text(harness)
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable)], check=True)
        payload = subprocess.check_output([str(executable)], text=True)
        json.loads(payload)
        subprocess.run([node, '-e', JS_HARNESS, str(root / 'web/compulidian_manager.js')], input=payload, text=True, check=True)
        audio_header = (root / 'include/audio/audio.h').read_text()
        source.write_text(AUDIO_HARNESS.replace('@PLAYER@', extract_block(audio_header, 'class SamplePlayer : public ComputerCard')))
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print('PASS: audio callback/buffer diagnostics and sample generation')
        controls = CONTROLS_HARNESS.replace('@KNOB_SOURCE@', extract_block((root / 'src/cv_input.cpp').read_text(), 'class ControlKnobVoltageSource'))
        controls = controls.replace('@HANDLE_CONTROLS@', extract_block(configurator, 'static void handle_config_mode()'))
        controls = controls.replace('@SET_CONFIG@', extract_block(configurator, 'static void handle_set_config_('))
        controls = controls.replace('@APPLY_SEQUENCER@', extract_block(configurator, 'static void apply_sequencer_settings('))
        controls = controls.replace('@BUILD_CONFIG@', extract_block(configurator, 'static bool build_config_payload('))
        controls = controls.replace('@CONFIG_CONSTANTS@', '\n'.join(line for line in configurator.splitlines() if line.startswith((
          '#define SYSEX_DATA_LEN ', '#define SYSEX_BANK_NAME ',
          '#define SYSEX_CONFIG_ROLE_OFFSET ', '#define SYSEX_CONFIG_CV_PPQN_OFFSET ',
          '#define SYSEX_CONFIG_CV_CLOCK_OFFSET ', '#define SYSEX_CONFIG_OPTIONS_OFFSET ', '#define SYSEX_CONFIG_PAYLOAD_LEN '))))
        controls = controls.replace('@CONTROL_LEDS@', extract_block(configurator, 'static void update_device_control_leds()'))
        controls = controls.replace('@DEFAULT_SETTINGS@', extract_block((root / 'src/settings.cpp').read_text(), 'Settings settings_defaults()'))
        controls = controls.replace('@LOAD_SETTINGS@', extract_block((root / 'src/settings.cpp').read_text(), 'Settings settings_load()'))
        main_source = (root / 'src/main.cpp').read_text()
        assert 'clock_start();' not in extract_block(main_source, 'void setup()')
        assert main_source.index('set_external_cv_clock_ppqn(current_settings.cv_clock_ppqn);') < main_source.index('setup_uclock(do_tick);')
        controls = controls.replace('@START_PLAYBACK@', extract_block(main_source, 'static void start_playback_when_ready()'))
        source.write_text(controls)
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I', str(root / 'include'), str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all', '-DTEST_NO_CV_CLOCK', '-I', str(root / 'include'), str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print('PASS: startup switch readiness, immediate knob response, switch roles, sparse banks, failed confirmation, LED ownership and persistence')
        sequence_header = (root.parent / 'my_libraries/seqlib/include/sequencer/Euclidian/Sequencer.h').read_text()
        seed_methods = '\n'.join(extract_block(sequence_header, anchor) for anchor in (
          'void set_seed_identity(', 'bool is_add_identity_enabled(', 'void set_add_identity_enabled(',
          'bool is_add_phrase_enabled(', 'void set_add_phrase_enabled(',
          'bool is_euclidian_seed_lock(', 'void set_euclidian_seed_lock(',
          'int get_euclidian_seed(', 'void set_euclidian_seed('))
        source.write_text(SEED_HARNESS.replace('@SEED_METHODS@', seed_methods))
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all', str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print('PASS: optional identity seed, phrase progression, seed lock and overflow-safe seed setter')
        parameter_root = root.parent / 'my_libraries/parameters/include/parameters'
        parameter_header = (parameter_root / 'Parameter.h').read_text().split('class DataParameterBase :', 1)[1]
        mixer = MIXER_STARTUP_HARNESS.replace('@LAST_GETTER@', next(
          line.strip() for line in parameter_header.splitlines() if 'DataType lastGetterValue =' in line))
        mixer = mixer.replace('@SENT_STATE@', '\n'.join(
          line.strip() for line in parameter_header.splitlines()
          if 'DataType last_sent_value =' in line or 'bool initial_output_pending =' in line))
        mixer = mixer.replace('@UPDATE_MIXER@', extract_block(parameter_header, 'virtual void update_mixer()'))
        mixer = mixer.replace('@SEND_CURRENT@', extract_block(parameter_header, 'virtual void sendCurrentTargetValue()'))
        mixer = mixer.replace('@SET_TARGET@', extract_block(parameter_header, 'virtual void setTargetValueFromNormal(float value, bool force = false)'))
        mixer = mixer.replace('@RANGE_METHODS@', '\n'.join(extract_block(parameter_header, anchor) for anchor in (
          'virtual DataType normalToData(', 'virtual float dataToNormal(',
          'virtual DataType get_effective_minimum_data_value(', 'virtual DataType get_effective_maximum_data_value(',
          'virtual DataType constrainDataToRange(')))
        mixer = mixer.replace('@PROXY@', extract_block((parameter_root / 'ProxyParameter.h').read_text(), 'class ProxyParameter :'))
        mixer = mixer.replace('@LDATA@', extract_block(parameter_header, 'class LDataParameter :'))
        source.write_text(mixer)
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fsanitize=undefined', '-fno-sanitize-recover=all', str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print('PASS: existing settings/pattern values preserved; changed proxies apply zero, centre and maximum without redundant sends')
        baseline = mixer.replace('this->lastGetterValue = *source;', 'this->lastGetterValue = 0;')
        baseline = baseline.replace('this->last_sent_value = *target;', 'this->last_sent_value = 0;')
        baseline = baseline.replace('this->initial_output_pending = *source != *target;', 'this->initial_output_pending = false;')
        source.write_text(baseline)
        subprocess.run([compiler, '-std=c++17', str(source), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert result.returncode != 0 and 'effective == knob && parameter.sends == initial_sends' in result.stderr
        print('PASS: pre-fix mixer guards reproduce skipped initial zero output')


if __name__ == '__main__':
    main()