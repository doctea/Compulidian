#pragma once

#include <stdint.h>
#include <stddef.h>
#include "audio/flash_layout.h"
#include "audio/bank_header.h"
#include "audio/sample_classes.h"

// ---------------------------------------------------------------------------
// BankManager
//
// Manages compiled-in and flash-resident sample banks.
//
//  Bank 0  — compiled-in samples (always available; the original behaviour).
//  Banks 1..FLASH_MAX_BANKS — samples stored in raw flash at fixed offsets
//              (written there by wav2bank.py or the web manager tool).
//
// Typical call sequence (from setup_samples()):
//
//   bankManager.setup();          // initialise compiled-in bank + detect flash banks
//
// Runtime bank switch (from main loop, core 0 only):
//
//   bankManager.switch_bank(2);   // switch to flash bank 2
// ---------------------------------------------------------------------------

class BankManager {
public:
    // Bank 0 is the compiled-in bank (always valid).
    static constexpr int COMPILED_BANK = 0;
    static constexpr int MAX_FLASH_BANKS = FLASH_MAX_BANKS;

    // -----------------------------------------------------------------------
    // Initialisation
    // -----------------------------------------------------------------------

    // Full setup: initialise compiled-in bank, detect flash banks, load the
    // bank recorded in Settings::active_bank (defaults to 0).
    // Must be called once from setup_samples() before audio starts.
    void setup();

    // -----------------------------------------------------------------------
    // Bank queries
    // -----------------------------------------------------------------------

    // Total number of valid banks including the compiled-in bank 0.
    int  num_valid_banks() const;

    // Returns true if bank slot `n` has been detected as valid.
    bool is_bank_valid(int n) const;

    // Currently active bank index (0 = compiled-in).
    int  active_bank()    const { return active_bank_; }

    // Pointer to the BankHeader for a flash bank slot (1..MAX_FLASH_BANKS).
    // Returns nullptr if the slot is out of range or not valid.
    const BankHeader* get_bank_header(int slot) const;

    // -----------------------------------------------------------------------
    // Bank switching (call from core 0 / main loop only)
    // -----------------------------------------------------------------------

    // Silences all voices, swaps sample_data[] pointers to the new bank,
    // and updates Settings::active_bank in flash.
    // Returns false if `n` is not a valid bank index.
    bool switch_bank(int n);

private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    // Allocate and populate sample_data[] for the compiled-in bank.
    void init_compiled_bank_();

    // Cache the compiled-in sample_data[] pointers so they can be restored.
    void cache_compiled_bank_();

    // Restore sample_data[] to the cached compiled-in pointers.
    void restore_compiled_bank_();

    // Release SampleDataFlash objects currently in flash_pool_.
    void release_flash_pool_();

    // Silence all voices (set sampleindex to past-end).
    void silence_all_voices_();

    // Load a flash bank (slot 1..MAX_FLASH_BANKS) into sample_data[].
    bool load_flash_bank_(int slot);

    // Scan flash slots 1..MAX_FLASH_BANKS and set bank_valid_[].
    void detect_banks_();

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    // bank_valid_[0]  = true (compiled-in is always valid)
    // bank_valid_[n]  = true if flash bank slot n has a valid header
    bool bank_valid_[MAX_FLASH_BANKS + 1] = {};

    int  active_bank_ = 0;

    // Cached compiled-in BaseSampleData pointers (so they can be restored).
    BaseSampleData* compiled_cache_[MAX_SAMPLES_PER_BANK] = {};
    size_t          compiled_num_voices_ = 0;

    // Static pool of SampleDataFlash objects — avoids heap allocations after
    // the initial compiled-in bank setup.
    SampleDataFlash flash_pool_[MAX_SAMPLES_PER_BANK];
    int             flash_pool_count_ = 0;
};

// Global singleton — defined in bank_manager.cpp.
extern BankManager bankManager;
