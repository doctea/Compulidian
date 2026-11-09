#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Per-bank-slot configuration (Phase C).
//
// Replaces include/audio/bank_header.h's BankHeader/BankEntryHeader raw-flash
// format: a bank is now just a small text file (one per slot, saveloadlib
// format) that references samples in the shared content-addressed
// SampleStore (see include/audio/sample_store.h) by hash, instead of
// embedding raw PCM per bank. Written by a future PC-side bank-config tool;
// the device only ever reads these files.
// ---------------------------------------------------------------------------

// Cap on configurable slots per bank. Sized comfortably above the current
// voice count (17, see src/audio/audio.cpp) with headroom for growth, while
// keeping the packed hex line well under saveloadlib's SL_MAX_LINE (512
// bytes): 32 slots x 7 bytes = 224 bytes -> 448 hex chars.
#define BANK_CONFIG_MAX_SLOTS 32

// Reinterprets a slot's sample_id as a 1-based compiled-in-voice index
// (rather than a SampleStore content hash) when set - lets banks reference
// built-in samples directly as a first-class picker choice. Superseded the
// old "fallback to compiled if unresolved" meaning of this same bit; that
// safety net is now unconditional (see BankManager::load_bank_config_()).
#define BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX 0x01

#define BANK_CONFIG_SAVE_PATH_FMT "/save/bank_%d.txt"

// One entry per voice slot in a bank. Packed to a fixed 7 bytes (no compiler
// padding) so it round-trips identically through the PC-side packer's hex
// encoding - see SlotConfig static_assert below.
#pragma pack(push, 1)
struct SlotConfig {
    uint32_t sample_id;   // SampleStore content_hash reference; 0 = unassigned.
    uint8_t  volume;      // 0..255, mirrors sample[].play_volume.
    uint8_t  midi_note;   // MIDI note that triggers this voice (mirrors the
                          // old BankEntryHeader.midi_note).
    uint8_t  flags;       // BANK_CONFIG_SLOT_FLAG_* bits.
};
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(SlotConfig) == 7,
              "SlotConfig must be tightly packed to 7 bytes to match the PC-side packer");
#endif

// The saveloadlib-backed host type is only usable on ENABLE_LITTLEFS builds
// (saveloadlib's file I/O only links when ENABLE_LITTLEFS or ENABLE_SD is
// defined - see saveloadlib.cpp). SlotConfig/the constants above are plain
// data and stay available unconditionally so BankManager can size its
// compiled-in-bank caches the same way on every board.
#ifdef ENABLE_LITTLEFS

#include "saveload_settings.h"

// One instance is reused (via a temporary SL_ROOT swap) to load each bank
// slot's config file in turn - see BankManager::detect_banks_()/load_bank_config_().
class BankConfigHost : public SHStorage<0, 2> {
public:
    SlotConfig slots[BANK_CONFIG_MAX_SLOTS] = {};
    char       bank_name[24] = {};

    BankConfigHost() { set_path_segment("BankConfig"); }

    virtual void setup_saveable_settings() override {
        ISaveableSettingHost::setup_saveable_settings();
        register_setting(new SaveableByteArraySetting<uint8_t>(
            "slots", "BankConfig", reinterpret_cast<uint8_t*>(slots), sizeof(slots)), SL_SCOPE_PROJECT);
        register_setting(new SaveableByteArraySetting<uint8_t>(
            "name", "BankConfig", reinterpret_cast<uint8_t*>(bank_name), sizeof(bank_name)), SL_SCOPE_PROJECT);
    }

    void reset() {
        memset(slots, 0, sizeof(slots));
        memset(bank_name, 0, sizeof(bank_name));
    }
};

#endif // ENABLE_LITTLEFS
