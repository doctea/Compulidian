#include "audio/bank_manager.h"
#include "audio/samps.h"   // sample[], NUM_SAMPLES, sample_data
#include "audio/audio.h"   // voice[], NUM_VOICES
#include "settings.h"

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

    // 4. Detect which flash bank slots contain valid headers.
    detect_banks_();

    // 5. Load the bank stored in settings (or fall back to compiled-in).
    Settings s = settings_load();
    if (s.active_bank > 0 && s.active_bank <= MAX_FLASH_BANKS && bank_valid_[s.active_bank]) {
        load_flash_bank_(s.active_bank);
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

const BankHeader* BankManager::get_bank_header(int slot) const {
    if (slot < 1 || slot > MAX_FLASH_BANKS) return nullptr;
    if (!bank_valid_[slot]) return nullptr;
    return reinterpret_cast<const BankHeader*>(flash_bank_xip_addr(slot));
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
        ok = load_flash_bank_(n);
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
    for (int i = 0; i < NUM_VOICES && i < MAX_SAMPLES_PER_BANK; ++i) {
        int si = voice[i].sample;
        compiled_cache_[i]    = sample_data[si];
        compiled_midinote_[i] = sample[si].MIDINOTE;
        compiled_volume_[i]   = sample[si].play_volume;
    }
}

void BankManager::restore_compiled_bank_() {
    release_flash_pool_();
    for (int i = 0; i < (int)compiled_num_voices_ && i < MAX_SAMPLES_PER_BANK; ++i) {
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

bool BankManager::load_flash_bank_(int slot) {
    if (slot < 1 || slot > MAX_FLASH_BANKS) return false;

    const BankHeader *hdr = reinterpret_cast<const BankHeader *>(flash_bank_xip_addr(slot));
    if (Serial) Serial.printf("BankMgr: load slot %d @ 0x%08X magic=0x%08X ver=%u n=%u\n",
        slot, flash_bank_xip_addr(slot), hdr->magic, hdr->version, hdr->num_samples);

    if (hdr->magic != BANK_MAGIC || hdr->version != BANK_VERSION) {
        if (Serial) Serial.printf("BankMgr: load FAILED - bad magic/version\n");
        return false;
    }

    uint32_t num = hdr->num_samples;
    if (num == 0 || num > MAX_SAMPLES_PER_BANK) return false;

    // Base XIP address of the PCM data region (immediately after the header).
    uint32_t data_base = flash_bank_xip_addr(slot) + BANK_HEADER_SIZE;

    release_flash_pool_();

    uint32_t voices_to_load = (num < (uint32_t)NUM_VOICES) ? num : (uint32_t)NUM_VOICES;
    flash_pool_count_ = 0;

    for (uint32_t i = 0; i < voices_to_load; ++i) {
        const BankEntryHeader &e = hdr->entries[i];
        int si = voice[i].sample; // voice i plays sample slot si

        // Raw dump of every entry as read from flash - temporary diagnostic
        // to help pin down corrupt bank data vs. a genuine playback bug.
        if (Serial) Serial.printf("BankMgr: slot %d entry %u raw: offset=%u num_samples=%u rate=%u bits=%u note=%u vol=%u flags=0x%02X name='%.*s'\n",
            slot, i, e.offset, e.num_samples, e.sample_rate, e.bit_depth, e.midi_note, e.volume, e.flags, 23, e.name);

        // An entry is treated as unusable (and falls back the same way an
        // empty slot would) if it fails basic sanity checks — this guards
        // against corrupt/truncated bank data (e.g. an interrupted flash
        // write) crashing the audio ISR instead of just staying silent.
        bool usable = e.num_samples != 0;
        if (usable && e.bit_depth != 16) {
            if (Serial) Serial.printf("BankMgr: slot %d entry %u has unsupported bit_depth %u - skipping\n",
                slot, i, e.bit_depth);
            usable = false;
        }
        if (usable) {
            uint64_t pcm_region_size = (uint64_t)FLASH_BANK_SIZE - BANK_HEADER_SIZE;
            uint64_t entry_end = (uint64_t)e.offset + (uint64_t)e.num_samples * 2u;
            if (entry_end > pcm_region_size) {
                if (Serial) Serial.printf("BankMgr: slot %d entry %u out of bounds (offset=%u num_samples=%u) - skipping\n",
                    slot, i, e.offset, e.num_samples);
                usable = false;
            }
        }
        // Guard against the sampleindex sentinel calculation overflowing
        // uint32_t below (num_samples << 12) for implausibly long samples.
        if (usable && e.num_samples > (0xFFFFFFFFu >> 12)) {
            if (Serial) Serial.printf("BankMgr: slot %d entry %u num_samples %u too large (sampleindex would overflow) - skipping\n",
                slot, i, e.num_samples);
            usable = false;
        }

        if (!usable) {
            // Unusable/empty slot: fall back to this voice's compiled-in sample if
            // requested, otherwise leave it silent.
            if ((e.flags & BANK_ENTRY_FLAG_FALLBACK_TO_COMPILED) && i < compiled_num_voices_) {
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

        // Initialise pool entry in-place (no heap allocation).
        flash_pool_[flash_pool_count_].init(data_base + e.offset, e.num_samples);

        sample_data[si] = &flash_pool_[flash_pool_count_];

        // Match this bank's own note/volume/name assignments, not the
        // compiled-in bank's - otherwise get_voice_number_for_note() keeps
        // matching against stale MIDINOTE values from the previous bank.
        sample[si].MIDINOTE    = e.midi_note;
        sample[si].play_volume = e.volume;
        strncpy(sample[si].sname, e.name, sizeof(sample[si].sname) - 1);
        sample[si].sname[sizeof(sample[si].sname) - 1] = '\0';

        // Silence the voice so playback can be triggered freshly.
        voice[i].sampleindex = (uint32_t)e.num_samples << 12;

        ++flash_pool_count_;
    }

    // Silence any voices beyond what the bank provides, and invalidate their
    // notes so a stale MIDINOTE can't route a trigger to leftover/null sample_data.
    for (int i = (int)voices_to_load; i < NUM_VOICES; ++i) {
        voice[i].sampleindex = 0xFFFFFFFFu;
        sample[voice[i].sample].MIDINOTE = 0xFF;
    }

    return (flash_pool_count_ > 0);
}

void BankManager::detect_banks_() {
    for (int slot = 1; slot <= MAX_FLASH_BANKS; ++slot) {
        uint32_t addr = flash_bank_xip_addr(slot);

        // Guard: ensure the bank's XIP address is within the device's flash.
        if (addr >= XIP_BASE + FLASH_SIZE_BYTES) {
            bank_valid_[slot] = false;
            if (Serial) Serial.printf("BankMgr: slot %d @ 0x%08X SKIP (beyond flash end 0x%08X)\n",
                slot, addr, XIP_BASE + FLASH_SIZE_BYTES);
            continue;
        }

        const BankHeader *hdr = reinterpret_cast<const BankHeader *>(addr);
        uint32_t got_magic   = hdr->magic;
        uint32_t got_version = hdr->version;
        uint32_t got_num     = hdr->num_samples;
        bool ok = (got_magic == BANK_MAGIC && got_version == BANK_VERSION
                   && got_num > 0 && got_num <= MAX_SAMPLES_PER_BANK);
        bank_valid_[slot] = ok;

        if (Serial) {
            if (ok) {
                Serial.printf("BankMgr: slot %d @ 0x%08X  VALID  magic=0x%08X ver=%u n=%u name='%.*s'\n",
                    slot, addr, got_magic, got_version, got_num, 31, hdr->bank_name);
            } else {
                Serial.printf("BankMgr: slot %d @ 0x%08X  empty  magic=0x%08X (want 0x%08X) ver=%u n=%u\n",
                    slot, addr, got_magic, BANK_MAGIC, got_version, got_num);
            }
        }
    }
}
