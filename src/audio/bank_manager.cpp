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
        compiled_cache_[i] = sample_data[voice[i].sample];
    }
}

void BankManager::restore_compiled_bank_() {
    release_flash_pool_();
    for (int i = 0; i < (int)compiled_num_voices_ && i < MAX_SAMPLES_PER_BANK; ++i) {
        sample_data[voice[i].sample] = compiled_cache_[i];
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
        if (e.num_samples == 0) continue; // skip empty / placeholder entries

        // Initialise pool entry in-place (no heap allocation).
        flash_pool_[flash_pool_count_].init(data_base + e.offset, e.num_samples);

        int si = voice[i].sample; // voice i plays sample slot si
        sample_data[si] = &flash_pool_[flash_pool_count_];

        // Silence the voice so playback can be triggered freshly.
        voice[i].sampleindex = (uint32_t)e.num_samples << 12;

        ++flash_pool_count_;
    }

    // Silence any voices beyond what the bank provides.
    for (int i = (int)voices_to_load; i < NUM_VOICES; ++i) {
        voice[i].sampleindex = 0xFFFFFFFFu;
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
