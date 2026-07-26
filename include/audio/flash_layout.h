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
// Firmware occupies the start of flash. Typical RP2040 Arduino builds are
// 400-600 KB; 1 MB is a generous upper bound that should never be exceeded.
// Sample banks are placed AFTER this reserve, so firmware reflashing never
// overwrites bank data.

#define FLASH_FIRMWARE_SIZE     0x00100000u   // 1 MB

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
// Banks are numbered 1..FLASH_MAX_BANKS (bank 0 = compiled-in, not in flash).

#define FLASH_BANKS_OFFSET  (FLASH_SETTINGS_OFFSET + FLASH_SETTINGS_RESERVED)  // 0x00110000
#define FLASH_BANKS_ADDR    (XIP_BASE + FLASH_BANKS_OFFSET)

// Per-bank size and maximum bank count depend on total flash size.
#if FLASH_SIZE_MB <= 2
  // Standard 2 MB board: one flash bank using whatever space remains after
  // the firmware reserve and settings block (~960 KB).
  #define FLASH_BANK_SIZE   (FLASH_SIZE_BYTES - FLASH_BANKS_OFFSET)
  #define FLASH_MAX_BANKS   1
#else
  // Extended 16 MB board: four 4 MB banks.
  #define FLASH_BANK_SIZE   0x00400000u   // 4 MB per bank
  #define FLASH_MAX_BANKS   4
#endif

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
