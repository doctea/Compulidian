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
#include <atomic>

#define USE_UCLOCK
#define ENABLE_CLOCK_INPUT_CV
#define ENABLE_CLOCK_INPUT_MIDI_DIN
#define ENABLE_EUCLIDIAN
#include "cv_clock_input.h"
#include "settings.h"
#define ATOMIC_BLOCK(mode) for (bool atomic_once = true; atomic_once; atomic_once = false)
constexpr uint32_t TICKS_PER_PHRASE = 384;
constexpr uint32_t TICKS_PER_BAR = 96, TICKS_PER_STEP = 6, BARS_PER_PHRASE = 4;
constexpr int PULSE_1_INPUT = 0;
constexpr int PULSE_2_INPUT = 1;
constexpr int NORMALISATION_PROBE = 4;
bool probe_level = false, probe_wired = true;
bool patched[2] = {false, false};
bool input_level[2] = {false, false};
uint32_t now_us = 0;
uint32_t pulse_0_transitions = 0;
uint32_t pulse_1_transitions = 0;
bool gpio_get(int pin) { return !((patched[pin] || !probe_wired) ? input_level[pin] : probe_level); }
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
    bool cv_clock_started = false;
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
    bool PulseIn1RisingEdge() { return pulse[0] && !last_pulse[0]; }
    bool PulseIn2RisingEdge() { return pulse[1] && !last_pulse[1]; }
    uint32_t next_norm_probe() {
        probe_seed = 1664525 * probe_seed + 1013904223;
        return probe_seed >> 31;
    }
    void sample() {
        @SAMPLING@
        @CAPTURE@
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
enum ClockMode { CLOCK_INTERNAL, CLOCK_EXTERNAL_USB_HOST, CLOCK_EXTERNAL_CV, CLOCK_NONE, CLOCK_EXTERNAL_MIDI_DIN };
ClockMode clock_mode = CLOCK_INTERNAL;
bool playing = true, waiting_for_external_clock = false;
float bpm_current = 120;
void (*__clock_mode_changed_callback)(ClockMode, ClockMode) = nullptr;
bool is_external_clock_mode(ClockMode mode) { return mode != CLOCK_INTERNAL && mode != CLOCK_NONE; }
#define DEFAULT_INTERNAL_PPQN umodular::clock::uClockClass::PPQN_24
umodular::clock::uClockClass::PPQNResolution external_cv_ppqn = umodular::clock::uClockClass::PPQN_1;
umodular::clock::uClockClass::PPQNResolution internal_ppqn = DEFAULT_INTERNAL_PPQN;
uint32_t ticks = 0;
#define BPM_CURRENT_PHRASE (ticks / TICKS_PER_PHRASE)
#define BPM_CURRENT_BAR_OF_PHRASE ((ticks % TICKS_PER_PHRASE) / TICKS_PER_BAR)
#define BPM_CURRENT_BEAT_OF_BAR ((ticks % TICKS_PER_BAR) / 24)
bool is_bpm_on_phrase(uint32_t value) { return value % TICKS_PER_PHRASE == 0; }
bool is_bpm_on_bar(uint32_t value) { return value % TICKS_PER_BAR == 0; }
bool is_bpm_on_beat(uint32_t value) { return value % 24 == 0; }
bool is_bpm_on_sixteenth(uint32_t value, uint32_t offset = 0) { return value % TICKS_PER_STEP == offset; }
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
    int seed = 0;
    uint32_t seed_identity = 0, last_locked_seed = 0;
    bool seed_locked = false, add_phrase_to_seed = true, add_identity_to_seed = false;
    bool independent_progression = false, progression_started = false, fills_enabled = true;
    uint32_t progression_tick = 0;
    uint32_t mutations = 0, fills = 0, last_mutation_seed = 0;
    void release_outputs() { ++releases; }
    void on_step(int) {}
    void on_step_end(int) {}
    void on_beat(int) {}
    void do_deferred_recomputes() {}
    void do_phrase_mutations(int) { ++mutations; last_mutation_seed = get_euclidian_seed(); }
    void do_bar_fill(int) { ++fills; }
    @PROGRESSION_METHODS@
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
bool cv_clock_ticked = false, cv_clock_reset = false;
bool usb_midi_clock_ticked = false, din_midi_clock_ticked = false;
bool (*check_cv_clock_ticked_callback)() = nullptr;
void (*__external_clock_stall_callback)(bool) = nullptr;
unsigned long missed_micros = 0, micros_per_tick = 1000, last_ticked_at_micros = 0;
bool clock_receive_external_pulse(ClockMode source) { return clock_receive_external_pulse_at(source, now_us); }
void clock_receive_external_reset(ClockMode) { clock_reset_to_position(0); }
bool check_and_unset_pc_usb_midi_clock_ticked() {
    bool result = usb_midi_clock_ticked;
    usb_midi_clock_ticked = false;
    return result;
}
bool check_and_unset_din_midi_clock_ticked() {
    bool result = din_midi_clock_ticked;
    din_midi_clock_ticked = false;
    return result;
}
@CHECK_CV_TICK@
@UPDATE_TICKS@
#undef USE_UCLOCK
#define update_clock_ticks update_clock_ticks_legacy
@UPDATE_TICKS@
#undef update_clock_ticks
#define USE_UCLOCK
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
    cv_clock_ticked = true;
    check(check_and_unset_cv_clock_ticked(), "queued CV pulse hidden by absent legacy callback");
    check(!check_and_unset_cv_clock_ticked(), "CV pulse reported twice");
    ticks = 10;
    check(update_clock_ticks(), "internal tick not reported");
    check(!update_clock_ticks(), "unchanged internal tick reported twice");
    for (auto source : {CLOCK_EXTERNAL_CV, CLOCK_EXTERNAL_USB_HOST, CLOCK_EXTERNAL_MIDI_DIN}) {
        clock_mode = source;
        ++ticks;
        const auto engine_ticks = ticks;
        check(update_clock_ticks(), "delivered external tick not reported without raw pulse");
        check(ticks == engine_ticks, "foreground polling advanced engine position");
        check(!update_clock_ticks(), "unchanged external tick reported twice");
        cv_clock_ticked = usb_midi_clock_ticked = din_midi_clock_ticked = true;
        check(!update_clock_ticks(), "raw external pulse reported as a delivered tick");
        check(ticks == engine_ticks, "raw external pulse advanced engine position");
        cv_clock_ticked = usb_midi_clock_ticked = din_midi_clock_ticked = false;
        playing = false;
        ++ticks;
        check(!update_clock_ticks(), "paused transport reported a tick");
        playing = true;
    }
    clock_mode = CLOCK_NONE;
    check(!update_clock_ticks(), "disabled clock reported a tick");
    clock_mode = CLOCK_EXTERNAL_USB_HOST;
    ticks = 20;
    usb_midi_clock_ticked = true;
    check(update_clock_ticks_legacy() && ticks == 21, "legacy USB clock did not advance once");
    check(!update_clock_ticks_legacy(), "legacy USB clock repeated a pulse");
    clock_mode = CLOCK_EXTERNAL_MIDI_DIN;
    din_midi_clock_ticked = true;
    check(update_clock_ticks_legacy() && ticks == 22, "legacy DIN clock did not advance once");
    clock_mode = CLOCK_EXTERNAL_CV;
    cv_clock_ticked = true;
    check(update_clock_ticks_legacy() && ticks == 22, "legacy CV notification changed position");
    check(!update_clock_ticks_legacy(), "legacy CV clock repeated a pulse");
    clock_mode = CLOCK_INTERNAL;
    now_us += micros_per_tick;
    check(update_clock_ticks_legacy() && ticks == 23, "legacy internal clock did not advance");
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
    check(ticks == 0 && uClock.output_position == 0, "resync RESET did not restart playback phase");
    check(sequence.releases == 2 && !output_processing_pending && last_processed_tick == -1, "RESET left held/pending outputs");
    ticks = 1151;
    process_cv_clock_event(true, 3000);
    check(ticks == 0, "resync RESET did not restart playback at zero");
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
                check(ticks == 0 && uClock.output_position == 0,
                    "starting on RESET failed to restart playback phase");
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
    for (uint32_t interval : {96u, 192u}) {
        sequence = Sequence{};
        for (ticks = 0; ticks < 2 * TICKS_PER_PHRASE; ++ticks) sequence.on_tick(ticks);
        check(sequence.mutations == 2 && sequence.fills == 2,
            "default shared-clock phrase/bar dispatch changed");
        sequence = Sequence{};
        sequence.restart_progression();
        current_settings.cv_reset_song_position = CV_RESET_RESYNC;
        for (uint32_t elapsed = 0, playback = 0; elapsed < 8 * TICKS_PER_PHRASE; ++elapsed, ++playback) {
            if (elapsed && elapsed % interval == 0) {
                process_cv_clock_event(true, elapsed);
                playback = 0;
            }
            ticks = playback;
            sequence.on_tick(ticks);
        }
        check(sequence.mutations == 8 && sequence.fills == 8,
            "one/two-bar RESET starved phrase mutations or fills");
        check(sequence.last_mutation_seed == 7 && sequence.get_seed_phrase() == 7,
            "frequent RESET stopped seed progression");
        sequence.set_euclidian_seed_lock(true);
        const uint32_t held_phrase = sequence.get_seed_phrase();
        const uint32_t previous_fills = sequence.fills;
        for (uint32_t elapsed = 0; elapsed < 2 * TICKS_PER_PHRASE; ++elapsed) {
            if (elapsed % interval == 0) process_cv_clock_event(true, elapsed);
            ticks = elapsed % interval;
            sequence.on_tick(ticks);
        }
        check(sequence.get_seed_phrase() == held_phrase && sequence.fills == previous_fills + 2,
            "Hold failed to freeze seed phrase or prevented fills");
        sequence.set_add_identity_enabled(true);
        sequence.set_seed_identity(100);
        check(sequence.get_euclidian_seed() == int(held_phrase + 100), "Card ID ignored while held");
        for (auto mode : {CV_RESET_NEXT_PHRASE, CV_RESET_RESTART_SONG}) {
            const uint32_t previous_progression_phrase =
                sequence.get_progression_tick() / TICKS_PER_PHRASE;
            current_settings.cv_reset_song_position = mode;
            process_cv_clock_event(true, 10000);
            sequence.on_tick(0);
            check(sequence.get_seed_phrase() == held_phrase, "RESET mode lost held phrase");
            check(sequence.get_progression_tick() == (mode == CV_RESET_NEXT_PHRASE
                ? (previous_progression_phrase + 1) * TICKS_PER_PHRASE : 0),
                "Hold changed RESET mode progression policy");
        }
        sequence.set_euclidian_seed_lock(false);
        current_settings.cv_reset_song_position = CV_RESET_NEXT_PHRASE;
        const auto previous_phrase = sequence.get_seed_phrase();
        process_cv_clock_event(true, 10001);
        sequence.on_tick(0);
        check(sequence.get_seed_phrase() == previous_phrase + 1, "Next Phrase did not advance");
        current_settings.cv_reset_song_position = CV_RESET_RESTART_SONG;
        process_cv_clock_event(true, 10002);
        sequence.on_tick(0);
        check(sequence.get_seed_phrase() == 0, "Restart Song did not rewind evolution");
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
    probe_wired = false;
    patched[0] = patched[1] = false;
    Card no_probe_wiring;
    uint32_t startup_resets = 0, startup_clocks = 0;
    for (int sample = 0; sample < 1024; ++sample) {
        no_probe_wiring.sample();
        bool reset;
        uint32_t timestamp;
        while (take_cv_clock_event(reset, timestamp)) {
            if (reset) ++startup_resets;
            else ++startup_clocks;
        }
    }
    check(startup_resets == 1 && startup_clocks == 1,
          "held-high inputs without probe wiring did not reproduce startup qualification edges");
    check(no_probe_wiring.connected[Card::Pulse1] && no_probe_wiring.connected[Card::Pulse2],
          "absent probe wiring incorrectly suppressed constant inputs");
    input_level[0] = input_level[1] = false;
    sample_and_check_events(no_probe_wiring, 0, 0);
    input_level[0] = input_level[1] = true;
    sample_and_check_events(no_probe_wiring, 1, 1);
    Card probe_disabled;
    probe_disabled.useNormProbe = false;
    sample_and_check_events(probe_disabled, 0, 0);
    sample_and_check_events(probe_disabled, 0, 0);
    input_level[0] = input_level[1] = false;
    sample_and_check_events(probe_disabled, 0, 0);
    input_level[0] = input_level[1] = true;
    sample_and_check_events(probe_disabled, 1, 1);
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
    check(field(4) == uint32_t(sequence.get_euclidian_seed()), "effective seed missing from status");
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
    driver = (root / "include/ComputerCard.h").read_text()
    sampling_start = driver.index("\t// Set pulse inputs")
    sampling = driver[sampling_start:driver.index("\t// Run the DSP", sampling_start)]
    player = (root / "include/audio/audio.h").read_text()
    callback = player.index("virtual void __not_in_flash_func(ProcessSample)")
    capture_start = player.index("#ifdef ENABLE_CLOCK_INPUT_CV", callback)
    capture = player[capture_start:player.index("        ++audio_callback_count;", capture_start)]
    clock = (root.parent / "my_libraries/midihelpers/src/clock.cpp").read_text()
    from test_web_bank_management import extract_block
    harness_text = HARNESS.replace("@QUEUE@", queue).replace("@SAMPLING@", sampling)
    base = (root.parent / "my_libraries/seqlib/include/sequencer/Base/Sequencer.h").read_text()
    euclidian = (root.parent / "my_libraries/seqlib/include/sequencer/Euclidian/Sequencer.h").read_text()
    methods = [extract_block(base, anchor) for anchor in (
        "uint32_t get_progression_tick(", "void restart_progression(", "virtual void on_tick(int tick) {")]
    methods += [extract_block(euclidian, anchor).replace(" override", "") for anchor in (
        "uint32_t get_seed_phrase(", "bool is_euclidian_seed_lock(", "void set_euclidian_seed_lock(",
        "bool is_add_phrase_enabled(", "bool is_add_identity_enabled(", "void set_add_identity_enabled(",
        "void set_seed_identity(", "int get_euclidian_seed(", "virtual void on_bar(", "virtual void on_phrase(")]
    harness_text = harness_text.replace("@PROGRESSION_METHODS@", "\n".join(methods))
    harness_text = harness_text.replace("@CAPTURE@", capture)
    harness_text = harness_text.replace("@SET_PPQN@", extract_block(clock, "bool set_external_cv_clock_ppqn("))
    harness_text = harness_text.replace("@CHANGE_MODE@", extract_block(clock, "void change_clock_mode("))
    harness_text = harness_text.replace("@SET_PLAYING@", extract_block(clock, "void clock_set_playing("))
    harness_text = harness_text.replace("@START_CLOCK@", extract_block(clock, "void clock_start("))
    harness_text = harness_text.replace("@RESET_POSITION@", extract_block(clock, "bool clock_reset_to_position("))
    harness_text = harness_text.replace("@CHECK_CV_TICK@", extract_block(clock, "bool check_and_unset_cv_clock_ticked("))
    harness_text = harness_text.replace("@UPDATE_TICKS@", extract_block(clock, "bool update_clock_ticks("))
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