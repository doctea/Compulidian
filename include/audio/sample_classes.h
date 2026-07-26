#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// BaseSampleData — abstract interface for a block of 16-bit PCM samples.
// size() returns uint32_t so that large flash-stored samples (> 65535 frames)
// are supported without truncation.
// ---------------------------------------------------------------------------
class BaseSampleData {
    public:
        BaseSampleData() = default;
        virtual int16_t  get_sample(uint32_t index) = 0;
        virtual uint32_t size() = 0;
};

// ---------------------------------------------------------------------------
// SampleDataArray — wraps a compile-time const int16_t[] in flash/PROGMEM.
// ---------------------------------------------------------------------------
class SampleDataArray : public BaseSampleData {
    private:
        const int16_t *sample_array;
        uint32_t       sample_size;

    public:
        SampleDataArray(const int16_t *array, uint32_t size)
            : sample_array(array), sample_size(size) {}

        inline virtual int16_t get_sample(uint32_t index) override {
            if (index < sample_size) {
                return sample_array[index];
            }
            return 0;
        }

        inline virtual uint32_t size() override {
            return sample_size;
        }
};

// ---------------------------------------------------------------------------
// SampleDataFlash — reads 16-bit PCM directly from XIP-mapped flash.
// On RP2040, flash is memory-mapped at XIP_BASE (0x10000000), so any address
// in that range can be dereferenced as a regular pointer with no DMA overhead.
// Construct with the raw XIP address and the number of int16_t frames.
// ---------------------------------------------------------------------------
class SampleDataFlash : public BaseSampleData {
    private:
        const int16_t *flash_ptr;   // pointer into XIP-mapped flash
        uint32_t       sample_size; // number of int16_t frames

    public:
        SampleDataFlash() : flash_ptr(nullptr), sample_size(0) {}

        SampleDataFlash(uint32_t xip_address, uint32_t num_samples)
            : flash_ptr(reinterpret_cast<const int16_t *>(xip_address))
            , sample_size(num_samples) {}

        // Re-initialise in place (used with static pools to avoid heap churn).
        void init(uint32_t xip_address, uint32_t num_samples) {
            flash_ptr   = reinterpret_cast<const int16_t *>(xip_address);
            sample_size = num_samples;
        }

        inline virtual int16_t get_sample(uint32_t index) override {
            if (index < sample_size) {
                return flash_ptr[index];
            }
            return 0;
        }

        inline virtual uint32_t size() override {
            return sample_size;
        }
};