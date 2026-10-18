"""Native callback regression checks; run with python3 test/test_output_batching.py."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile


def extract_block(source, anchor):
    start = source.index(anchor)
    opening = source.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (source[position] == "{") - (source[position] == "}")
        position += 1
    return source[start:position]


HARNESS = r"""
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define Debug_printf(...) ((void)0)
#define Debug_println(...) ((void)0)
#define __not_in_flash_func(name) name
#define MAX_OUTPUT_LABEL 40
constexpr int8_t NOTE_OFF = -1;
constexpr uint32_t PPQN = 24;
struct Console {
  explicit operator bool() const { return false; }
  template <typename... Args> void printf(const char*, Args...) {}
} Serial;
bool is_valid_note(int8_t note) { return note >= 0; }

void check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": " << expression << '\n';
    std::exit(1);
  }
}
#define CHECK(condition) check(condition, #condition, __LINE__)

struct ISequencerEventReceiver {
  virtual void receive_event(int_fast8_t, int_fast8_t, int_fast8_t, int_fast8_t) = 0;
};

@BASE_OUTPUT@;

struct Target {
  int note_ons = 0;
  int note_offs = 0;
  void sendNoteOn(int8_t, int8_t, int8_t) { ++note_ons; }
  void sendNoteOff(int8_t, int8_t, int8_t) { ++note_offs; }
};
using IMIDINoteAndCCTarget = Target;

@MIDI_OUTPUT@;

struct CountOutput : MIDIBaseOutput {
  std::vector<BaseOutput*>* nodes;
  uint8_t start_count_at = 0;
  int finish_count_at = 2;
  int last_count = -1;
  CountOutput(Target* target, std::vector<BaseOutput*>* outputs)
    : MIDIBaseOutput("Count", target, 60), nodes(outputs) {}
  @COUNT_METHOD@
  int_fast8_t get_note_number() override {
    last_count = get_note_number_count();
    return 60 + last_count;
  }
};

struct Processor {
  std::vector<BaseOutput*>* nodes;
  bool enabled = true;
  int calls = 0;
  bool is_enabled() { return enabled; }
  void process() {
    ++calls;
    @PROCESS_BODY@
  }
};

struct Event {
  BaseOutput* output;
  bool note_on;
};

struct Sequencer {
  bool running = true;
  std::vector<Event> tick_events;
  std::vector<Event> shuffle_events;
  uint32_t last_step = 0;
  int last_track = -1;
  bool is_running() { return running; }
  void dispatch(std::vector<Event>& events) {
    for (const auto& event : events) {
      event.output->receive_event(event.note_on, !event.note_on, 60, 100);
    }
    events.clear();
  }
  void on_tick(uint32_t) { dispatch(tick_events); }
  void on_step_shuffled(int track, uint32_t step) {
    last_track = track;
    last_step = step;
    dispatch(shuffle_events);
  }
};

struct Parameters { void tick_sh() {} };
Parameters parameters;
Parameters* parameter_manager = &parameters;
Processor* output_processor;
Sequencer* sequencer;
uint32_t ticks = 0;
int debug_step_offset = 0;
bool is_restart_on_next_bar() { return false; }
bool is_bpm_on_bar(uint32_t tick) { return tick % 96 == 0; }
bool is_bpm_on_sixteenth(uint32_t tick) { return tick % 6 == 0; }
void global_on_restart() {}
void set_restart_on_next_bar(bool) {}

@PENDING_DECLARATION@
@END_CALLBACK@
@DO_TICK@
#ifdef ENABLE_SHUFFLE
@SHUFFLED_CALLBACK@
#endif

struct Fixture {
  Target target;
  MIDIBaseOutput kick{"Kick", &target, 36};
  MIDIBaseOutput snare{"Snare", &target, 38};
  std::vector<BaseOutput*> nodes;
  CountOutput count{&target, &nodes};
  Processor processor;
  Sequencer sequence;
  Fixture() {
    nodes = {&kick, &snare, &count};
    processor.nodes = &nodes;
    output_processor = &processor;
    sequencer = &sequence;
    output_processing_pending = false;
    ticks = 0;
    debug_step_offset = 0;
    for (auto* output : nodes) output->reset();
  }
};

int main() {
  {
    Fixture fixture;
    fixture.sequence.tick_events = {{&fixture.kick, true}, {&fixture.count, true}};
    do_tick(0);
#ifdef ENABLE_SHUFFLE
    fixture.sequence.shuffle_events = {{&fixture.snare, true}};
    shuffled_step_callback(0);
#endif
    process_pending_outputs(0);
#ifdef ENABLE_SHUFFLE
    CHECK(fixture.count.last_count == 1);
    CHECK(fixture.target.note_ons == 3);
#else
    CHECK(fixture.count.last_count == 0);
    CHECK(fixture.target.note_ons == 2);
#endif
    CHECK(fixture.processor.calls == 1);
    process_pending_outputs(1);
    process_pending_outputs(2);
    process_pending_outputs(3);
    CHECK(fixture.processor.calls == 1);
    CHECK(ticks == 0);
    do_tick(1);
    process_pending_outputs(4);
    CHECK(fixture.processor.calls == 1);
    CHECK(fixture.target.note_offs == 0);
  }
#ifdef ENABLE_SHUFFLE
  {
    Fixture fixture;
    do_tick(1);
    process_pending_outputs(4);
    CHECK(fixture.processor.calls == 0);
    fixture.sequence.shuffle_events = {{&fixture.snare, true}};
    debug_step_offset = 3;
    shuffled_step_callback(7);
    CHECK(fixture.target.note_ons == 0);
    process_pending_outputs(5);
    CHECK(fixture.target.note_ons == 1);
    CHECK(fixture.processor.calls == 1);
    CHECK(fixture.sequence.last_track == 0);
    CHECK(fixture.sequence.last_step == 10);
  }
  {
    Fixture fixture;
    fixture.sequence.shuffle_events = {{&fixture.kick, true}};
    shuffled_step_callback(0);
    fixture.sequence.shuffle_events = {{&fixture.snare, true}, {&fixture.count, true}};
    shuffled_step_callback(0);
    process_pending_outputs(0);
    CHECK(fixture.processor.calls == 1);
    CHECK(fixture.count.last_count == 1);
    CHECK(fixture.target.note_ons == 3);
  }
#endif
  {
    Fixture fixture;
    fixture.sequence.tick_events = {{&fixture.kick, true}};
    do_tick(0);
    process_pending_outputs(0);
    fixture.sequence.tick_events = {{&fixture.kick, false}};
    do_tick(6);
    CHECK(fixture.target.note_offs == 0);
    process_pending_outputs(24);
    CHECK(fixture.target.note_ons == 1);
    CHECK(fixture.target.note_offs == 1);
    CHECK(fixture.kick.get_last_note_number() == NOTE_OFF);
    process_pending_outputs(25);
    CHECK(fixture.target.note_offs == 1);
    fixture.sequence.tick_events = {{&fixture.kick, true}};
    do_tick(0);
    process_pending_outputs(0);
    CHECK(fixture.target.note_ons == 2);
    CHECK(fixture.processor.calls == 3);
  }
  {
    Fixture fixture;
    fixture.processor.enabled = false;
    fixture.sequence.tick_events = {{&fixture.kick, true}};
    do_tick(0);
    process_pending_outputs(0);
    CHECK(fixture.processor.calls == 0);
    CHECK(!output_processing_pending);
    fixture.processor.enabled = true;
    process_pending_outputs(1);
    CHECK(fixture.processor.calls == 0);
    do_tick(6);
    process_pending_outputs(24);
    CHECK(fixture.processor.calls == 1);
    CHECK(fixture.target.note_ons == 1);
    fixture.sequence.running = false;
    fixture.sequence.tick_events = {{&fixture.snare, true}};
    do_tick(12);
    process_pending_outputs(48);
    CHECK(fixture.processor.calls == 2);
    CHECK(fixture.target.note_ons == 1);
  }
  std::cout << "PASS: combined counts, same-tick dispatch, empty ticks, offsets, note-offs, restart, enable/running gates\n";
}
"""


def make_harness(main_source, seqlib):
    base = (seqlib / "include/outputs/base_outputs.h").read_text()
    midi = (seqlib / "include/outputs/output_midi.h").read_text()
    processor = (seqlib / "include/outputs/output_processor.h").read_text()
    count = (seqlib / "include/outputs/output_midi_triggercount.h").read_text()
    process = extract_block(processor, "virtual void process() {")
    pending = re.search(r"static bool output_processing_pending\s*=\s*false;", main_source)
    if pending:
        end_callback = extract_block(main_source, "void process_pending_outputs(")
        assert "uClock.setOnOutputPPQNEnd(process_pending_outputs);" in main_source
    else:
        end_callback = "void process_pending_outputs(uint32_t) {}"
    substitutions = {
        "BASE_OUTPUT": extract_block(base, "class BaseOutput :"),
        "MIDI_OUTPUT": extract_block(midi, "class MIDIBaseOutput :"),
        "COUNT_METHOD": extract_block(count, "virtual int_fast8_t get_note_number_count()"),
        "PROCESS_BODY": process[process.index("{") + 1:-1],
        "PENDING_DECLARATION": pending.group() if pending else "static bool output_processing_pending = false;",
        "END_CALLBACK": end_callback,
        "DO_TICK": extract_block(main_source, "void __not_in_flash_func(do_tick)"),
        "SHUFFLED_CALLBACK": extract_block(main_source, "void shuffled_step_callback("),
    }
    result = HARNESS
    for key, value in substitutions.items():
        result = result.replace("@" + key + "@", value)
    return result


def run_harness(source, compiler, directory, label, shuffle):
    native_source = directory / (label + ".cpp")
    executable = directory / label
    native_source.write_text(source)
    command = [compiler, "-std=c++11", "-DUSE_UCLOCK", "-DENABLE_EUCLIDIAN"]
    if shuffle:
        command.append("-DENABLE_SHUFFLE")
    command.extend([str(native_source), "-o", str(executable)])
    subprocess.run(command, check=True)
    return subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-baseline", action="store_true")
    options = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    seqlib = project.parent / "my_libraries/seqlib"
    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        parser.error("A native C++ compiler (g++ or clang++) is required; use WSL.")
    current = make_harness((project / "src/main.cpp").read_text(), seqlib)
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        for shuffle in (True, False):
            label = "shuffled" if shuffle else "unshuffled"
            result = run_harness(current, compiler, directory, label, shuffle)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            print(label + ": " + result.stdout.strip())
        if options.check_baseline:
            archive = project / "firmware/checkpoints/2026-10-16-usb-midi-working/working-state.zip"
            with zipfile.ZipFile(archive) as snapshot:
                baseline = make_harness(snapshot.read("Compulidian/src/main.cpp").decode(), seqlib)
            result = run_harness(baseline, compiler, directory, "baseline", True)
            if result.returncode == 0:
                raise RuntimeError("Baseline unexpectedly passed the aggregation regression check")
            if "fixture.count.last_count == 1" not in result.stderr:
                raise RuntimeError("Baseline failed for an unexpected reason: " + result.stderr)
            print("checkpoint: expected failure confirms split-batch count-derived pitch")


if __name__ == "__main__":
    main()