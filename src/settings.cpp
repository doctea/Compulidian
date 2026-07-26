#include "settings.h"
#include "audio/flash_layout.h"

#include <Arduino.h>
#include <string.h>

// Pico SDK flash programming API.
// These functions call through ROM and are safe to call from C++ as long as
// core 1 is paused and interrupts are disabled (handled in settings_save()).
#include <hardware/flash.h>
#include <hardware/irq.h>

// earlephilhower Arduino-Pico multicore helpers.
#include <RP2040.h>

// ---------------------------------------------------------------------------
// settings_defaults()
// ---------------------------------------------------------------------------
Settings settings_defaults() {
    Settings s;
    memset(&s, 0, sizeof(s));
    s.magic   = SETTINGS_MAGIC;
    s.version = SETTINGS_VERSION;
    s.active_bank = 0; // compiled-in bank

    // Default outputs: pulse1 and pulse2 as clock, cv1 and cv2 as audio.
    s.outputs[0] = { OUTPUT_TYPE_CLOCK, 1, 0, 0 };
    s.outputs[1] = { OUTPUT_TYPE_CLOCK, 1, 0, 0 };
    s.outputs[2] = { OUTPUT_TYPE_AUDIO, 0, 0, 0 };
    s.outputs[3] = { OUTPUT_TYPE_AUDIO, 0, 0, 0 };

    // Default: knobs and CV not specially assigned.
    s.cv_input_role[0] = INPUT_ROLE_NONE;
    s.cv_input_role[1] = INPUT_ROLE_NONE;
    s.knob_role[0]     = INPUT_ROLE_NONE;
    s.knob_role[1]     = INPUT_ROLE_NONE;
    s.knob_role[2]     = INPUT_ROLE_NONE;

    return s;
}

// ---------------------------------------------------------------------------
// settings_load()
// ---------------------------------------------------------------------------
Settings settings_load() {
    const Settings *flash_s =
        reinterpret_cast<const Settings *>(FLASH_SETTINGS_ADDR);

    // Validate magic and version before trusting any field.
    if (flash_s->magic == SETTINGS_MAGIC && flash_s->version == SETTINGS_VERSION) {
        Settings s;
        memcpy(&s, flash_s, sizeof(Settings));
        return s;
    }

    // Flash sector is blank (0xFF) or contains stale data — return defaults.
    return settings_defaults();
}

// ---------------------------------------------------------------------------
// settings_save()
// ---------------------------------------------------------------------------

// The actual erase+write must run from RAM because it disables XIP.
// Mark the helper with __no_inline_not_in_flash_func so the linker places it
// in SRAM for us; the outer settings_save() wrapper can stay in flash.
static void __no_inline_not_in_flash_func(do_flash_write)(const uint8_t *buf, size_t len) {
    const uint32_t erase_offset = FLASH_SETTINGS_OFFSET; // from start of flash
    const uint32_t erase_size   = FLASH_SECTOR_SIZE;      // 4 KB

    // Erase one 4 KB sector.  Offset and count must be sector-aligned.
    flash_range_erase(erase_offset, erase_size);

    // Write the settings block rounded up to the nearest 256-byte page.
    const uint32_t write_len = ((len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;

    // Temporary page-aligned buffer (256 bytes max, on the stack).
    // Settings are <= 256 bytes by design (SETTINGS_STORED_SIZE == 256).
    static uint8_t page_buf[FLASH_PAGE_SIZE];
    memset(page_buf, 0xFF, sizeof(page_buf));
    memcpy(page_buf, buf, len < FLASH_PAGE_SIZE ? len : FLASH_PAGE_SIZE);

    flash_range_program(erase_offset, page_buf, write_len);
}

void settings_save(const Settings *s) {
    if (s == nullptr) return;

    // Stop core 1 (audio worker) so it is not fetching instructions or data
    // from flash while we disable XIP.
    rp2040.idleOtherCore();
    uint32_t irq_state = save_and_disable_interrupts();

    do_flash_write(reinterpret_cast<const uint8_t *>(s), sizeof(Settings));

    restore_interrupts(irq_state);
    rp2040.resumeOtherCore();
}
