#include "audio/bank_manager.h"
#include "audio/samps.h"   // sample[], NUM_SAMPLES, sample_data
#include "audio/audio.h"   // voice[], NUM_VOICES
#include "settings.h"

#ifdef ENABLE_LITTLEFS
#include "audio/sample_store.h"
#include <LittleFS.h>
#endif

#include <Arduino.h>
#include <string.h>

// Global singleton
BankManager bankManager;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BankManager::setup() {
    // 1. Allocate the sample_data pointer array (compiled-in size).
    //    This array persists for the lifetime of the device; we swap the
    //    individual pointers inside it when switching banks.
    if (sample_data == nullptr) {
        sample_data = (BaseSampleData**)calloc(NUM_SAMPLES, sizeof(BaseSampleData*));
    }

    // 2. Populate compiled-in bank (bank 0).
    init_compiled_bank_();

    // 3. Cache compiled-in pointers so they can be restored later.
    cache_compiled_bank_();
    bank_valid_[0] = true;

#ifdef ENABLE_LITTLEFS
    // Register bank_config_'s settings exactly once - detect_banks_()/
    // load_bank_config_() below only ever swap SL_ROOT to point at it, they
    // don't re-run setup (that would overflow its fixed-size settings[]).
    sl_setup_all(&bank_config_);
#endif

    // 4. Detect which bank slots have a usable config file.
    //    Requires sampleStore to already be set up (see setup_samples() in
    //    audio.cpp) so slot-count validation below can resolve sample_ids.
    detect_banks_();

    // 5. Load the bank stored in settings (or fall back to compiled-in).
    Settings s = settings_load();
    if (s.active_bank > 0 && s.active_bank <= MAX_FLASH_BANKS && bank_valid_[s.active_bank]) {
        load_bank_config_(s.active_bank);
        active_bank_ = s.active_bank;
    } else {
        active_bank_ = 0;
    }
}

int BankManager::num_valid_banks() const {
    int count = 0;
    for (int i = 0; i <= MAX_FLASH_BANKS; ++i) {
        if (bank_valid_[i]) ++count;
    }
    return count;
}

bool BankManager::is_bank_valid(int n) const {
    if (n < 0 || n > MAX_FLASH_BANKS) return false;
    return bank_valid_[n];
}

const char* BankManager::get_bank_name(int slot) const {
    if (slot < 1 || slot > MAX_FLASH_BANKS) return nullptr;
    if (!bank_valid_[slot]) return nullptr;
    return bank_names_[slot];
}

int BankManager::get_bank_slot_count(int slot) const {
    if (slot < 1 || slot > MAX_FLASH_BANKS) return 0;
    if (!bank_valid_[slot]) return 0;
    return bank_slot_counts_[slot];
}

bool BankManager::switch_bank(int n) {
    if (n < 0 || n > MAX_FLASH_BANKS) return false;
    if (!bank_valid_[n]) return false;
    if (n == active_bank_) return true; // already active

    // Silence all voices first so the audio ISR stops touching sample_data.
    silence_all_voices_();

    // Give the audio worker time to finish any in-progress buffer fill.
    // At 48 kHz with a 128-sample buffer each fill takes ~2.7 ms.
    // @@TODO: Use a semaphore or flag to wait for the audio ISR to finish instead of a fixed delay.
    delay(6);

    bool ok;
    if (n == 0) {
        restore_compiled_bank_();
        ok = true;
    } else {
        ok = load_bank_config_(n);
    }

    if (ok) {
        active_bank_ = n;
        if (Serial) Serial.printf("BankMgr: switched to bank %d\n", n);

        // Persist new bank choice to flash.
        Settings s = settings_load();
        s.active_bank = (uint8_t)n;
        settings_save(&s);
    }

    return ok;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void BankManager::init_compiled_bank_() {
    // Mirror of the original setup_samples() body: initialise voice positions
    // and wrap each compiled-in sample array in a SampleDataArray.
    for (int i = 0; i < NUM_VOICES; ++i) {
        int si = voice[i].sample; // == i in the default mapping
        // Start with sampleindex past the end → silent.
        voice[i].sampleindex = (uint32_t)sample[si].samplesize << 12;
        voice[i].level       = sample[si].play_volume / 8;

        if (sample_data[si] == nullptr) {
            sample_data[si] = new SampleDataArray(
                sample[si].samplearray,
                sample[si].samplesize);
        }
    }
    compiled_num_voices_ = (size_t)NUM_VOICES;
}

void BankManager::cache_compiled_bank_() {
    for (int i = 0; i < NUM_VOICES && i < BANK_MANAGER_MAX_VOICE_SLOTS; ++i) {
        int si = voice[i].sample;
        compiled_cache_[i]    = sample_data[si];
        compiled_midinote_[i] = sample[si].MIDINOTE;
        compiled_volume_[i]   = sample[si].play_volume;
    }
}

void BankManager::restore_compiled_bank_() {
    release_flash_pool_();
    for (int i = 0; i < (int)compiled_num_voices_ && i < BANK_MANAGER_MAX_VOICE_SLOTS; ++i) {
        int si = voice[i].sample;
        sample_data[si] = compiled_cache_[i];
        sample[si].MIDINOTE    = compiled_midinote_[i];
        sample[si].play_volume = compiled_volume_[i];
        // Silence the voice so it re-starts cleanly.
        voice[i].sampleindex = compiled_cache_[i]
            ? (uint32_t)compiled_cache_[i]->size() << 12
            : 0xFFFFFFFFu;
    }
}

void BankManager::release_flash_pool_() {
    // SampleDataFlash objects live in the static flash_pool_ array, so there
    // is nothing to free; just zero out sample_data[] entries that point to
    // them and reset the count.
    for (int i = 0; i < flash_pool_count_; ++i) {
        // Find which sample_data slot(s) point to this pool entry and clear.
        for (int si = 0; si < NUM_SAMPLES; ++si) {
            if (sample_data[si] == &flash_pool_[i]) {
                sample_data[si] = nullptr;
            }
        }
    }
    flash_pool_count_ = 0;
}

void BankManager::silence_all_voices_() {
    for (int i = 0; i < NUM_VOICES; ++i) {
        // Setting sampleindex to the maximum value guarantees the audio loop
        // will not read from sample_data[voice[i].sample] for this voice.
        voice[i].sampleindex = 0xFFFFFFFFu;
    }
}

#ifdef ENABLE_LITTLEFS
// Loads path into bank_config_ via a temporary SL_ROOT swap, restoring the
// previous root afterwards regardless of success/failure.
static bool load_bank_config_file_(BankConfigHost &host, const char *path) {
    ISaveableSettingHost *prev_root = SL_ROOT;
    host.reset();
    sl_register_root(&host);
    bool ok = sl_load_from_file(path, SL_SCOPE_PROJECT);
    sl_register_root(prev_root);
    return ok;
}
#endif

bool BankManager::load_bank_config_(int slot) {
#ifdef ENABLE_LITTLEFS
    if (slot < 1 || slot > MAX_FLASH_BANKS) return false;

    char path[32];
    snprintf(path, sizeof(path), BANK_CONFIG_SAVE_PATH_FMT, slot);

    if (!load_bank_config_file_(bank_config_, path)) {
        if (Serial) Serial.printf("BankMgr: load FAILED - could not read %s\n", path);
        return false;
    }

    release_flash_pool_();

    uint32_t voices_to_load = (uint32_t)NUM_VOICES;
    if (voices_to_load > (uint32_t)BANK_CONFIG_MAX_SLOTS) voices_to_load = BANK_CONFIG_MAX_SLOTS;
    flash_pool_count_ = 0;

    for (uint32_t i = 0; i < voices_to_load; ++i) {
        const SlotConfig &slotcfg = bank_config_.slots[i];
        int si = voice[i].sample; // voice i plays sample slot si

        // Explicitly unassigned: stay silent. Distinct from "assigned but
        // unresolvable" below - only the latter falls back to compiled-in.
        if (slotcfg.sample_id == 0) {
            sample_data[si] = nullptr;
            sample[si].MIDINOTE = 0xFF;
            voice[i].sampleindex = 0xFFFFFFFFu;
            continue;
        }

        // sample_id reinterpreted as a 1-based compiled-voice index (so 0
        // stays reserved for "unassigned" above) rather than a SampleStore
        // hash - lets the picker offer built-in samples as first-class
        // choices, not just as an implicit fallback.
        if (slotcfg.flags & BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX) {
            uint32_t ci = slotcfg.sample_id - 1;
            if (ci < compiled_num_voices_) {
                sample_data[si] = compiled_cache_[ci];
                sample[si].MIDINOTE    = slotcfg.midi_note;
                sample[si].play_volume = slotcfg.volume;
                voice[i].sampleindex = compiled_cache_[ci]
                    ? (uint32_t)compiled_cache_[ci]->size() << 12
                    : 0xFFFFFFFFu;
            } else {
                if (Serial) Serial.printf("BankMgr: slot %d voice %u compiled index %u out of range - skipping\n",
                    slot, i, (unsigned)ci);
                sample_data[si] = nullptr;
                sample[si].MIDINOTE = 0xFF;
                voice[i].sampleindex = 0xFFFFFFFFu;
            }
            continue;
        }

        const SampleStoreEntryHeader *entry = sampleStore.is_valid()
            ? sampleStore.find_by_hash(slotcfg.sample_id) : nullptr;

        if (!entry) {
            if (Serial) {
                Serial.printf("BankMgr: slot %d voice %u sample_id 0x%08X not found in SampleStore - falling back to compiled\n",
                    slot, i, slotcfg.sample_id);
            }
            // Assigned but unresolvable (e.g. a since-deleted sample): always
            // fall back to this voice's compiled-in sample - unconditional,
            // unlike the old opt-in fallback flag.
            if (i < compiled_num_voices_) {
                sample_data[si] = compiled_cache_[i];
                sample[si].MIDINOTE    = compiled_midinote_[i];
                sample[si].play_volume = compiled_volume_[i];
                voice[i].sampleindex = compiled_cache_[i]
                    ? (uint32_t)compiled_cache_[i]->size() << 12
                    : 0xFFFFFFFFu;
            } else {
                sample_data[si] = nullptr;
                // Invalidate the note too - otherwise get_voice_number_for_note()
                // can still match this voice against a stale MIDINOTE left over
                // from the compiled-in bank/a previous load, and route a real
                // trigger into a null sample_data pointer.
                sample[si].MIDINOTE = 0xFF;
                voice[i].sampleindex = 0xFFFFFFFFu;
            }
            continue;
        }

        if (!sampleStore.get_sample_data(slotcfg.sample_id, &flash_pool_[flash_pool_count_])) {
            // Shouldn't happen (find_by_hash just succeeded above), but stay
            // defensive rather than risk the audio ISR reading a half-init'd pool entry.
            sample_data[si] = nullptr;
            sample[si].MIDINOTE = 0xFF;
            voice[i].sampleindex = 0xFFFFFFFFu;
            continue;
        }

        sample_data[si] = &flash_pool_[flash_pool_count_];

        // Match this bank's own note/volume/name assignments, not the
        // compiled-in bank's - otherwise get_voice_number_for_note() keeps
        // matching against stale MIDINOTE values from the previous bank.
        sample[si].MIDINOTE    = slotcfg.midi_note;
        sample[si].play_volume = slotcfg.volume;
        strncpy(sample[si].sname, entry->name, sizeof(sample[si].sname) - 1);
        sample[si].sname[sizeof(sample[si].sname) - 1] = '\0';

        // Silence the voice so playback can be triggered freshly.
        voice[i].sampleindex = (uint32_t)flash_pool_[flash_pool_count_].size() << 12;

        ++flash_pool_count_;
    }

    // Silence any voices beyond what the bank config covers, and invalidate
    // their notes so a stale MIDINOTE can't route a trigger to leftover/null sample_data.
    for (int i = (int)voices_to_load; i < NUM_VOICES; ++i) {
        voice[i].sampleindex = 0xFFFFFFFFu;
        sample[voice[i].sample].MIDINOTE = 0xFF;
    }

    return (flash_pool_count_ > 0);
#else
    (void)slot;
    return false;
#endif
}

void BankManager::detect_single_bank_(int slot) {
#ifdef ENABLE_LITTLEFS
    char path[32];
    snprintf(path, sizeof(path), BANK_CONFIG_SAVE_PATH_FMT, slot);

    bank_valid_[slot] = false;
    bank_names_[slot][0] = '\0';
    bank_slot_counts_[slot] = 0;

    if (!LittleFS.exists(path)) {
        if (Serial) Serial.printf("BankMgr: slot %d @ %s  empty (no config file)\n", slot, path);
        return;
    }

    if (!load_bank_config_file_(bank_config_, path)) {
        if (Serial) Serial.printf("BankMgr: slot %d @ %s  FAILED to load\n", slot, path);
        return;
    }

    uint8_t used = 0;
    for (int i = 0; i < BANK_CONFIG_MAX_SLOTS; ++i) {
        if (bank_config_.slots[i].sample_id != 0) ++used;
    }
    if (used == 0) {
        if (Serial) Serial.printf("BankMgr: slot %d @ %s  no samples assigned - treating as empty\n", slot, path);
        return;
    }

    strncpy(bank_names_[slot], bank_config_.bank_name, sizeof(bank_names_[slot]) - 1);
    bank_names_[slot][sizeof(bank_names_[slot]) - 1] = '\0';
    bank_slot_counts_[slot] = used;
    bank_valid_[slot] = true;

    if (Serial) Serial.printf("BankMgr: slot %d @ %s  VALID  name='%s' slots_used=%u\n",
        slot, path, bank_names_[slot], used);
#else
    (void)slot;
#endif
}

void BankManager::detect_banks_() {
    for (int slot = 1; slot <= MAX_FLASH_BANKS; ++slot) {
#ifdef ENABLE_LITTLEFS
        detect_single_bank_(slot);
#else
        bank_valid_[slot] = false;
#endif
    }
}

void BankManager::refresh_bank(int slot) {
#ifdef ENABLE_LITTLEFS
    if (slot < 1 || slot > MAX_FLASH_BANKS) return;

    detect_single_bank_(slot);

    if (slot != active_bank_) return; // not currently playing - cache update is enough

    if (!bank_valid_[slot]) {
        // The active bank's config just became invalid (e.g. emptied out) -
        // silence rather than risk playing back a half-applied config.
        if (Serial) Serial.printf("BankMgr: active bank %d became invalid after refresh - silencing\n", slot);
        silence_all_voices_();
        return;
    }

    // Mirrors switch_bank()'s audio-safety dance (silence, let any in-flight
    // buffer fill finish) since we're swapping sample_data[] pointers live.
    silence_all_voices_();
    delay(6);
    load_bank_config_(slot);
    if (Serial) Serial.printf("BankMgr: hot-reloaded active bank %d after live config upload\n", slot);
#else
    (void)slot;
#endif
}

#ifdef ENABLE_LITTLEFS
void BankManager::debug_print_bank_detail(int slot) {
    if (!Serial) return;
    if (slot < 1 || slot > MAX_FLASH_BANKS) {
        Serial.printf("BankMgr: slot %d out of range\n", slot);
        return;
    }

    char path[32];
    snprintf(path, sizeof(path), BANK_CONFIG_SAVE_PATH_FMT, slot);

    if (!LittleFS.exists(path) || !load_bank_config_file_(bank_config_, path)) {
        Serial.printf("  slot %d @ %s: empty\n", slot, path);
        return;
    }

    Serial.printf("  slot %d @ %s: name='%s'\n", slot, path, bank_config_.bank_name);
    for (int i = 0; i < BANK_CONFIG_MAX_SLOTS; ++i) {
        const SlotConfig &sc = bank_config_.slots[i];
        if (sc.sample_id == 0) continue;

        if (sc.flags & BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX) {
            uint32_t ci = sc.sample_id - 1;
            Serial.printf("    [%2d] compiled voice #%u  note=%3u  vol=%3u%s\n",
                i, (unsigned)ci, sc.midi_note, sc.volume,
                (ci < compiled_num_voices_) ? "" : "  OUT OF RANGE");
            continue;
        }

        const SampleStoreEntryHeader *entry = sampleStore.is_valid()
            ? sampleStore.find_by_hash(sc.sample_id) : nullptr;
        if (!entry) {
            Serial.printf("    [%2d] sample_id=0x%08X  NOT FOUND in SampleStore  vol=%3u note=%3u flags=0x%02X\n",
                i, sc.sample_id, sc.volume, sc.midi_note, sc.flags);
            continue;
        }

        float dur = entry->sample_rate > 0 ? (float)entry->num_samples / entry->sample_rate : 0.f;
        Serial.printf("    [%2d] %-23s  note=%3u  vol=%3u  %5u Hz  %ub=%u (%.2fs)  id=0x%08X\n",
            i, entry->name[0] ? entry->name : "(unnamed)",
            sc.midi_note, sc.volume, entry->sample_rate,
            entry->bit_depth, entry->num_samples, dur, sc.sample_id);
    }
}
#endif // ENABLE_LITTLEFS



