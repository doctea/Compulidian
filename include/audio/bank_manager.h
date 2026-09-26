#pragma once

#include <stdint.h>
#include <stddef.h>
#include "audio/flash_layout.h"
#include "audio/bank_config.h"
#include "audio/sample_classes.h"

// Max per-voice slots tracked by the compiled-in-bank cache and (on
// ENABLE_LITTLEFS builds) the flash sample pool. Independent of
// BANK_CONFIG_MAX_SLOTS's own sizing constraints, but kept equal to it so a
// bank config file can address every cached slot.
#define BANK_MANAGER_MAX_VOICE_SLOTS BANK_CONFIG_MAX_SLOTS

// ---------------------------------------------------------------------------
// BankManager
//
// Manages compiled-in and LittleFS-configured sample banks (Phase C).
//
//  Bank 0  — compiled-in samples (always available; the original behaviour).
//  Banks 1..FLASH_MAX_BANKS — a small per-slot config file (see
//              audio/bank_config.h, BankConfigHost) at "/save/bank_N.txt"
//              that references samples in the shared content-addressed
//              SampleStore (audio/sample_store.h) by hash. Retires the old
//              per-bank raw-flash BankHeader format entirely - only
//              available when ENABLE_LITTLEFS is defined; builds without it
//              behave as if no flash banks are ever valid (same as
//              FLASH_MAX_BANKS == 0 today).
//
// Typical call sequence (from setup_samples(), AFTER sampleStore.setup()):
//
//   bankManager.setup();          // initialise compiled-in bank + detect banks
//
// Runtime bank switch (from main loop, core 0 only):
//
//   bankManager.switch_bank(2);   // switch to bank 2
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

    // Cached display name for a bank slot (1..MAX_FLASH_BANKS), read from its
    // config file's "name" field. Returns nullptr if the slot is out of
    // range or not valid.
    const char* get_bank_name(int slot) const;

    // Number of configured (non-empty) voice slots in a bank
    // (1..MAX_FLASH_BANKS). Returns 0 if the slot is out of range or not valid.
    int get_bank_slot_count(int slot) const;

    // Re-scans a single bank slot's config file (e.g. after a live SysEx
    // upload) and, if that slot is the currently active bank, hot-reloads
    // its sample assignments into the running audio engine without a
    // reboot. No-op on non-ENABLE_LITTLEFS builds.
    void refresh_bank(int slot);

#ifdef ENABLE_LITTLEFS
    // Debug-only: prints detailed per-slot info (sample name/rate/bit depth/
    // duration, per-slot volume/note) for a bank (1..MAX_FLASH_BANKS) to
    // Serial, loading its config file transiently. No-op if Serial isn't
    // connected or the slot has no config file.
    void debug_print_bank_detail(int slot);
#endif

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

    // Load a bank's config file (slot 1..MAX_FLASH_BANKS) into sample_data[].
    bool load_bank_config_(int slot);

    // Check one bank slot's config file for validity, updating
    // bank_valid_[slot]/bank_names_[slot]/bank_slot_counts_[slot].
    void detect_single_bank_(int slot);

    // Scan bank slots 1..MAX_FLASH_BANKS for a config file and populate
    // bank_valid_[]/bank_names_[]/bank_slot_counts_[].
    void detect_banks_();

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    // bank_valid_[0]  = true (compiled-in is always valid)
    // bank_valid_[n]  = true if bank slot n has a usable config file
    bool bank_valid_[MAX_FLASH_BANKS + 1] = {};

    // Cached per-slot display name / configured-slot count, populated by
    // detect_banks_() so configurator.h can report them for every bank
    // without having to load each bank's config file on demand.
    char    bank_names_[MAX_FLASH_BANKS + 1][24] = {};
    uint8_t bank_slot_counts_[MAX_FLASH_BANKS + 1] = {};

    int  active_bank_ = 0;

    // Cached compiled-in BaseSampleData pointers (so they can be restored).
    BaseSampleData* compiled_cache_[BANK_MANAGER_MAX_VOICE_SLOTS] = {};
    // Cached compiled-in sample[] metadata, so switching back to bank 0
    // restores the original note/volume/name rather than a flash bank's.
    uint8_t         compiled_midinote_[BANK_MANAGER_MAX_VOICE_SLOTS] = {};
    uint8_t         compiled_volume_[BANK_MANAGER_MAX_VOICE_SLOTS] = {};
    size_t          compiled_num_voices_ = 0;

    // Static pool of SampleDataFlash objects — avoids heap allocations after
    // the initial compiled-in bank setup.
    SampleDataFlash flash_pool_[BANK_MANAGER_MAX_VOICE_SLOTS];
    int             flash_pool_count_ = 0;

#ifdef ENABLE_LITTLEFS
    // Scratch host reused (via a temporary SL_ROOT swap) to load each bank
    // slot's config file - see detect_banks_()/load_bank_config_().
    BankConfigHost  bank_config_;
#endif
};

// Global singleton — defined in bank_manager.cpp.
extern BankManager bankManager;
