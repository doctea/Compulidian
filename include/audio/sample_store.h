#pragma once

#include <stdint.h>
#include "audio/flash_layout.h"
#include "audio/sample_classes.h"

// ---------------------------------------------------------------------------
// Content-addressed sample store (Phase B).
//
// Written by the PC-side upload tool via the same raw-flash/picoboot
// mechanism as legacy banks (see bank_header.h) - the device never writes to
// this region, only scans it at boot and resolves lookups by content hash.
// Samples are identified by a hash computed PC-side over
// {original_filename, original_length, PCM checksum}; the device just
// stores/compares the hash, it never computes or verifies it itself.
//
// Occupies the same physical flash region as the legacy per-bank blob
// storage (FLASH_SAMPLESTORE_OFFSET == FLASH_BANKS_OFFSET, see
// flash_layout.h) - Phase C retires the old per-bank blob format in favour
// of this shared store plus small per-bank config files, so the two formats
// are not meant to coexist there long-term.
// ---------------------------------------------------------------------------

#define SAMPLESTORE_MAGIC   0x73746F72u   // 'stor' (Compulidian sample sTORe)
#define SAMPLESTORE_VERSION 1u

// Maximum number of samples the store can index.
#define MAX_SAMPLESTORE_ENTRIES 256

// Per-sample entry, stored packed in SampleStoreHeader::entries[].
struct SampleStoreEntryHeader {
    uint32_t content_hash;   // content-addressed identity, computed PC-side.
    uint32_t data_offset;    // byte offset of this sample's PCM data, measured
                             // from FLASH_SAMPLESTORE_DATA_ADDR (after the index).
    uint32_t num_samples;    // number of PCM frames (int16_t values for 16-bit).
    uint32_t sample_rate;    // e.g. 44100, 22050, 24000.
    uint8_t  bit_depth;      // bits per sample (currently only 16 is supported).
    uint8_t  flags;          // bitfield, see SAMPLESTORE_ENTRY_FLAG_* below.
    uint8_t  reserved[2];
    char     name[24];       // null-terminated original filename - a
                             // convenience label only, not part of the
                             // content identity.
    uint8_t  _pad[4];
};
#define SAMPLESTORE_ENTRY_SIZE 48

#define SAMPLESTORE_ENTRY_FLAG_USED 0x01

// Sample metadata (Phase F.4): one small LittleFS text file, separate from
// the sample store's flash region, mapping content_hash -> {type, tags}.
// Purely informational for the PC tool's UI - firmware treats it as opaque
// bytes (like bank config files), never parses or acts on its contents.
// See tools/web/compulidian_manager.html for the line format.
#define SAMPLE_META_SAVE_PATH "/save/sample_meta.txt"

#ifdef __cplusplus
static_assert(sizeof(SampleStoreEntryHeader) == SAMPLESTORE_ENTRY_SIZE,
              "SampleStoreEntryHeader layout mismatch — check for unexpected padding");
#endif

// Index sector(s) reserved at the start of the sample store region.
#define SAMPLESTORE_INDEX_SIZE 0x00004000u   // 16 KB (4 x 4KB flash sectors)

struct SampleStoreHeader {
    uint32_t magic;               // must equal SAMPLESTORE_MAGIC
    uint32_t version;             // must equal SAMPLESTORE_VERSION
    uint32_t num_entries;         // valid slots in entries[] (check flags per-entry)
    uint32_t next_free_data_offset; // bump allocator: next unused byte, offset
                                     // from FLASH_SAMPLESTORE_DATA_ADDR
    uint8_t  reserved[16];
    SampleStoreEntryHeader entries[MAX_SAMPLESTORE_ENTRIES];
    uint8_t  _pad[SAMPLESTORE_INDEX_SIZE - 32 - (SAMPLESTORE_ENTRY_SIZE * MAX_SAMPLESTORE_ENTRIES)];
};

#ifdef __cplusplus
static_assert(sizeof(SampleStoreHeader) == SAMPLESTORE_INDEX_SIZE,
              "SampleStoreHeader must be exactly SAMPLESTORE_INDEX_SIZE bytes — check _pad calculation");
#endif

// PCM data region starts immediately after the index.
#define FLASH_SAMPLESTORE_DATA_OFFSET (FLASH_SAMPLESTORE_OFFSET + SAMPLESTORE_INDEX_SIZE)
#define FLASH_SAMPLESTORE_DATA_ADDR   (XIP_BASE + FLASH_SAMPLESTORE_DATA_OFFSET)

// ---------------------------------------------------------------------------
// SampleStore — read-only device-side view of the index above.
// ---------------------------------------------------------------------------
class SampleStore {
public:
    // Validate the index sector. Safe to call even if the region is
    // blank/invalid (e.g. on a 2 MB board, or before any upload) - just
    // yields num_entries() == 0 / is_valid() == false.
    void setup();

    bool     is_valid()    const { return valid_; }
    uint32_t num_entries() const { return valid_ ? header_->num_entries : 0; }

    // Look up a sample by its content hash. Returns nullptr if not found or
    // the store itself is invalid.
    const SampleStoreEntryHeader* find_by_hash(uint32_t content_hash) const;

    // Entry at raw index (0..num_entries()-1), for enumeration/debugging.
    // Returns nullptr if out of range or the store is invalid.
    const SampleStoreEntryHeader* entry_at(uint32_t index) const;

    // XIP address of a given entry's PCM data.
    static uint32_t entry_xip_addr(const SampleStoreEntryHeader *entry);

    // Convenience: populate a SampleDataFlash in place from a content hash.
    // Returns false (leaving *out untouched) if the hash is not found.
    bool get_sample_data(uint32_t content_hash, SampleDataFlash *out) const;

private:
    const SampleStoreHeader *header_ = nullptr;
    bool                     valid_  = false;
};

extern SampleStore sampleStore;
