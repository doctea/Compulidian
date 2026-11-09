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

#define ENABLE_CLOCK_INPUT_CV
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

int main() {
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
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A native C++ compiler (g++ or clang++) is required")
    with tempfile.TemporaryDirectory(prefix="cv-clock-input-") as temporary:
        harness = Path(temporary) / "test.cpp"
        executable = Path(temporary) / "test"
        harness.write_text(HARNESS.replace("@QUEUE@", queue).replace("@SAMPLING@", sampling))
        subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
             "-fno-sanitize-recover=all", str(harness), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()