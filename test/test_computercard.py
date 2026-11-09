"""Native full-driver integration check; run with python3 test/test_computercard.py."""

import configparser
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


SDK = r"""
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

using uint = unsigned;
#define __not_in_flash_func(name) name
constexpr int GPIO_FUNC_PWM = 1, GPIO_FUNC_SPI = 2, GPIO_FUNC_I2C = 3;
constexpr int GPIO_IN = 0, GPIO_OUT = 1;
constexpr int DMA_SIZE_16 = 1, DREQ_ADC = 2, DREQ_SPI0_TX = 3;
constexpr int DMA_IRQ_0 = 0, PWM_IRQ_WRAP = 1;
constexpr int SPI_CPOL_0 = 0, SPI_CPHA_0 = 0, SPI_MSB_FIRST = 0;
constexpr uint32_t PICO_FLASH_SIZE_BYTES = 2u << 20;

namespace sdk {
inline void (*handlers[2])() = {};
inline bool irqs_enabled[2] = {};
inline unsigned levels[30] = {};
inline float divider = 0;
inline int channels_claimed = 0;
inline int channels_cleaned = 0;
inline int eeprom_attempts = 0;
inline uint32_t now_us = 100;
inline bool running = false;
inline int pwm_wrap = 0;
inline void fill(void* buffer) {
    auto* samples = static_cast<uint16_t*>(buffer);
    for (int index = 0; index < 8; ++index) samples[index] = 2048;
    samples[6] = 500;
}
}

inline uint32_t micros() { return sdk::now_us; }
inline void sleep_us(unsigned) {}
inline bool gpio_get(unsigned pin) { return pin == 2 || pin == 3; }
inline void gpio_put(unsigned pin, bool value) { sdk::levels[pin] = value; }
inline void gpio_init(unsigned) {}
inline void gpio_set_function(unsigned, unsigned) {}
inline void gpio_set_dir(unsigned, bool) {}
inline void gpio_set_pulls(unsigned, bool, bool) {}
inline void gpio_pull_up(unsigned) {}
inline void gpio_disable_pulls(unsigned) {}

struct pwm_config {};
inline pwm_config pwm_get_default_config() { return {}; }
inline void pwm_config_set_wrap(pwm_config*, unsigned wrap) { sdk::pwm_wrap = wrap; }
inline unsigned pwm_gpio_to_slice_num(unsigned pin) { return pin / 2; }
inline void pwm_init(unsigned, const pwm_config*, bool) {}
inline void pwm_set_gpio_level(unsigned pin, unsigned level) { sdk::levels[pin] = level; }
inline void pwm_clear_irq(unsigned) {}
inline void pwm_set_irq_enabled(unsigned, bool) {}

struct adc_hw_t { uint32_t fifo = 0; };
inline adc_hw_t adc_registers;
inline adc_hw_t* adc_hw = &adc_registers;
inline void adc_select_input(unsigned) {}
inline void adc_set_round_robin(unsigned) {}
inline void adc_fifo_setup(bool, bool, unsigned, bool, bool) {}
inline void adc_set_clkdiv(float divider) { sdk::divider = divider; }
inline void adc_run(bool running) { sdk::running = running; }
inline void adc_init() {}
inline void adc_gpio_init(unsigned) {}

struct dma_channel_config {};
struct dma_hw_t { uint32_t ints0 = 0; };
inline dma_hw_t dma_registers;
inline dma_hw_t* dma_hw = &dma_registers;
inline unsigned dma_claim_unused_channel(bool) { return sdk::channels_claimed++; }
inline dma_channel_config dma_channel_get_default_config(unsigned) { return {}; }
inline void channel_config_set_transfer_data_size(dma_channel_config*, unsigned) {}
inline void channel_config_set_read_increment(dma_channel_config*, bool) {}
inline void channel_config_set_write_increment(dma_channel_config*, bool) {}
inline void channel_config_set_dreq(dma_channel_config*, unsigned) {}
inline void dma_channel_configure(unsigned, const dma_channel_config*, void* target,
                                  const void*, unsigned count, bool) {
    if (count == 8) sdk::fill(target);
}
inline void dma_channel_set_irq0_enabled(unsigned, bool) {}
inline void dma_channel_set_write_addr(unsigned, void* target, bool) { sdk::fill(target); }
inline void dma_channel_set_read_addr(unsigned, const void*, bool) {}
inline void dma_channel_cleanup(unsigned) { ++sdk::channels_cleaned; }

inline void irq_set_enabled(unsigned irq, bool enabled) { sdk::irqs_enabled[irq] = enabled; }
inline void irq_set_exclusive_handler(unsigned irq, void (*handler)()) { sdk::handlers[irq] = handler; }
inline void irq_set_priority(unsigned, unsigned) {}
inline void irq_remove_handler(unsigned irq, void (*handler)()) {
    assert(sdk::handlers[irq] == handler);
    sdk::handlers[irq] = nullptr;
}

struct spi_hw_t { uint32_t dr = 0; };
inline spi_hw_t spi_registers;
constexpr int spi0 = 0;
inline spi_hw_t* spi_get_hw(int) { return &spi_registers; }
inline void spi_init(int, unsigned) {}
inline void spi_set_format(int, unsigned, unsigned, unsigned, unsigned) {}

constexpr int i2c0 = 0;
inline void i2c_init(int, unsigned) {}
inline int i2c_write_timeout_us(int, uint8_t, const uint8_t*, size_t, bool, unsigned timeout) {
    assert(timeout == 10000);
    ++sdk::eeprom_attempts;
    return -1;
}
inline int i2c_read_timeout_us(int, uint8_t, uint8_t*, size_t, bool, unsigned) {
    assert(false);
    return -1;
}
inline void flash_do_cmd(const uint8_t*, uint8_t* result, size_t count) {
    assert(count == 4);
    result[3] = 0x18;
}
inline void flash_get_unique_id(uint8_t* result) {
    const uint64_t identity = 0x123456789abcdef0ULL;
    std::memcpy(result, &identity, sizeof(identity));
}
"""


HARNESS = r"""
#include "ComputerCard.h"
#include "cv_clock_input.h"

volatile uint32_t pulse_0_transitions = 0, pulse_1_transitions = 0;
void clock_report_external_event_overflow() {}
int verify_header_consumer(ComputerCard* card);

class TestCard : public ComputerCard {
public:
    int callbacks = 0, worker_loops = 0, fills = 0;
    void CalculateSamples() override { ++fills; }
    void AudioWorkerLoop() override {
        ++worker_loops;
        CalculateSamples();
        assert(sdk::handlers[DMA_IRQ_0]);
        sdk::handlers[DMA_IRQ_0]();
    }
    void ProcessSample() override {
        assert(SwitchReady() == (callbacks >= 3));
        if (SwitchReady()) assert(SwitchVal() == Down);
        assert(!SwitchChanged());
        ++callbacks;
        if (callbacks == 8) Abort();
    }
};

int main() {
    TestCard card;
    ComputerCard* base = &card;
    assert(!base->SwitchReady() && base->SwitchVal() == ComputerCard::Middle);
    assert(!base->CVOutsCalibrated() && !base->InputsCalibrated());
    assert(base->HardwareVersion() == ComputerCard::Unknown);
    assert(sdk::eeprom_attempts == 4);
    assert(base->UniqueCardID() != 0 && base->FlashSizeBytes() == (16u << 20));
    assert(verify_header_consumer(base) == 6);
    assert(sdk::pwm_wrap == 1999);
    base->AudioOut1(0);
    base->AudioOut2(0);
    base->CVOut1MIDINote(60);
    base->CVOut2MIDINote(60);
    base->Run();
    assert(card.callbacks == 8 && card.worker_loops == 8 && card.fills == 8);
    assert(sdk::divider == EXPECTED_DIVIDER);
    assert(sdk::channels_claimed == 2 && sdk::channels_cleaned == 2);
    assert(!sdk::running && !sdk::irqs_enabled[DMA_IRQ_0] && !sdk::irqs_enabled[PWM_IRQ_WRAP]);
    assert(!sdk::handlers[DMA_IRQ_0] && !sdk::handlers[PWM_IRQ_WRAP]);
#ifdef ENABLE_CLOCK_INPUT_CV
    capture_cv_clock_edges(true, true);
    bool reset;
    uint32_t timestamp;
    assert(take_cv_clock_event(reset, timestamp) && reset && timestamp == 100);
    assert(take_cv_clock_event(reset, timestamp) && !reset && timestamp == 100);
    assert(!take_cv_clock_event(reset, timestamp));
    const auto diagnostics = get_cv_clock_input_diagnostics();
    assert(diagnostics.resets == 1 && diagnostics.pulses == 1 && diagnostics.overflow == 0);
    assert(pulse_0_transitions == 1 && pulse_1_transitions == 1);
#endif
}
"""


CONSUMER = r"""
#include "ComputerCard.h"
#ifndef COMPUTERCARD_NOIMPL
#error Consumer must be declaration-only
#endif
int verify_header_consumer(ComputerCard* card) {
    card->KnobVal(ComputerCard::Main);
    card->CVIn(0);
    card->AudioIn(0);
    card->PulseOut1(false);
    card->CVOut2(0);
    card->LedOff(0);
    return ComputerCard::numLeds;
}
"""


def main():
    root = Path(__file__).resolve().parents[1]
    reference = root / "include/upstream/ComputerCard.h"
    assert hashlib.sha256(reference.read_bytes()).hexdigest() == (
        "2137722c6ab51db000c93061bac1fea0f441e4c4785b9248117f88f832a1d8de"
    ), "Pristine upstream reference changed"
    config = configparser.ConfigParser(interpolation=None)
    config.read(root / "platformio.ini")
    flags = [line.strip() for line in config["env:rpipico"]["build_flags"].splitlines()
             if line.strip() and not line.lstrip().startswith(";")]
    assert "-DCOMPUTERCARD_NOIMPL" in flags
    divider_flags = [line for line in flags if line.startswith("-DCOMPUTERCARD_ADC_DIVIDER=")]
    assert len(divider_flags) <= 1, "Conflicting ADC divider overrides"
    configured_divider = int(divider_flags[0].split("=", 1)[1]) if divider_flags else 124
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A native C++ compiler (g++ or clang++) is required")
    with tempfile.TemporaryDirectory(prefix="computercard-") as temporary:
        directory = Path(temporary)
        (directory / "hardware").mkdir()
        (directory / "sdk.h").write_text(SDK)
        for header in ("gpio", "pwm", "adc", "clocks", "dma", "flash", "i2c", "irq", "spi"):
            (directory / "hardware" / (header + ".h")).write_text('#include "sdk.h"\n')
        (directory / "clock.h").write_text("void clock_report_external_event_overflow();\n")
        harness = directory / "test.cpp"
        consumer = directory / "consumer.cpp"
        harness.write_text(HARNESS)
        consumer.write_text(CONSUMER)
        common = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                  "-fno-sanitize-recover=all", "-DCOMPUTERCARD_NOIMPL", "-I", str(directory),
                  "-I", str(root / "include")]
        for divider, clock_enabled in ((None, False), (None, True), (248, True),
                                       (249, True), (configured_divider, True)):
            executable = directory / "test"
            flags = [f"-DEXPECTED_DIVIDER={divider or 124}"]
            if divider is not None:
                flags.append(f"-DCOMPUTERCARD_ADC_DIVIDER={divider}")
            if clock_enabled:
                flags.append("-DENABLE_CLOCK_INPUT_CV")
            subprocess.run(common + flags + [str(root / "src/ComputerCard.cpp"), str(harness),
                           str(consumer), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
        for divider in (94, 65536):
            result = subprocess.run(common + [f"-DCOMPUTERCARD_ADC_DIVIDER={divider}",
                                    "-fsyntax-only", str(consumer)], capture_output=True, text=True)
            assert result.returncode != 0 and "unsupported ADC divider" in result.stderr
    print("PASS: full driver link, public API, worker hook, switch readiness, EEPROM-less fallback, "
          "IRQ teardown, clock ordering, upstream default and legacy divider overrides")


if __name__ == "__main__":
    main()