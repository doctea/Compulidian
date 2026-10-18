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
#include "usb_midi_tx.h"

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
bool g_usbmidi_tx_busy = false;
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
    usb_midi_tx_queue.begin();
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
               "-I" + str(project / "include"), str(native_source),
               "-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
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


if __name__ == "__main__":
    main()