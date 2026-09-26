#include "audio/sample_store.h"

SampleStore sampleStore;

void SampleStore::setup() {
    header_ = reinterpret_cast<const SampleStoreHeader *>(FLASH_SAMPLESTORE_ADDR);
    valid_ = FLASH_SAMPLESTORE_SIZE > SAMPLESTORE_INDEX_SIZE
           && header_->magic == SAMPLESTORE_MAGIC
           && header_->version == SAMPLESTORE_VERSION
           && header_->num_entries <= MAX_SAMPLESTORE_ENTRIES;
}

const SampleStoreEntryHeader* SampleStore::find_by_hash(uint32_t content_hash) const {
    if (!valid_) return nullptr;
    for (uint32_t i = 0; i < header_->num_entries; i++) {
        const SampleStoreEntryHeader &e = header_->entries[i];
        if ((e.flags & SAMPLESTORE_ENTRY_FLAG_USED) && e.content_hash == content_hash) {
            return &e;
        }
    }
    return nullptr;
}

const SampleStoreEntryHeader* SampleStore::entry_at(uint32_t index) const {
    if (!valid_ || index >= header_->num_entries) return nullptr;
    return &header_->entries[index];
}

uint32_t SampleStore::entry_xip_addr(const SampleStoreEntryHeader *entry) {
    return FLASH_SAMPLESTORE_DATA_ADDR + entry->data_offset;
}

bool SampleStore::get_sample_data(uint32_t content_hash, SampleDataFlash *out) const {
    const SampleStoreEntryHeader *e = find_by_hash(content_hash);
    if (!e || !out) return false;
    out->init(entry_xip_addr(e), e->num_samples);
    return true;
}
