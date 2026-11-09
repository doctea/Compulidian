"""Native pulse normalisation regression; run with python3 test/test_cv_clock_input.py."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile


HARNESS = r"""
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstdio>

#define USE_UCLOCK
#define ENABLE_CLOCK_INPUT_CV
#define ENABLE_EUCLIDIAN
#include "cv_clock_input.h"
#include "settings.h"
#define ATOMIC_BLOCK(mode) for (bool atomic_once = true; atomic_once; atomic_once = false)
constexpr uint32_t TICKS_PER_PHRASE = 384;
constexpr int PULSE_1_INPUT = 0;
constexpr int PULSE_2_INPUT = 1;
constexpr int NORMALISATION_PROBE = 4;
bool probe_level = false;
bool patched[2] = {false, false};
bool input_level[2] = {false, false};
uint32_t now_us = 0;
uint32_t pulse_0_transitions = 0;
uint32_t pulse_1_transitions = 0;
bool gpio_get(int pin) { return !(patched[pin] ? input_level[pin] : probe_level); }
void gpio_put(int, bool level) { probe_level = level; }
uint32_t micros() { return now_us; }
uint32_t millis() { return now_us / 1000; }
void clock_report_external_event_overflow() {}

@QUEUE@

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

struct Card {
    enum Input { Audio1, Audio2, CV1, CV2, Pulse1, Pulse2 };
    enum Switch { Down, Middle, Up };
    bool useNormProbe = true;
    bool first_run = true;
    bool pulse[2] = {};
    bool last_pulse[2] = {};
    bool connected[6] = {};
    uint32_t plug_state[6] = {};
    uint32_t np = 0;
    uint32_t probe_seed = 1;
    int norm_probe_count = 0;
    int mux_state = 0;
    int cpuPhase = 0;
    int cvi = 0;
    int startupCounter = 8;
    int32_t knobssm[4] = {};
    int32_t knobs[4] = {};
    int32_t cv[2] = {};
    int adcInL = 0, adcInR = 0;
    uint16_t ADC_Buffer[2][8] = {};
    Switch switchVal = Middle, lastSwitchVal = Middle;
    bool switchInitialized = false;
    bool Disconnected(Input input) { return !connected[input]; }
    uint32_t next_norm_probe() {
        probe_seed = 1664525 * probe_seed + 1013904223;
        return probe_seed >> 31;
    }
    void sample() {
        @SAMPLING@
        norm_probe_count = (norm_probe_count + 1) & 0xF;
        mux_state = (mux_state + 1) & 3;
        if (startupCounter) --startupCounter;
        ++now_us;
    }
};

void sample_and_check_events(Card& card, uint32_t expected_reset, uint32_t expected_clock) {
    card.sample();
    uint32_t reset_events = 0, clock_events = 0;
    bool reset;
    uint32_t observed_at_us;
    while (take_cv_clock_event(reset, observed_at_us)) {
        check(observed_at_us == now_us - 1, "event lost its observation timestamp");
        if (reset) ++reset_events;
        else ++clock_events;
    }
    check(reset_events == expected_reset, "unexpected reset edge (Pulse1)");
    check(clock_events == expected_clock, "unexpected clock edge (Pulse2)");
}

namespace umodular { namespace clock {
struct uClockClass {
    enum PPQNResolution { PPQN_1 = 1, PPQN_24 = 24 };
    enum ClockMode { INTERNAL_CLOCK, EXTERNAL_CLOCK };
    enum ClockState { PAUSED, STOPPED, STARTED, STARTING };
    PPQNResolution input_ppqn = PPQN_24;
    ClockState clock_state = STARTED;
    int starts = 0;
    ClockMode mode = INTERNAL_CLOCK;
    void setInputPPQN(PPQNResolution value) { input_ppqn = value; }
    void setClockMode(ClockMode value) { mode = value; }
    void setTempo(float) {}
    void pause() { clock_state = STARTED; }
    void start() {
        ++starts;
        resetCounters();
        clock_state = mode == EXTERNAL_CLOCK ? STARTING : STARTED;
    }
    uint32_t output_position = 0;
    int getOutputPPQN() { return 96; }
    void resetCounters() { output_position = 0; }
    bool resetCountersToPosition(uint32_t position) { output_position = position; return true; }
    bool isStrictExternalMode() { return true; }
    bool isExternalClockStalled() { return false; }
    float getTempo() { return 120.5f; }
    uint16_t getExternalTicksRemaining() { return 7; }
    uint32_t getLastAcceptedExternalInterval() { return 12345; }
    uint32_t getExternalClockPulseAge() { return 67; }
    uint16_t getIntOverflowCounter() { return 0; }
    uint16_t getExtOverflowCounter() { return 0; }
};
}}
umodular::clock::uClockClass uClock;
enum ClockMode { CLOCK_INTERNAL, CLOCK_EXTERNAL_USB_HOST, CLOCK_EXTERNAL_CV, CLOCK_NONE };
ClockMode clock_mode = CLOCK_INTERNAL;
bool playing = true, waiting_for_external_clock = false;
float bpm_current = 120;
void (*__clock_mode_changed_callback)(ClockMode, ClockMode) = nullptr;
bool is_external_clock_mode(ClockMode mode) { return mode != CLOCK_INTERNAL && mode != CLOCK_NONE; }
#define DEFAULT_INTERNAL_PPQN umodular::clock::uClockClass::PPQN_24
umodular::clock::uClockClass::PPQNResolution external_cv_ppqn = umodular::clock::uClockClass::PPQN_1;
umodular::clock::uClockClass::PPQNResolution internal_ppqn = DEFAULT_INTERNAL_PPQN;
uint32_t ticks = 0;
uint32_t external_clock_received_pulses = 0, external_clock_delivered_sync_ticks = 0;
uint32_t external_clock_rejected_pulses = 0, external_clock_adapter_overflow = 0;
@SET_PLAYING@
@START_CLOCK@
@RESET_POSITION@
@SET_PPQN@
@CHANGE_MODE@
Settings current_settings{};
struct Sequence {
    int releases = 0;
    void release_outputs() { ++releases; }
    int get_euclidian_seed() { return -1; }
} sequence;
using EuclidianSequencer = Sequence;
Sequence* sequencer = &sequence;
int last_processed_tick = 0;
bool output_processing_pending = false;
int accepted = 0;
bool clock_receive_external_pulse_at(ClockMode source, uint32_t) {
    if (!playing || source != clock_mode) return false;
    waiting_for_external_clock = false;
    ++accepted;
    return true;
}
@PROCESS_EVENT@
struct ExternalClockDiagnostics {
    uint16_t input_ppqn = 4;
    uint32_t received_pulses = 3, rejected_pulses = 2, delivered_sync_ticks = 18, expected_sync_ticks = 18;
};
ExternalClockDiagnostics get_external_clock_diagnostics() { return {}; }
float get_bpm() { return 100.25f; }
@SET_SERIAL_PPQN@
constexpr int SYSEX_CLOCK_STATUS_HEADER_BYTES = 15, SYSEX_CLOCK_STATUS_FIELDS = 25;
@BUILD_STATUS@

int main() {
    for (uint16_t ppqn : {1, 2, 4, 8, 12, 16, 24}) {
        change_clock_mode(CLOCK_INTERNAL);
        check(set_external_cv_clock_ppqn(ppqn), "valid PPQN rejected");
        check(uClock.input_ppqn == 24, "CV setting changed internal clock");
        change_clock_mode(CLOCK_EXTERNAL_CV);
        check(uClock.input_ppqn == ppqn, "CV mode lost selected PPQN");
        check(waiting_for_external_clock && playing, "PPQN change stopped playback");
        change_clock_mode(CLOCK_EXTERNAL_USB_HOST);
        check(uClock.input_ppqn == 24, "MIDI clock inherited CV PPQN");
        change_clock_mode(CLOCK_EXTERNAL_CV);
        check(uClock.input_ppqn == ppqn, "CV PPQN did not survive mode switch");
    }
    check(set_external_cv_clock_ppqn(4) && uClock.input_ppqn == 4, "live PPQN change failed");
    for (uint16_t ppqn : {0, 3, 5, 23, 25, 255}) {
        check(!set_external_cv_clock_ppqn(ppqn), "invalid PPQN accepted");
        check(uClock.input_ppqn == 4 && external_cv_ppqn == 4, "invalid PPQN mutated clock");
    }
    current_settings.cv_reset_required = 1;
    current_settings.cv_reset_song_position = 1;
    change_clock_mode(CLOCK_INTERNAL);
    process_cv_clock_event(false, 1000);
    check(clock_mode == CLOCK_INTERNAL && accepted == 0, "RESET-required mode accepted clock takeover");
    ticks = 1000;
    process_cv_clock_event(true, 1000);
    check(clock_mode == CLOCK_EXTERNAL_CV && ticks == 0, "default RESET did not rewind");
    current_settings.cv_reset_song_position = 0;
    ticks = 1000;
    output_processing_pending = true;
    process_cv_clock_event(true, 2000);
    check(ticks == 768 && uClock.output_position == 3072, "phase RESET lost phrase number or resolution");
    check(sequence.releases == 2 && !output_processing_pending && last_processed_tick == -1, "RESET left held/pending outputs");
    ticks = 1151;
    process_cv_clock_event(true, 3000);
    check(ticks == 1152, "phrase boundary RESET prevented phrase progression");
        for (bool rewind : {false, true}) {
          current_settings.cv_reset_song_position = rewind;
          for (auto source : {CLOCK_INTERNAL, CLOCK_EXTERNAL_CV}) {
            for (auto state : {uClock.STOPPED, uClock.PAUSED, uClock.STARTED}) {
                playing = state == uClock.STARTED;
                clock_mode = source;
                uClock.mode = source == CLOCK_INTERNAL ? uClock.INTERNAL_CLOCK : uClock.EXTERNAL_CLOCK;
                uClock.clock_state = state;
                ticks = 1000;
                output_processing_pending = true;
                int previous_starts = uClock.starts;
                int previous_releases = sequence.releases;
                int previous_accepted = accepted;
                process_cv_clock_event(true, 3500);
                check(playing && clock_mode == CLOCK_EXTERNAL_CV, "RESET did not enable CV playback");
                check(uClock.clock_state != uClock.STOPPED && uClock.clock_state != uClock.PAUSED,
                    "RESET left the clock engine stopped or paused");
                check(uClock.starts == previous_starts + (state == uClock.STARTED ? 0 : 1),
                    "RESET failed to start stopped transport or restarted running transport");
                check(ticks == (rewind ? 0u : 768u) && uClock.output_position == ticks * 4,
                    "starting on RESET discarded the selected phrase position");
                check(waiting_for_external_clock && accepted == previous_accepted,
                    "RESET did not wait for a fresh CV clock pulse");
                check(sequence.releases == previous_releases + 1 && !output_processing_pending && last_processed_tick == -1,
                    "starting on RESET left held or pending outputs");
                process_cv_clock_event(false, 3600);
                check(accepted == previous_accepted + 1 && !waiting_for_external_clock,
                    "CV clock pulse after RESET was not accepted");
            }
          }
        }
    current_settings.cv_reset_required = 0;
    change_clock_mode(CLOCK_EXTERNAL_USB_HOST);
        accepted = 0;
    process_cv_clock_event(false, 4000);
    check(clock_mode == CLOCK_EXTERNAL_CV && accepted == 1, "clock-only CV takeover failed");
    Card card;
    for (int sample = 0; sample < 4096; ++sample)
        sample_and_check_events(card, 0, 0);
    check(pulse_0_transitions == 0 && pulse_1_transitions == 0,
          "unpatched normalisation probe incremented transition counters");

    patched[1] = true;
    for (int sample = 0; sample < 1024; ++sample)
        sample_and_check_events(card, 0, 0);
    check(card.connected[Card::Pulse2], "patched low input was not recognised");
    for (int cycle = 0; cycle < 16; ++cycle) {
        input_level[1] = true;
        sample_and_check_events(card, 0, 1);
        sample_and_check_events(card, 0, 0);
        input_level[1] = false;
        sample_and_check_events(card, 0, 0);
    }
    check(pulse_0_transitions == 0 && pulse_1_transitions == 16,
          "square wave did not produce exactly one event per rising edge");

    patched[0] = true;
    for (int sample = 0; sample < 1024; ++sample)
        sample_and_check_events(card, 0, 0);
    check(card.connected[Card::Pulse1], "patched reset input was not recognised");
    input_level[0] = input_level[1] = true;
    sample_and_check_events(card, 1, 1);
    sample_and_check_events(card, 0, 0);
    input_level[0] = input_level[1] = false;
    sample_and_check_events(card, 0, 0);

    card.useNormProbe = false;
    input_level[0] = input_level[1] = true;
    sample_and_check_events(card, 1, 1);
    sample_and_check_events(card, 0, 0);

    Card startup_card;
    sample_and_check_events(startup_card, 0, 0);
    const auto before = get_cv_clock_input_diagnostics();
    for (int edge = 0; edge < 10; ++edge) {
        now_us += 100;
        enqueue_cv_clock_event(false);
    }
    auto diagnostics = get_cv_clock_input_diagnostics();
    check(diagnostics.pulses == before.pulses + 10, "diagnostics lost raw edges on overflow");
    check(diagnostics.overflow == before.overflow + 3, "queue overflow not counted");
    check(diagnostics.interval_us == 100, "pulse interval not measured at capture");
    check(diagnostics.min_interval_us <= 100 && diagnostics.max_interval_us >= 100, "interval range missing");
    now_us += 500;
    bool reset;
    uint32_t observed_at_us;
    int drained = 0;
    while (take_cv_clock_event(reset, observed_at_us)) ++drained;
    check(drained == 7, "queue capacity changed");
    diagnostics = get_cv_clock_input_diagnostics();
    check(diagnostics.max_queue_lag_us >= 1400, "queue lag not reported");
    now_us = UINT32_MAX - 50;
    enqueue_cv_clock_event(false);
    take_cv_clock_event(reset, observed_at_us);
    now_us = 49;
    enqueue_cv_clock_event(false);
    check(get_cv_clock_input_diagnostics().interval_us == 100, "pulse timer wrap broke interval measurement");
    uint8_t payload[141];
    std::memset(payload, 0xa5, sizeof(payload));
    current_settings.cv_clock_ppqn = 4;
    const auto length = build_clock_status_payload(payload);
    check(length == 140 && payload[140] == 0xa5, "clock status length/guard mismatch");
    for (int index = 0; index < length; ++index) check(payload[index] < 128, "clock status is not MIDI safe");
    check(payload[0] == 1 && payload[1] == 1 && payload[2] == 2 && payload[5] == 1, "clock status flags incorrect");
    auto field = [&](int index) {
        uint32_t value = 0;
        for (int byte = 0; byte < 5; ++byte) value |= uint32_t(payload[15 + index * 5 + byte]) << (7 * byte);
        return value;
    };
    check(field(1) == 100250 && field(2) == 120500, "set/engine BPM not distinguished");
    check(field(4) == UINT32_MAX, "signed seed bits lost");
    check(field(5) == get_cv_clock_input_diagnostics().pulses && field(7) == 3, "raw counts/overflow lost in status");
    check(field(24) == 4 && field(17) == 18, "clock delivery diagnostics missing");
    for (unsigned rate : {1u, 2u, 4u, 8u, 12u, 16u, 24u}) {
        char command[64];
        std::snprintf(command, sizeof(command), "clock-external-cv-ppqn %u", rate);
        check(set_cv_clock_ppqn_from_serial(command), "valid serial PPQN rejected");
        check(current_settings.cv_clock_ppqn == rate && external_cv_ppqn == rate, "serial PPQN did not update shared/live setting");
    }
    for (const char *argument : {"-1", "+2", "65537", "4294967297", "9999999999999999999999999999", "2junk", "2 1", "3", "0", ""}) {
        char command[128];
        std::snprintf(command, sizeof(command), "clock-external-cv-ppqn %s", argument);
        check(!set_cv_clock_ppqn_from_serial(command), "malformed serial PPQN accepted");
        check(current_settings.cv_clock_ppqn == 24 && external_cv_ppqn == 24, "invalid serial PPQN mutated state");
    }
    check(!set_cv_clock_ppqn_from_serial("clock-external-cv-ppqn"), "bare serial command accepted");
    std::cout << "CV clock input normalisation checks passed\n";
}
"""


def main():
    root = Path(__file__).resolve().parents[1]
    source = (root / "src" / "ComputerCard.cpp").read_text()
    queue_start = source.index("\tstruct CvClockEvent")
    queue = source[queue_start:source.index("\n#endif", queue_start)]
    sampling_start = source.index("\t// Set pulse inputs")
    sampling = source[sampling_start:source.index("\t// Run the DSP", sampling_start)]
    clock = (root.parent / "my_libraries/midihelpers/src/clock.cpp").read_text()
    from test_web_bank_management import extract_block
    harness_text = HARNESS.replace("@QUEUE@", queue).replace("@SAMPLING@", sampling)
    harness_text = harness_text.replace("@SET_PPQN@", extract_block(clock, "bool set_external_cv_clock_ppqn("))
    harness_text = harness_text.replace("@CHANGE_MODE@", extract_block(clock, "void change_clock_mode("))
    harness_text = harness_text.replace("@SET_PLAYING@", extract_block(clock, "void clock_set_playing("))
    harness_text = harness_text.replace("@START_CLOCK@", extract_block(clock, "void clock_start("))
    harness_text = harness_text.replace("@RESET_POSITION@", extract_block(clock, "bool clock_reset_to_position("))
    main_source = (root / "src/main.cpp").read_text()
    harness_text = harness_text.replace("@PROCESS_EVENT@", extract_block(main_source, "static void process_cv_clock_event("))
    configurator = (root / "include/configurator.h").read_text()
    serial_debug = (root / "src/serial_debug.cpp").read_text()
    harness_text = harness_text.replace("@SET_SERIAL_PPQN@", extract_block(serial_debug, "static bool set_cv_clock_ppqn_from_serial("))
    harness_text = harness_text.replace("@BUILD_STATUS@", extract_block(configurator, "static uint16_t build_clock_status_payload("))
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A native C++ compiler (g++ or clang++) is required")
    with tempfile.TemporaryDirectory(prefix="cv-clock-input-") as temporary:
        harness = Path(temporary) / "test.cpp"
        executable = Path(temporary) / "test"
        harness.write_text(harness_text)
        subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
             "-fno-sanitize-recover=all", "-I", str(root / "include"), str(harness), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()