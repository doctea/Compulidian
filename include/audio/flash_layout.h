#pragma once
#include <stdint.h>

// XIP (execute-in-place) base — flash is memory-mapped here on RP2040
#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

// Total flash size in MB.
// Override at build time with -DFLASH_SIZE_MB=16 for extended-flash Workshop Computer cards.
// Default is 2MB (standard Raspberry Pi Pico / RP2040).
#ifndef FLASH_SIZE_MB
#define FLASH_SIZE_MB 2
#endif

#define FLASH_SIZE_BYTES ((uint32_t)(FLASH_SIZE_MB) * 1024u * 1024u)

// ---------------------------------------------------------------------------
// Firmware reserve
// ---------------------------------------------------------------------------
// The compiled firmware (code + const 808 sample arrays) is currently ~1.73 MB.
// Round up to 1.9375 MB with headroom for future growth.
// Banks and settings are placed AFTER this reserve, so firmware reflashing
// (UF2 drag-drop or picoboot) never touches bank data.

#define FLASH_FIRMWARE_SIZE     0x001F0000u   // 1.9375 MB

// ---------------------------------------------------------------------------
// Settings sector
// ---------------------------------------------------------------------------
// One 64 KB aligned block reserved for device settings.
// Only the first 4 KB sector is actually written; the rest is headroom.

#define FLASH_SETTINGS_OFFSET   FLASH_FIRMWARE_SIZE          // offset from flash start
#define FLASH_SETTINGS_ADDR     (XIP_BASE + FLASH_SETTINGS_OFFSET)
#define FLASH_SETTINGS_RESERVED 0x00010000u   // 64 KB reserved block

// ---------------------------------------------------------------------------
// Sample bank region
// ---------------------------------------------------------------------------
// Banks start at exactly the 2 MB mark — a clean boundary that is safely
// beyond the firmware end on both 2 MB and 16 MB boards.
// Banks are numbered 1..FLASH_MAX_BANKS (bank 0 = compiled-in, not in flash).

#define FLASH_BANKS_OFFSET  (FLASH_SETTINGS_OFFSET + FLASH_SETTINGS_RESERVED)  // 0x00200000
#define FLASH_BANKS_ADDR    (XIP_BASE + FLASH_BANKS_OFFSET)

// ---------------------------------------------------------------------------
// LittleFS reservation (ENABLE_LITTLEFS builds only)
// ---------------------------------------------------------------------------
// The earlephilhower core carves the LittleFS partition (board_build.
// filesystem_size) plus a fixed 4 KB EEPROM-emulation region from the TOP of
// flash (sized from board_upload.maximum_size), so the bank region below
// must stop well short of the top. Keep FLASH_LITTLEFS_RESERVED_SIZE in sync
// with board_build.filesystem_size for env:rpipico_16mb; the extra headroom
// below covers the core's 4 KB EEPROM region plus a safety margin.
#define FLASH_LITTLEFS_RESERVED_SIZE 0x00200000u   // 2 MB headroom (1 MB filesystem + margin)

// Per-bank size and maximum bank count depend on total flash size.
#if FLASH_SIZE_MB <= 2
  // 2 MB board: the compiled-in 808 samples make the firmware ~1.73 MB,
  // leaving no usable flash for user banks. Banks (and LittleFS/live
  // settings) are disabled; only the compiled-in bank 0 is available.
  #define FLASH_BANK_SIZE   0u
  #define FLASH_MAX_BANKS   0
#else
  // 16 MB board: banks occupy 2 MB..14 MB (4 x 3 MB), leaving the top 2 MB
  // free for the LittleFS partition + the core's EEPROM region + margin.
  #define FLASH_BANK_SIZE   0x00300000u   // 3 MB per bank
  #define FLASH_MAX_BANKS   4
#endif

// ---------------------------------------------------------------------------
// Sample store region (Phase B) — content-addressed, shared across banks.
// ---------------------------------------------------------------------------
// Reuses the same physical space as the legacy per-bank blob region above,
// rather than a separate reservation, since Phase C retires the old
// per-bank blob format in favour of this shared store + small per-bank
// config files. Until Phase C rewires BankManager to use it, writing
// sample-store data here means the legacy per-bank BankHeader magic checks
// will simply stop finding valid banks (fails safe: falls back to the
// compiled-in bank 0) - see include/audio/sample_store.h.
#define FLASH_SAMPLESTORE_OFFSET  FLASH_BANKS_OFFSET
#define FLASH_SAMPLESTORE_ADDR    (XIP_BASE + FLASH_SAMPLESTORE_OFFSET)
#define FLASH_SAMPLESTORE_SIZE    ((uint32_t)FLASH_BANK_SIZE * (uint32_t)FLASH_MAX_BANKS)

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

// XIP address of bank slot n (1-indexed, 0 = compiled-in).
static inline uint32_t flash_bank_xip_addr(int slot) {
    return FLASH_BANKS_ADDR + (uint32_t)(slot - 1) * (uint32_t)FLASH_BANK_SIZE;
}

// Byte offset from start of flash chip for bank slot n.
// Used with flash_range_erase() / flash_range_program() from hardware/flash.h.
static inline uint32_t flash_bank_erase_offset(int slot) {
    return FLASH_BANKS_OFFSET + (uint32_t)(slot - 1) * (uint32_t)FLASH_BANK_SIZE;
}
