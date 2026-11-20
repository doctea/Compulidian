"""Run with python3 test/test_usb_midi_tx.py; requires a native C++ compiler."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from test_output_batching import extract_block


SYNC_HEADER = r"""
#pragma once
#include <cstdint>
extern uint32_t fake_irq_state;
extern unsigned fake_exception;
extern unsigned fake_core;
inline uint32_t save_and_disable_interrupts() {
  uint32_t previous = fake_irq_state;
  fake_irq_state = 1;
  return previous;
}
inline void restore_interrupts(uint32_t previous) { fake_irq_state = previous; }
inline unsigned __get_current_exception() { return fake_exception; }
inline unsigned get_core_num() { return fake_core; }
"""

QUEUE_HEADER = r"""
#pragma once
#include <array>
#include <cstring>
#include <deque>
#include <cassert>
struct queue_t { std::deque<std::array<uint8_t, 4>> entries; unsigned limit; };
inline void queue_init(queue_t* queue, unsigned size, unsigned limit) {
  assert(size == 4);
  queue->limit = limit;
}
inline bool queue_try_add(queue_t* queue, const void* packet) {
  if (queue->entries.size() == queue->limit) return false;
  std::array<uint8_t, 4> entry;
  std::memcpy(entry.data(), packet, 4);
  queue->entries.push_back(entry);
  return true;
}
inline bool queue_try_remove(queue_t* queue, void* packet) {
  if (queue->entries.empty()) return false;
  std::memcpy(packet, queue->entries.front().data(), 4);
  queue->entries.pop_front();
  return true;
}
"""

HARNESS = r"""
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>
#include "midi_usb/usb_midi_tx.h"

uint32_t fake_irq_state = 0;
unsigned fake_exception = 0;
unsigned fake_core = 0;
void check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": " << expression << '\n';
    std::exit(1);
  }
}
#define CHECK(condition) check(condition, #condition, __LINE__)

struct Transport {
  std::vector<std::array<uint8_t, 4>> sent;
  bool writable = true;
  std::function<void()> interrupt;
  bool writePacket(const uint8_t packet[4]) {
    CHECK(fake_exception == 0);
    CHECK(fake_irq_state == 0);
    CHECK(fake_core == 0);
    if (!writable) return false;
    sent.push_back({packet[0], packet[1], packet[2], packet[3]});
    if (interrupt) {
      auto callback = interrupt;
      interrupt = nullptr;
      callback();
    }
    return true;
  }
} usb_midi;

USBMIDITxQueue usb_midi_tx_queue;
std::atomic<bool> g_usbmidi_tx_busy{false};
struct UnsafeMIDI {
  void sendNoteOn(uint8_t pitch, uint8_t velocity, uint8_t channel) {
    uint8_t packet[4] = {9, static_cast<uint8_t>(0x90 | (channel - 1)), pitch, velocity};
    usb_midi.writePacket(packet);
  }
  void sendNoteOff(uint8_t pitch, uint8_t velocity, uint8_t channel) {
    uint8_t packet[4] = {8, static_cast<uint8_t>(0x80 | (channel - 1)), pitch, velocity};
    usb_midi.writePacket(packet);
  }
} USBMIDI;

constexpr uint8_t GM_CHANNEL_DRUMS = 10;
constexpr int NUM_VOICES = 1;
constexpr int NUM_SAMPLES = 1;
struct Voice { int sample = 0; int sampleindex = 10; int level = 0; } voice[1];
struct Sample { const char* sname = "sample"; uint8_t MIDINOTE = 36; } sample[1];
using sample_t = Sample;
bool is_valid_note(uint8_t note) { return note <= 127; }
const char* get_note_name_c(uint8_t, uint8_t) { return "note"; }
struct Console {
  explicit operator bool() const { return true; }
  template <typename... Args> void printf(const char*, Args...) {
    CHECK(fake_exception == 0);
    CHECK(fake_irq_state == 0);
    CHECK(fake_core == 0);
  }
} Serial;
#define ATOMIC() if (true)
#define USE_TINYUSB
struct Player {
  int numLeds = 6;
  bool enable_volume = true;
  int led_ons = 0;
  int led_offs = 0;
  void LedOn(int) { ++led_ons; }
  void LedOff(int) { ++led_offs; }
};
#define HIGH true
#define LOW false
struct Wrapper {
  bool muted = false;
  bool control_leds_owned = false;
  bool debug = false;
  Player* sw;
  bool gate = false;
  int get_output_number_for_note(uint8_t) { return 0; }
  @VOICE_FOR_NOTE@
  void gateWrite(int, bool value) { gate = value; }
  @NOTE_ON@
  @NOTE_OFF@
};

int main() {
  {
    USBMIDITxQueue queue;
    queue.begin();
    Transport transport;
    fake_exception = 16;
    fake_irq_state = 1;
    queue.enqueue_note(true, 36, 100, 10);
    queue.enqueue_note(false, 36, 0, 10);
    CHECK(fake_irq_state == 1);
    queue.drain(transport, false);
    CHECK(transport.sent.empty());
    fake_exception = 0;
    queue.drain(transport, false);
    CHECK(transport.sent.empty());
    fake_irq_state = 0;
    fake_core = 1;
    queue.drain(transport, false);
    CHECK(transport.sent.empty());
    fake_core = 0;
    queue.drain(transport, true);
    CHECK(transport.sent.empty());
    transport.writable = false;
    queue.drain(transport, false);
    CHECK(transport.sent.empty());
    transport.writable = true;
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 2);
    CHECK((transport.sent[0] == std::array<uint8_t, 4>{9, 0x99, 36, 100}));
    CHECK((transport.sent[1] == std::array<uint8_t, 4>{8, 0x89, 36, 0}));
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 2);
    queue.enqueue_note(true, 128, 100, 1);
    queue.enqueue_note(true, 60, 128, 1);
    queue.enqueue_note(true, 60, 100, 0);
    queue.enqueue_note(true, 60, 100, 17);
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 2);
  }
  {
    USBMIDITxQueue queue;
    queue.begin();
    Transport transport;
    for (unsigned note = 0; note < queue.capacity; ++note)
      queue.enqueue_note(true, note, 100, 1);
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 64);
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 128);
    for (unsigned note = 0; note < queue.capacity; ++note)
      CHECK(transport.sent[note][2] == note);
    transport.interrupt = [&queue]() {
      fake_exception = 16;
      queue.enqueue_note(false, 60, 0, 1);
      fake_exception = 0;
    };
    queue.enqueue_note(true, 60, 100, 1);
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 130);
    CHECK(transport.sent[129][0] == 8);
  }
  {
    USBMIDITxQueue queue;
    queue.begin();
    Transport transport;
    queue.enqueue_note(true, 60, 100, 1);
    transport.writable = false;
    queue.drain(transport, false);
    for (unsigned note = 0; note <= queue.capacity; ++note)
      queue.enqueue_note(false, note % 128, 0, 1);
    CHECK(queue.overflow_count == 1);
    queue.drain(transport, false);
    queue.enqueue_note(true, 70, 100, 1);
    CHECK(transport.sent.empty());
    transport.writable = true;
    transport.interrupt = [&transport]() { transport.writable = false; };
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 1);
    transport.writable = true;
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 16);
    for (unsigned channel = 0; channel < 16; ++channel)
      CHECK((transport.sent[channel] == std::array<uint8_t, 4>{11, static_cast<uint8_t>(0xB0 | channel), 120, 0}));
    CHECK(queue.dropped_count == 131);
    queue.enqueue_note(true, 72, 100, 16);
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 17);
    CHECK(transport.sent.back()[1] == 0x9F);
  }
  {
    USBMIDITxQueue queue;
    queue.begin();
    Transport transport;
    queue.set_transport_ready(false);
    for (unsigned note = 0; note < 512; ++note)
      queue.enqueue_note(true, note % 128, 100, 10);
    queue.drain(transport, false);
    CHECK(transport.sent.empty());
    CHECK(queue.overflow_count == 0 && queue.dropped_count == 512);
    queue.set_transport_ready(true);
    queue.drain(transport, false);
    CHECK(transport.sent.empty());
    queue.enqueue_note(true, 60, 100, 10);
    transport.writable = false;
    queue.drain(transport, false);
    queue.enqueue_note(false, 60, 0, 10);
    queue.set_transport_ready(false);
    CHECK(queue.dropped_count == 514);
    queue.set_transport_ready(false);
    CHECK(queue.dropped_count == 514);
    queue.set_transport_ready(true);
    transport.writable = true;
    queue.drain(transport, false);
    CHECK(transport.sent.empty());
    queue.enqueue_note(true, 72, 100, 10);
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 1 && transport.sent[0][2] == 72);

    transport.writable = false;
    for (unsigned note = 0; note <= queue.capacity; ++note)
      queue.enqueue_note(true, note % 128, 100, 10);
    queue.drain(transport, false);
    queue.set_transport_ready(false);
    queue.set_transport_ready(true);
    transport.writable = true;
    queue.enqueue_note(true, 74, 100, 10);
    queue.drain(transport, false);
    CHECK(transport.sent.size() == 2 && transport.sent[1][2] == 74);
  }
  {
    usb_midi_tx_queue.begin();
    usb_midi_tx_queue.set_transport_ready(false);
    Player player;
    Wrapper wrapper;
    wrapper.sw = &player;
    wrapper.debug = true;
    fake_exception = 16;
    wrapper.sendNoteOn(36, 96, 10);
    CHECK(usb_midi.sent.empty());
    CHECK(wrapper.gate);
    CHECK(player.led_ons == 1);
    CHECK(voice[0].sampleindex == 0);
    CHECK(voice[0].level == 96);
    wrapper.sendNoteOff(36, 0, 10);
    CHECK(!wrapper.gate);
    CHECK(player.led_offs == 1);
    CHECK(usb_midi.sent.empty());
    voice[0].sample = -1;
    CHECK(wrapper.get_voice_number_for_note(36) == -1);
    voice[0].sample = NUM_SAMPLES;
    CHECK(wrapper.get_voice_number_for_note(36) == -1);
    voice[0].sample = 0;
    fake_exception = 0;
    usb_midi_tx_queue.drain(usb_midi, false);
    CHECK(usb_midi.sent.empty());
    usb_midi_tx_queue.set_transport_ready(true);
    wrapper.sendNoteOn(36, 96, 10);
    wrapper.sendNoteOff(36, 0, 10);
    usb_midi_tx_queue.drain(usb_midi, false);
    CHECK(usb_midi.sent.size() == 2);
    wrapper.muted = true;
    wrapper.sendNoteOn(38, 100, 10);
    usb_midi_tx_queue.drain(usb_midi, false);
    CHECK(usb_midi.sent.size() == 2);
    g_usbmidi_tx_busy = true;
    wrapper.muted = false;
    wrapper.sendNoteOn(38, 100, 10);
    usb_midi_tx_queue.drain(usb_midi, g_usbmidi_tx_busy);
    CHECK(usb_midi.sent.size() == 2);
    g_usbmidi_tx_busy = false;
    usb_midi_tx_queue.drain(usb_midi, g_usbmidi_tx_busy);
    CHECK(usb_midi.sent.size() == 3);
  }
  std::cout << "PASS: ISR/masked/core guards, no ISR console writes, local triggers, FIFO order, backpressure, SysEx, budget, overflow recovery\n";
}
"""


def make_harness(wrapper):
  source = HARNESS.replace("@VOICE_FOR_NOTE@", extract_block(wrapper, "int8_t get_voice_number_for_note("))
  source = source.replace("@NOTE_ON@", extract_block(wrapper, "virtual void sendNoteOn("))
  return source.replace("@NOTE_OFF@", extract_block(wrapper, "virtual void sendNoteOff("))


SYSEX_HARNESS = r"""
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <vector>

#define Serial_println(...) do { if (Serial) Serial.println(__VA_ARGS__); } while (0)
#define Serial_printf(...) do { if (Serial) Serial.printf(__VA_ARGS__); } while (0)

constexpr bool SYSEX_DEBUG = true;
constexpr unsigned SYSEX_FRAME_BYTES = 6;
constexpr unsigned SYSEX_MAX_PAYLOAD = 128;
constexpr uint8_t SYSEX_MFR_ID = 0x7d;
constexpr uint8_t SYSEX_DEVICE_ID = 1;
constexpr uint8_t SYSEX_PROTO_VER = 1;
constexpr uint8_t SYSEX_TYPE_CONFIG_RESP = 2;
constexpr uint8_t SYSEX_TYPE_NACK = 3;
constexpr uint8_t SYSEX_STATUS_OK = 0;
constexpr uint8_t SYSEX_NACK_NOT_READY = 5;
constexpr unsigned SYSEX_CONFIG_PAYLOAD_LEN = 74;
struct Settings { uint8_t active_bank = 0; } current_settings;
uint32_t now_ms = 0;
unsigned task_count = 0;
unsigned delay_count = 0;
unsigned file_work = 0;
bool connected = false;
bool writable = true;
bool g_usbmidi_tx_busy = false;
std::function<void()> on_task;
std::function<void()> on_delay;
std::function<void()> on_write;
struct Device {
  bool is_mounted = false;
  bool is_suspended = false;
  bool mounted() const { return is_mounted; }
  bool suspended() const { return is_suspended; }
} TinyUSBDevice;
bool tud_connected() { return connected; }
uint32_t millis() { return now_ms++; }
void delay(unsigned ms) {
  now_ms += ms;
  ++delay_count;
  if (on_delay) on_delay();
}
void TinyUSB_Device_Task() {
  ++task_count;
  if (on_task) on_task();
}
struct Console {
  explicit operator bool() const { return true; }
  void println(const char*) {}
  template <typename... Args> void printf(const char*, Args...) {}
} Serial;
struct Transport {
  unsigned attempts = 0;
  std::vector<uint8_t> sent;
  size_t write(uint8_t byte) {
    assert(g_usbmidi_tx_busy);
    ++attempts;
    if (!writable || !connected || !TinyUSBDevice.mounted()) return 0;
    sent.push_back(byte);
    if (on_write) on_write();
    return 1;
  }
} usb_midi;
bool build_config_payload(uint8_t* payload, uint16_t* length) {
  ++file_work;
  *length = SYSEX_CONFIG_PAYLOAD_LEN;
  memset(payload, 42, *length);
  return true;
}
@SYSEX_FUNCTIONS@
@PENDING_TYPES@
std::deque<PendingSysexItem> pending;
bool dequeue_pending_sysex(PendingSysexItem* item) {
  if (pending.empty()) return false;
  *item = pending.front();
  pending.pop_front();
  return true;
}
static void send_clock_status_sysex(uint8_t) { ++file_work; }
static void send_ack(uint8_t) { ++file_work; }
static void send_bank_info_sysex(uint8_t, uint8_t) { ++file_work; }
static void send_bank_chunk_sysex(uint8_t, uint8_t, uint16_t) { ++file_work; }
static void send_slots_info_sysex(uint8_t) { ++file_work; }
static void send_samples_info_sysex(uint8_t) { ++file_work; }
static void send_samples_chunk_sysex(uint8_t, uint16_t) { ++file_work; }
static void send_sample_meta_info_sysex(uint8_t) { ++file_work; }
static void send_sample_meta_chunk_sysex(uint8_t, uint16_t) { ++file_work; }
struct Reboot { void rebootToBootloader() { assert(false); } } rp2040;
@PROCESS_PENDING@
void reset(bool online = false) {
  now_ms = task_count = delay_count = file_work = 0;
  connected = online;
  writable = true;
  TinyUSBDevice.is_mounted = online;
  TinyUSBDevice.is_suspended = false;
  usb_midi.attempts = 0;
  usb_midi.sent.clear();
  on_task = on_delay = on_write = nullptr;
  assert(!g_usbmidi_tx_busy);
}
int main() {
  reset();
  for (uint8_t action : {PENDING_SEND_CONFIG, PENDING_SEND_BANK_CHUNK,
      PENDING_SEND_CLOCK_STATUS, PENDING_SEND_SLOTS_INFO,
      PENDING_SEND_SAMPLES_CHUNK, PENDING_SEND_SAMPLE_META_CHUNK, PENDING_SEND_CONFIG})
    pending.push_back({action, 0, 0, 0, 0});
  process_pending_sysex_responses();
  assert(pending.size() == 1 && file_work == 0 && usb_midi.attempts == 0);
  process_pending_sysex_responses();
  assert(pending.empty() && file_work == 0 && now_ms < 10);

  reset(true);
  pending.push_back({PENDING_SEND_BANK_CHUNK, 0, 1, 0, 0});
  process_pending_sysex_responses();
  assert(pending.empty() && file_work == 1);
  TinyUSBDevice.is_suspended = true;
  pending.push_back({PENDING_SEND_BANK_CHUNK, 0, 1, 0, 0});
  process_pending_sysex_responses();
  assert(pending.empty() && file_work == 1);

  // A physical bank change queues this config response, including its NACK on failure.
  reset();
  send_config_sysex(0);
  assert(now_ms < 10 && delay_count == 0 && task_count == 0);
  assert(usb_midi.attempts == 0 && !g_usbmidi_tx_busy);

  reset(true);
  send_config_sysex(17);
  std::vector<uint8_t> expected = {0xf0, SYSEX_MFR_ID, SYSEX_DEVICE_ID,
    SYSEX_PROTO_VER, SYSEX_TYPE_CONFIG_RESP, 17, SYSEX_STATUS_OK};
  expected.insert(expected.end(), SYSEX_CONFIG_PAYLOAD_LEN, 42);
  expected.push_back(0xf7);
  assert(usb_midi.sent == expected && !g_usbmidi_tx_busy);

  reset(true);
  writable = false;
  on_task = [] { writable = true; };
  send_config_sysex(17);
  assert(task_count == 1 && usb_midi.sent == expected && !g_usbmidi_tx_busy);

  reset(true);
  writable = false;
  on_task = [] { connected = false; TinyUSBDevice.is_mounted = false; };
  send_config_sysex(0);
  assert(now_ms < 10 && task_count == 1 && delay_count == 0);
  assert(usb_midi.attempts == 1 && !g_usbmidi_tx_busy);

  reset(true);
  on_write = [] { if (usb_midi.sent.size() == 4) connected = false; };
  send_config_sysex(0);
  assert(now_ms < 10 && usb_midi.sent.size() == 4 && usb_midi.attempts == 4);
  assert(!g_usbmidi_tx_busy);

  reset(true);
  TinyUSBDevice.is_suspended = true;
  send_config_sysex(0);
  assert(now_ms < 10 && usb_midi.attempts == 0 && delay_count == 0);

  reset(true);
  writable = false;
  on_task = [] { TinyUSBDevice.is_suspended = true; };
  send_config_sysex(0);
  assert(now_ms < 10 && task_count == 1 && usb_midi.attempts == 1);
  assert(!g_usbmidi_tx_busy);

  reset(true);
  TinyUSBDevice.is_mounted = false;
  on_delay = [] { if (delay_count == 2) TinyUSBDevice.is_mounted = true; };
  send_config_sysex(17);
  assert(delay_count == 2 && usb_midi.sent == expected);

  reset(true);
  TinyUSBDevice.is_mounted = false;
  on_delay = [] { connected = false; };
  send_config_sysex(0);
  assert(now_ms < 10 && delay_count == 1 && usb_midi.attempts == 0);

  reset(true);
  writable = false;
  assert(!send_sysex_frame(SYSEX_TYPE_CONFIG_RESP, 0, SYSEX_STATUS_OK, nullptr, 0));
  assert(now_ms > 2000 && now_ms < 2010 && !g_usbmidi_tx_busy);
  std::cout << "PASS: offline bank notification, connected SysEx bytes, backpressure, disconnect/suspend abort, enumeration grace and congestion timeout\n";
}
"""


def make_sysex_harness(configurator):
    functions = "\n".join(extract_block(configurator, anchor) for anchor in (
        "static bool midi_write_byte_blocking_(",
        "static bool send_sysex_bytes_reliable_(",
        "static bool send_sysex_frame(",
        "static void send_nack(",
        "static void send_config_sysex(uint8_t req_id) {",
    ))
    source = SYSEX_HARNESS.replace("@SYSEX_FUNCTIONS@", functions)
    source = source.replace("@PENDING_TYPES@", "\n".join(extract_block(configurator, anchor) + ";" for anchor in (
        "enum PendingSysexAction", "struct PendingSysexItem")))
    return source.replace("@PROCESS_PENDING@", extract_block(configurator, "static void process_pending_sysex_responses()"))


AUDIO_STARTUP_HARNESS = r"""
#include <atomic>
#include <cassert>
std::atomic<bool> started{false};
unsigned waits = 0;
void delay(unsigned ms) {
  assert(ms == 1);
  if (++waits == 3) started = true;
}
struct Player {
  unsigned runs = 0;
  void EnableNormalisationProbe() {}
  void Run() { assert(started); ++runs; }
} sw;
@SETUP1@
int main() {
  setup1();
  assert(waits == 3 && sw.runs == 1);
  setup1();
  assert(waits == 3 && sw.runs == 2);
}
"""


def main():
    project = Path(__file__).resolve().parents[1]
    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        raise RuntimeError("A native C++ compiler is required; use WSL.")
    wrapper = (project / "include/workshop_output.h").read_text()
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        (directory / "hardware").mkdir()
        (directory / "pico/util").mkdir(parents=True)
        (directory / "hardware/sync.h").write_text(SYNC_HEADER)
        (directory / "pico/util/queue.h").write_text(QUEUE_HEADER)
        native_source = directory / "test.cpp"
        native_source.write_text(make_harness(wrapper))
        executable = directory / "test"
        command = [compiler, "-std=c++11", "-I" + str(directory),
               "-I" + str(project / "include"),
               "-I" + str(project.parent / "my_libraries/midihelpers/include"), str(native_source),
               "-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
        audio = (project / "src/audio/audio.cpp").read_text()
        native_source.write_text(AUDIO_STARTUP_HARNESS.replace("@SETUP1@", extract_block(audio, "void setup1()")))
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
        print("PASS: core 1 waits only for initialized audio data, without serial access or fixed startup delay")
        checkpoint = project / "firmware/checkpoints/2026-10-16-usb-midi-working/working-state.zip"
        if checkpoint.is_file():
          with zipfile.ZipFile(checkpoint) as archive:
            baseline = archive.read("Compulidian/include/workshop_output.h").decode()
          native_source.write_text(make_harness(baseline))
          subprocess.run(command, check=True)
          result = subprocess.run([str(executable)], stdout=subprocess.PIPE,
                      stderr=subprocess.PIPE, text=True)
          if result.returncode == 0 or "fake_exception == 0" not in result.stderr:
            raise RuntimeError("Checkpoint did not fail the expected ISR-transmit guard: " + result.stderr)
          print("checkpoint: expected failure confirms direct USB transmission from ISR")
        native_source.write_text(make_sysex_harness((project / "include/configurator.h").read_text()))
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()