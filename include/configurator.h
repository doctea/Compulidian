#pragma once

#include <Arduino.h>

#include "settings.h"
#include "audio/flash_layout.h"
#include "audio/bank_manager.h"
#include "workshop_output.h"

// ---------------------------------------------------------------------------
// SysEx protocol  (matches tools/web/compulidian_manager.html)
// F0 7D 43 <cmd> [data...] F7
// ---------------------------------------------------------------------------
#define SYSEX_MFR_ID     0x7D
#define SYSEX_DEVICE_ID  0x43   // 'C'
#define SYSEX_CMD_GET    0x10   // host → device: request config
#define SYSEX_CMD_DATA   0x11   // bidirectional: config payload (19 bytes)
#define SYSEX_CMD_REBOOT 0x20   // host → device: reboot to bootloader

// Data layout for SYSEX_CMD_DATA (indices into the SysEx data array,
// i.e. after the F0, MFR_ID, DEVICE_ID, CMD bytes have been consumed):
//  [0]  active_bank
//  [1]  num_valid_banks (read-only, sent by device, ignored on receive)
//  [2..4]  outputs[0]: type, channel, note
//  [5..7]  outputs[1]: type, channel, note
//  [8..10] outputs[2]: type, channel, note
// [11..13] outputs[3]: type, channel, note
// [14]  cv_input_role[0]
// [15]  cv_input_role[1]
// [16]  knob_role[0]
// [17]  knob_role[1]
// [18]  knob_role[2]
// [19..50]  bank name for slot 1 (32 bytes, ASCII, null-padded)  — only on 16MB build
// [51..82]  bank name for slot 2  (etc.)
// [83..114] bank name for slot 3
// [115..146] bank name for slot 4
#define SYSEX_DATA_LEN    19
#define SYSEX_BANK_NAME   32   // bytes per bank name in the SysEx response

static Settings current_settings;

static void send_config_sysex() {
    if(Serial) Serial.printf("send_config_sysex() active_bank=%i\n", current_settings.active_bank);
    // Header (3) + settings (19) + bank names (FLASH_MAX_BANKS × 32)
    const int names_len = FLASH_MAX_BANKS * SYSEX_BANK_NAME;
    uint8_t buf[3 + SYSEX_DATA_LEN + FLASH_MAX_BANKS * SYSEX_BANK_NAME];
    memset(buf, 0, sizeof(buf));
    buf[0] = SYSEX_MFR_ID;
    buf[1] = SYSEX_DEVICE_ID;
    buf[2] = SYSEX_CMD_DATA;
    buf[3] = current_settings.active_bank;
    buf[4] = (uint8_t)bankManager.num_valid_banks();
    for (int i = 0; i < 4; i++) {
        buf[5 + i * 3]     = (uint8_t)current_settings.outputs[i].type;
        buf[5 + i * 3 + 1] = current_settings.outputs[i].channel;
        buf[5 + i * 3 + 2] = current_settings.outputs[i].note;
    }
    buf[17] = (uint8_t)current_settings.cv_input_role[0];
    buf[18] = (uint8_t)current_settings.cv_input_role[1];
    buf[19] = (uint8_t)current_settings.knob_role[0];
    buf[20] = (uint8_t)current_settings.knob_role[1];
    buf[21] = (uint8_t)current_settings.knob_role[2];
    // Append bank names (only for slots with valid headers).
    // Mask each byte to 7 bits — SysEx data bytes must be 0x00–0x7F.
    for (int slot = 1; slot <= FLASH_MAX_BANKS; ++slot) {
        uint8_t *dst = buf + 3 + SYSEX_DATA_LEN + (slot - 1) * SYSEX_BANK_NAME;
        const BankHeader *hdr = bankManager.get_bank_header(slot);
        if (hdr) {
            for (int i = 0; i < SYSEX_BANK_NAME - 1; i++) {
                char c = hdr->bank_name[i];
                if (c == '\0') break;
                dst[i] = (uint8_t)c & 0x7F;
            }
        }
    }
    Serial.printf("send_config_sysex() sending %d bytes\n", sizeof(buf));
    USBMIDI.sendClock();  // send a clock before the SysEx to wake up the host
    USBMIDI.sendSysEx(sizeof(buf), buf, false);
}

static void handle_sysex(uint8_t *data, unsigned int size) {
    // The MIDI library includes 0xF0 at data[0] and 0xF7 at data[size-1].
    // Layout: [0xF0, MFR_ID, DEVICE_ID, CMD, ...payload..., 0xF7]
    if (size < 5) return;
    if (data[0] != 0xF0) return;
    if (data[1] != SYSEX_MFR_ID || data[2] != SYSEX_DEVICE_ID) return;

    const uint8_t cmd = data[3];

    if (cmd == SYSEX_CMD_GET) {
        send_config_sysex();

    } else if (cmd == SYSEX_CMD_DATA && size >= 4 + SYSEX_DATA_LEN) {
        const uint8_t *d = data + 4; // payload start (after F0, MFR, DEVICE, CMD)
        current_settings.active_bank = d[0];
        // d[1] = num_valid_banks is read-only, skip
        for (int i = 0; i < 4; i++) {
            current_settings.outputs[i].type    = (OutputType)d[2 + i * 3];
            current_settings.outputs[i].channel = d[2 + i * 3 + 1];
            current_settings.outputs[i].note    = d[2 + i * 3 + 2];
        }
        current_settings.cv_input_role[0] = (InputRole)d[14];
        current_settings.cv_input_role[1] = (InputRole)d[15];
        current_settings.knob_role[0]     = (InputRole)d[16];
        current_settings.knob_role[1]     = (InputRole)d[17];
        current_settings.knob_role[2]     = (InputRole)d[18];
        settings_save(&current_settings);
        if ((int)current_settings.active_bank != bankManager.active_bank())
            bankManager.switch_bank(current_settings.active_bank);
        send_config_sysex(); // echo confirmation

    } else if (cmd == SYSEX_CMD_REBOOT) {
        rp2040.rebootToBootloader();
    }
}

// ---------------------------------------------------------------------------
// Boot-mode / config-mode state
// ---------------------------------------------------------------------------
static bool config_mode      = false;
static bool boot_check_done  = false;
static uint32_t boot_hold_start_ms  = 0;
static uint32_t boot_window_start_ms = 0;  // set on first loop() call
static int config_mode_displayed_bank = -1;

static void config_mode_update_leds(int bank) {
    // LEDs 0..N-1 lit to show which bank is selected (0 = all off → compiled-in).
    for (int i = 0; i < sw.numLeds; i++) {
        if (bank > 0 && i < bank) sw.LedOn(i);
        else                       sw.LedOff(i);
    }
}

// Called from loop() — handles all config-mode logic.
// Returns true if we are in config mode (caller should skip normal processing).
static uint32_t cfg_debug_last_ms = 0;  // rate-limit the boot-mode debug prints

static bool handle_config_mode() {
    if (!boot_check_done) {
        uint32_t now = millis();

        // Record the timestamp of the first loop() call.  By this point setup()
        // has completed and core1 has been running audio interrupts for the
        // full duration of setup(), so the switch hardware is definitely live.
        // We use elapsed time relative to this point so a slow setup() (e.g.
        // TinyUSB enumeration, flash reads) doesn't shrink or eliminate the
        // config-mode detection window.
        if (boot_window_start_ms == 0) {
            boot_window_start_ms = now;
            if (Serial) Serial.printf(
                "ConfigMode: window opened at %ums; hold Down for 1.5s within 8s\n", now);
        }
        uint32_t elapsed = now - boot_window_start_ms;

        // Periodic status print every ~500 ms while window is open
        if (Serial && (now - cfg_debug_last_ms >= 500)) {
            cfg_debug_last_ms = now;
            const char *sw_name[] = {"Down", "Middle", "Up"};
            int sv = (int)sw.SwitchVal();
            if (sv < 0 || sv > 2) sv = 2;
            Serial.printf("ConfigMode: elapsed=%ums switch=%s hold_for=%ums\n",
                elapsed, sw_name[sv],
                (boot_hold_start_ms && sv == 0) ? (now - boot_hold_start_ms) : 0u);
        }

        // Detection window: 8 seconds from first loop() call.
        // User must hold the switch Down for 1.5 s within this window.
        if (elapsed < 8000) {
            if (sw.SwitchVal() == ComputerCard::Down) {
                if (boot_hold_start_ms == 0) boot_hold_start_ms = now;
                if (now - boot_hold_start_ms >= 1500) {
                    // Switch held Down for 1.5 s → enter config mode
                    if (Serial) Serial.println("ConfigMode: ENTERED config mode");
                    config_mode = true;
                    boot_check_done = true;
                    // Flash all LEDs three times to signal entry
                    for (int flash = 0; flash < 3; flash++) {
                        for (int i = 0; i < sw.numLeds; i++) sw.LedOn(i);
                        delay(150);
                        for (int i = 0; i < sw.numLeds; i++) sw.LedOff(i);
                        delay(150);
                    }
                    config_mode_displayed_bank = bankManager.active_bank();
                    config_mode_update_leds(config_mode_displayed_bank);
                }
            } else {
                boot_hold_start_ms = 0;
            }
        } else {
            if (Serial) Serial.printf("ConfigMode: window closed at elapsed=%ums - boot-mode unavailable\n", elapsed);
            boot_check_done = true; // window expired, never entering config mode
        }
    }

    if (!config_mode) return false;

    // --- Config mode active ---
    // Main knob selects bank; switch Middle confirms and exits.
    int num_banks = bankManager.num_valid_banks();
    if (num_banks < 1) num_banks = 1;
    int32_t knob = sw.KnobVal(ComputerCard::Main);
    int desired = (int)((knob * num_banks) / 4096);
    if (desired >= num_banks) desired = num_banks - 1;

    if (desired != config_mode_displayed_bank) {
        config_mode_displayed_bank = desired;
        config_mode_update_leds(desired);
    }

    // Switch to Middle position → confirm selection
    if (sw.SwitchVal() == ComputerCard::Middle && sw.SwitchChanged()) {
        bankManager.switch_bank(desired);
        current_settings.active_bank = (uint8_t)desired;
        settings_save(&current_settings);
        config_mode = false;
        // Brief confirmation flash
        for (int i = 0; i < sw.numLeds; i++) sw.LedOn(i);
        delay(300);
        for (int i = 0; i < sw.numLeds; i++) sw.LedOff(i);
    }

    // Keep audio calculated even in config mode.
    if (sw.calculate_mode == IN_MAIN_LOOP)
        sw.CalculateSamples();

    return true; // skip normal loop body
}
