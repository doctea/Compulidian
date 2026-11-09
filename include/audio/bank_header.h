#pragma once
#include <stdint.h>

// Magic number written at the start of every valid bank header: "cmpd" (Compulidian).
#define BANK_MAGIC            0x636D7064u
#define BANK_VERSION          1u

// Maximum number of samples a single bank can hold.
#define MAX_SAMPLES_PER_BANK  64

// The bank header occupies exactly one 4 KB flash sector so that erasing the
// header alone is a clean, sector-aligned operation.  Sample data follows
// immediately in the next sector(s).
#define BANK_HEADER_SIZE      4096u

// ---------------------------------------------------------------------------
// Per-sample entry — 40 bytes, stored packed in BankHeader::entries[].
// ---------------------------------------------------------------------------
struct BankEntryHeader {
    uint32_t offset;       // byte offset of this sample's PCM data, measured
                           // from (bank_addr + BANK_HEADER_SIZE)
    uint32_t num_samples;  // number of PCM frames (int16_t values for 16-bit)
    uint32_t sample_rate;  // e.g. 44100, 22050, 24000
    uint8_t  bit_depth;    // bits per sample (currently only 16 is supported)
    uint8_t  midi_note;    // MIDI note number that triggers this sample - must
                           // match the active output processor's slot note at
                           // this same entry index, or it will never trigger
    uint8_t  volume;       // default playback volume 0-127
    uint8_t  flags;        // bitfield, see BANK_ENTRY_FLAG_* below
    char     name[24];     // null-terminated original sample filename (max 23
                           // chars + NUL) - a convenience label only, not the
                           // slot's identity (that comes from the device's
                           // output processor, see SYSEX_TYPE_GET_SLOTS_REQ)
};
// sizeof(BankEntryHeader) == 40

#define BANK_ENTRY_SIZE 40   // keep in sync with the struct above

// When set on an empty entry (num_samples == 0), the device restores that
// specific voice's compiled-in sample instead of leaving it silent.
#define BANK_ENTRY_FLAG_FALLBACK_TO_COMPILED 0x01

// Compile-time size guard (C++ only).
#ifdef __cplusplus
static_assert(sizeof(BankEntryHeader) == BANK_ENTRY_SIZE,
              "BankEntryHeader layout mismatch — check for unexpected padding");
#endif

// ---------------------------------------------------------------------------
// Bank header — exactly BANK_HEADER_SIZE bytes (one 4 KB flash sector).
// Stored at the very start of each flash bank slot.
// ---------------------------------------------------------------------------
//
// Layout (byte offsets):
//   0   magic        4 B
//   4   version      4 B
//   8   num_samples  4 B
//  12   reserved     4 B
//  16   bank_name   32 B
//  48   entries     64 × 40 B = 2560 B
// 2608  _pad      1488 B
// 4096  (end)
//
struct BankHeader {
    uint32_t        magic;                           // must equal BANK_MAGIC
    uint32_t        version;                         // must equal BANK_VERSION
    uint32_t        num_samples;                     // valid entries in entries[]
    uint8_t         reserved[4];                     // must be 0
    char            bank_name[32];                   // display name (max 31 chars + NUL)
    BankEntryHeader entries[MAX_SAMPLES_PER_BANK];   // 64 × 40 = 2560 B
    uint8_t         _pad[BANK_HEADER_SIZE
                         - 48                        // fields above entries
                         - BANK_ENTRY_SIZE * MAX_SAMPLES_PER_BANK];  // 1488 B
};

#ifdef __cplusplus
static_assert(sizeof(BankHeader) == BANK_HEADER_SIZE,
              "BankHeader must be exactly 4096 bytes — check _pad calculation");
#endif
