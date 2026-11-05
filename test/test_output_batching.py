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
#include <algorithm>
#include <new>

#define Debug_printf(...) ((void)0)
#define Debug_println(...) ((void)0)
#define __not_in_flash_func(name) name
#define MAX_OUTPUT_LABEL 40
constexpr int8_t NOTE_OFF = -1;
constexpr uint32_t PPQN = 24;
struct Console {
  explicit operator bool() const { return false; }
  template <typename... Args> void printf(const char*, Args...) {}
  void flush() {}
  template <typename Value> void println(Value) {}
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

@PATTERN_DRIVER@

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
  EuclidianPattern* pattern = nullptr;
  unsigned int get_number_patterns() { return pattern ? 1 : 0; }
  EuclidianPattern* get_pattern(unsigned int) { return pattern; }
  int get_euclidian_seed() { return 0; }
  bool is_euclidian_seed_lock() { return false; }
  bool is_mutate_enabled() { return true; }
  int get_mutation_count() { return 3; }
  int get_effective_mutation_count() { return 3; }
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
  void on_tick(uint32_t tick) {
    dispatch(tick_events);
    if (pattern) {
#ifndef ENABLE_SHUFFLE
      if (tick % 6 == 0) pattern->process_step(tick / 6);
#endif
      pattern->process_tick(tick);
    }
  }
  void on_step_shuffled(int track, uint32_t step) {
    last_track = track;
    last_step = step;
    dispatch(shuffle_events);
    if (pattern) pattern->process_step_shuffled(step);
  }
};

struct Parameters { void tick_sh() {} };
Parameters parameters;
Parameters* parameter_manager = &parameters;
Processor* output_processor;
Sequencer* sequencer;
using EuclidianSequencer = Sequencer;
uint32_t ticks = 0;
bool playing = true;
int clock_mode = 0;
float get_bpm() { return 120.0f; }
struct ComputerCard { enum Knob { Main, X, Y }; };
struct Hardware {
  bool SwitchReady() { return true; }
  int KnobVal(ComputerCard::Knob) { return 0; }
  int CVIn(int) { return 0; }
  int AudioIn(int) { return 0; }
} sw;
int debug_step_offset = 0;
bool is_restart_on_next_bar() { return false; }
bool is_bpm_on_bar(uint32_t tick) { return tick % 96 == 0; }
bool is_bpm_on_sixteenth(uint32_t tick) { return tick % 6 == 0; }
void global_on_restart() {}
void set_restart_on_next_bar(bool) {}

@STATE_REPORT@
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
  }
};

int main() {
  {
    alignas(MIDIBaseOutput) unsigned char storage[sizeof(MIDIBaseOutput)];
    std::memset(storage, 0x01, sizeof(storage));
    Target target;
    auto* output = new (storage) MIDIBaseOutput("Untouched", &target, 36);
    for (int pass = 0; pass < 3; ++pass) output->process();
    CHECK(target.note_ons == 0);
    CHECK(target.note_offs == 0);
    CHECK(output->get_last_note_number() == NOTE_OFF);
    CHECK(!output->should_go_on() && !output->should_go_off());
    output->receive_event(1, 0, 36, 100);
    output->process();
    CHECK(target.note_ons == 1 && target.note_offs == 0);
    output->receive_event(0, 1, 36, 0);
    output->process();
    CHECK(target.note_ons == 1 && target.note_offs == 1);
    CHECK(output->get_last_note_number() == NOTE_OFF);
    output->~MIDIBaseOutput();
  }
  for (bool start_empty : {true, false}) {
    Fixture fixture;
    EuclidianPattern pattern;
    pattern.output = &fixture.kick;
    fixture.sequence.pattern = &pattern;
    pattern.make_euclid();
    CHECK(pattern.query_note_on_for_step(0));
    if (start_empty) {
      pattern.density = 0;
      pattern.make_euclid();
    }
    for (uint32_t tick = 0; tick <= 12; ++tick) {
#ifdef ENABLE_SHUFFLE
      if (tick % 6 == 0) shuffled_step_callback(tick / 6);
#endif
      do_tick(tick);
      process_pending_outputs(tick);
      if (tick == 0 && !start_empty) {
        CHECK(fixture.target.note_ons == 1);
        CHECK(pattern.note_held);
        CHECK(fixture.target.note_offs == 0);
        pattern.density = 0;
        pattern.mark_dirty();
        pattern.do_deferred_recompute();
      }
      if (tick >= 2) CHECK(!pattern.note_held);
    }
    print_sequencer_state();
    CHECK(!pattern.note_held);
    CHECK(fixture.kick.get_last_note_number() == NOTE_OFF);
    CHECK(fixture.target.note_ons == (start_empty ? 0 : 1));
    CHECK(fixture.target.note_offs == (start_empty ? 0 : 1));
  }
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


PATTERN_DRIVER = r"""
extern uint32_t ticks;
constexpr int TICKS_PER_STEP = 6, STEPS_PER_BAR = 16, STEPS_PER_BEAT = 4;
constexpr int DEFAULT_VELOCITY = 127;
constexpr float MINIMUM_DENSITY = 0.0f;
constexpr float DEFAULT_DENSITY = 0.6666f;
#define SEQUENCE_LENGTH_STEPS 16
#define FLASHMEM
#define BPM_GLOBAL_STEP_FROM_TICKS(value) ((value) / TICKS_PER_STEP)
#define BPM_CURRENT_STEP_OF_SONG (ticks / TICKS_PER_STEP)
using std::max;
using std::min;
template<class Value, class Lower, class Upper>
Value constrain(Value value, Lower lower, Upper upper) {
  return max(Value(lower), min(value, Value(upper)));
}
uint32_t millis() { return ticks; }
struct Clock {
  int getOutputPPQN() { return PPQN; }
  int getShuffleOffset(int, int) { return 0; }
} uClock;
@ARGUMENTS@;
struct SimplePattern {
  bool debug = false, note_held = false, locked = false;
  uint32_t triggered_on_step = -1, triggered_on_tick = -1;
  int16_t current_duration = PPQN;
  bool shuffled_step_end_pending = false;
  uint32_t shuffled_step_started_at_tick = 0;
  int shuffled_step_end_step = 0;
  MIDIBaseOutput* output = nullptr;
  struct Event { short note = 0, velocity = 0, channel = 0; } events[16];
  uint32_t last_updated_at = 0;
  int get_effective_steps() { return 16; }
  bool is_locked() { return locked; }
  bool is_shuffled() { return true; }
  int get_shuffle_track() { return 0; }
  int get_velocity() { return 100; }
  const char* get_output_label() { return "Kick"; }
  MIDIBaseOutput* get_output() { return output; }
  virtual int8_t get_tick_duration() { return 2; }
  bool query_note_on_for_step(int step) { return query_note_on_for_tick(step * TICKS_PER_STEP); }
  bool query_note_off_for_step(int step) { return query_note_off_for_tick(step * TICKS_PER_STEP); }
  @PATTERN_METHODS@
  @TRIGGER_ON@
  @TRIGGER_OFF@
};
struct EuclidianPattern : SimplePattern {
  bool initialised = false, needs_recompute = false;
  int maximum_steps = 16;
  float density = DEFAULT_DENSITY;
  arguments_t arguments, used_arguments, last_arguments, last_post_arguments;
  EuclidianPattern() {
    used_arguments.pulses = 16;
    used_arguments.rotation = 0;
    used_arguments.duration = 2;
  }
  float get_global_density() { return density; }
  @MAKE_EUCLID@
  @MARK_DIRTY@
  @DEFERRED_RECOMPUTE@
  @GET_DURATION@
};
"""


SHUFFLE_TRANSITION_HARNESS = r"""
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "uClock.h"

using Clock = umodular::clock::uClockClass;
uint32_t fake_micros = 0;
uint32_t micros() { return fake_micros; }
uint32_t millis() { return fake_micros / 1000; }
Clock* callback_clock = nullptr;
std::vector<uint32_t> fired_steps;
std::vector<uint32_t> fired_ticks;
void record_step(uint32_t step) {
  fired_steps.push_back(step);
  fired_ticks.push_back(uint32_t(callback_clock->tick));
}
void run_until(Clock& clock, uint32_t end_tick) {
  while (clock.tick < end_tick) {
    fake_micros += 1000;
    clock.handleInternalClock();
  }
}
bool check_ticks(const char* label, std::vector<uint32_t> expected, uint32_t first_step = 0) {
  bool ok = fired_ticks == expected;
  for (size_t index = 0; index < fired_steps.size(); ++index)
    ok = ok && fired_steps[index] == first_step + index;
  if (!ok) {
    std::cerr << label << ": ticks";
    for (auto tick : fired_ticks) std::cerr << ' ' << tick;
    std::cerr << " (expected";
    for (auto tick : expected) std::cerr << ' ' << tick;
    std::cerr << ")\n";
  }
  fired_steps.clear();
  fired_ticks.clear();
  return ok;
}
int main() {
  bool ok = true;
  {
    Clock clock;
    callback_clock = &clock;
    clock.setOnStep(record_step);
    clock.init();
    int8_t pattern[] = {0, -8};
    clock.setShuffleTemplate(pattern, 2);
    clock.setShuffle(true);
    clock.start();
    run_until(clock, 18);
    clock.setShuffle(false);
    run_until(clock, 73);
    ok = check_ticks("disable after early step", {0, 16, 48, 72}) && ok;
  }
  {
    Clock clock;
    callback_clock = &clock;
    clock.setOnStep(record_step);
    clock.init();
    int8_t pattern[] = {0, -8};
    clock.setShuffleTemplate(pattern, 2);
    clock.start();
    run_until(clock, 20);
    clock.setShuffle(true);
    run_until(clock, 73);
    ok = check_ticks("enable after early target", {0, 24, 48, 64}) && ok;
  }
  for (uint32_t change_tick = 1; change_tick <= 96; ++change_tick) {
    for (int scenario = 0; scenario < 3; ++scenario) {
      Clock clock;
      callback_clock = &clock;
      clock.setOnStep(record_step);
      clock.init();
      const int8_t offset = scenario == 2 ? 8 : -8;
      int8_t pattern[] = {0, offset};
      clock.setShuffleTemplate(pattern, 2);
      clock.setShuffle(scenario == 1);
      clock.start();
      run_until(clock, change_tick);
      clock.setShuffle(scenario != 1);
      run_until(clock, 193);
      std::vector<uint32_t> expected;
      for (int32_t step = 0; step <= 8; ++step) {
        const int32_t nominal = step * 24;
        const int32_t shifted = nominal + (step % 2 ? offset : 0);
        if (scenario == 1) {
          expected.push_back(shifted < int32_t(change_tick) ? shifted : nominal);
        } else {
          expected.push_back(nominal < int32_t(change_tick)
            || shifted < int32_t(change_tick) ? nominal : shifted);
        }
      }
      ok = check_ticks("shuffle transition phase sweep", expected) && ok;
    }
  }
  {
    Clock clock;
    callback_clock = &clock;
    clock.setOnStep(record_step);
    clock.init();
    int8_t pattern[] = {0, -8};
    clock.setShuffleTemplate(pattern, 2);
    clock.setShuffle(true);
    clock.start();
    run_until(clock, 18);
    clock.setShuffle(false);
    clock.setShuffle(true);
    run_until(clock, 73);
    ok = check_ticks("early-step off/on transition", {0, 16, 48, 64}) && ok;
  }
  {
    Clock clock;
    callback_clock = &clock;
    clock.setOnStep(record_step);
    clock.init();
    int8_t pattern[] = {8, -8};
    clock.setShuffleTemplate(pattern, 2);
    clock.setShuffle(true);
    clock.start();
    run_until(clock, 10);
    int8_t changed[] = {8, 8};
    clock.setShuffleTemplate(changed, 2);
    run_until(clock, 60);
    ok = check_ticks("latched shuffle offset", {8, 16, 56}) && ok;
  }
  for (int scenario = 0; scenario < 3; ++scenario) {
    Clock clock;
    callback_clock = &clock;
    clock.setOnStep(record_step);
    clock.init();
    int8_t pattern[] = {0, -8};
    clock.setShuffleTemplate(pattern, 2);
    clock.setShuffle(scenario == 1);
    clock.start();
    clock.tick = 0xfffffff0u;
    clock.tracks[0].step_counter = 178956970;
    for (int elapsed = 0; elapsed < 80; ++elapsed) {
      if (scenario == 0 && elapsed == 20) clock.setShuffle(true);
      if (scenario == 1 && elapsed == 18) clock.setShuffle(false);
      fake_micros += 1000;
      clock.handleInternalClock();
    }
    const std::vector<uint32_t> expected = scenario == 0
      ? std::vector<uint32_t>{0xfffffff0u, 8, 32, 48}
      : scenario == 1 ? std::vector<uint32_t>{0xfffffff0u, 0, 32, 56}
      : std::vector<uint32_t>{0xfffffff0u, 8, 32, 56};
    ok = check_ticks("clock tick rollover", expected, 178956970) && ok;
  }
  if (!ok) return 1;
  std::cout << "PASS: 288 shuffle transition phases, off/on, latched offsets and rollover\n";
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
        "STATE_REPORT": extract_block((Path(__file__).resolve().parents[1] / "src/serial_debug.cpp").read_text(), "static void print_sequencer_state("),
    }
    patterns = (seqlib / "include/sequencer/Base/Patterns.h").read_text()
    pattern_source = (seqlib / "src/sequencer/Base/Patterns.cpp").read_text()
    euclidian = (seqlib / "include/sequencer/Euclidian/Pattern.h").read_text()
    arguments = (seqlib / "include/sequencer/Euclidian/Euclidian.h").read_text()
    simple = patterns.split("class SimplePattern :", 1)[1]
    methods = [
        "virtual unsigned int get_step_for_tick(",
        "virtual void set_event_for_tick(",
        "virtual void unset_event_for_tick(",
        "virtual bool query_note_on_for_tick(",
        "virtual bool query_note_off_for_tick(",
        "virtual void process_step(",
        "virtual void process_step_shuffled(",
        "virtual void process_step_end(",
        "virtual void process_tick(",
    ]
    driver = PATTERN_DRIVER
    pattern_substitutions = {
        "ARGUMENTS": extract_block(arguments, "struct arguments_t"),
        "PATTERN_METHODS": "\n".join(extract_block(simple, anchor).replace(" override", "") for anchor in methods),
        "TRIGGER_ON": extract_block(pattern_source, "void SimplePattern::trigger_on_for_step(").replace("SimplePattern::", ""),
        "TRIGGER_OFF": extract_block(pattern_source, "void SimplePattern::trigger_off_for_step(").replace("SimplePattern::", ""),
        "MAKE_EUCLID": extract_block(euclidian, "virtual void make_euclid("),
        "MARK_DIRTY": extract_block(euclidian, "void mark_dirty("),
        "DEFERRED_RECOMPUTE": extract_block(euclidian, "void do_deferred_recompute("),
        "GET_DURATION": extract_block(euclidian, "virtual int8_t get_tick_duration("),
    }
    for key, value in pattern_substitutions.items():
        driver = driver.replace("@" + key + "@", value)
    substitutions["PATTERN_DRIVER"] = driver
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
    parser.add_argument("--check-clock-shuffle", action="store_true")
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
        if options.check_clock_shuffle:
            clock = project.parent / "my_libraries/uClock"
            source = directory / "shuffle_transitions.cpp"
            executable = directory / "shuffle_transitions"
            source.write_text(SHUFFLE_TRANSITION_HARNESS)
            subprocess.run([
                compiler, "-std=c++17", "-DUSE_UCLOCK_SOFTWARE_TIMER",
                "-fsanitize=undefined", "-fno-sanitize-recover=all",
                "-I", str(clock / "test/native/stubs"), "-I", str(clock / "src"),
                str(source), str(clock / "src/uClock.cpp"), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()