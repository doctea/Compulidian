#pragma once

#include <Arduino.h>

#include "settings.h"
#include "audio/flash_layout.h"
#include "audio/bank_manager.h"
#include "workshop_output.h"

// ---------------------------------------------------------------------------
// SysEx protocol v2 (matches tools/web/compulidian_manager.html)
// Frame:
// F0 7D 43 <ver> <type> <req_id> <status> [payload...] F7
// ---------------------------------------------------------------------------
#define SYSEX_MFR_ID     0x7D
#define SYSEX_DEVICE_ID  0x43   // 'C'
#define SYSEX_PROTO_VER  0x02

#define SYSEX_TYPE_GET_CONFIG_REQ 0x10
#define SYSEX_TYPE_SET_CONFIG_REQ 0x11
#define SYSEX_TYPE_REBOOT_REQ     0x20

#define SYSEX_TYPE_CONFIG_RESP    0x30
#define SYSEX_TYPE_ACK            0x31
#define SYSEX_TYPE_NACK           0x32

#define SYSEX_STATUS_OK                    0x00
#define SYSEX_NACK_BAD_LENGTH             0x01
#define SYSEX_NACK_UNSUPPORTED_VERSION    0x02
#define SYSEX_NACK_UNKNOWN_TYPE           0x03
#define SYSEX_NACK_INVALID_VALUE          0x04
#define SYSEX_NACK_NOT_READY              0x05

#ifndef SYSEX_DEBUG
#define SYSEX_DEBUG 0
#endif

#define SYSEX_FRAME_BYTES         6

// Data layout for config payload (indices into the payload bytes after
// [MFR, DEVICE, VER, TYPE, REQ_ID, STATUS]):
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

enum PendingSysexAction : uint8_t {
    PENDING_NONE = 0,
    PENDING_SEND_CONFIG = 1,
    PENDING_SEND_ACK = 2,
    PENDING_SEND_NACK = 3,
    PENDING_REBOOT_BOOTSEL = 4,
};

struct PendingSysexItem {
    uint8_t action;
    uint8_t req_id;
    uint8_t arg;
};

static volatile uint8_t pending_sysex_head = 0;
static volatile uint8_t pending_sysex_tail = 0;
static PendingSysexItem pending_sysex_queue[8];

static void send_config_sysex(uint8_t req_id = 0);

static bool enqueue_pending_sysex(uint8_t action, uint8_t req_id, uint8_t arg = 0) {
    noInterrupts();
    const uint8_t next_head = (uint8_t)((pending_sysex_head + 1) % (sizeof(pending_sysex_queue) / sizeof(pending_sysex_queue[0])));
    if (next_head == pending_sysex_tail) {
        interrupts();
        if (SYSEX_DEBUG && Serial) Serial.println("pending sysex queue full, dropping action");
        return false;
    }
    pending_sysex_queue[pending_sysex_head].action = action;
    pending_sysex_queue[pending_sysex_head].req_id = req_id;
    pending_sysex_queue[pending_sysex_head].arg = arg;
    pending_sysex_head = next_head;
    interrupts();
    return true;
}

static bool dequeue_pending_sysex(PendingSysexItem *out) {
    if (!out) return false;
    noInterrupts();
    if (pending_sysex_tail == pending_sysex_head) {
        interrupts();
        return false;
    }
    *out = pending_sysex_queue[pending_sysex_tail];
    pending_sysex_tail = (uint8_t)((pending_sysex_tail + 1) % (sizeof(pending_sysex_queue) / sizeof(pending_sysex_queue[0])));
    interrupts();
    return true;
}

static bool send_sysex_frame(uint8_t type, uint8_t req_id, uint8_t status, const uint8_t *payload, uint16_t payload_len) {
    // On first browser connect, MIDI can become usable slightly before mounted()
    // stabilizes. Wait briefly, but do not hard-drop the frame if it still reads false.
    if (!TinyUSBDevice.mounted()) {
        const uint32_t start_ms = millis();
        while (!TinyUSBDevice.mounted() && (millis() - start_ms) < 250) {
            delay(1);
        }
        if (SYSEX_DEBUG && !TinyUSBDevice.mounted() && Serial) {
            Serial.println("send_sysex_frame(): USB still reports unmounted, attempting send anyway");
        }
    }

    uint8_t buf[SYSEX_FRAME_BYTES + SYSEX_DATA_LEN + FLASH_MAX_BANKS * SYSEX_BANK_NAME];
    if (payload_len > sizeof(buf) - SYSEX_FRAME_BYTES) {
        if (SYSEX_DEBUG && Serial) Serial.println("send_sysex_frame(): payload too large");
        return false;
    }

    memset(buf, 0, sizeof(buf));
    buf[0] = SYSEX_MFR_ID;
    buf[1] = SYSEX_DEVICE_ID;
    buf[2] = SYSEX_PROTO_VER;
    buf[3] = type;
    buf[4] = req_id & 0x7F;
    buf[5] = status & 0x7F;
    if (payload && payload_len > 0) {
        memcpy(buf + SYSEX_FRAME_BYTES, payload, payload_len);
    }

    if (SYSEX_DEBUG && Serial) {
        Serial.printf("sysex tx: type=0x%02X req=%u status=0x%02X payload=%u\n",
            type, req_id, status, payload_len);
    }
    USBMIDI.sendSysEx(SYSEX_FRAME_BYTES + payload_len, buf, false);
    return true;
}

static void send_ack(uint8_t req_id) {
    send_sysex_frame(SYSEX_TYPE_ACK, req_id, SYSEX_STATUS_OK, nullptr, 0);
}

static void send_nack(uint8_t req_id, uint8_t reason) {
    send_sysex_frame(SYSEX_TYPE_NACK, req_id, reason, nullptr, 0);
}

static void process_pending_sysex_responses() {
    PendingSysexItem item;
    int processed = 0;
    while (processed < 6 && dequeue_pending_sysex(&item)) {
        switch (item.action) {
            case PENDING_SEND_CONFIG:
                send_config_sysex(item.req_id);
                break;
            case PENDING_SEND_ACK:
                send_ack(item.req_id);
                break;
            case PENDING_SEND_NACK:
                send_nack(item.req_id, item.arg);
                break;
            case PENDING_REBOOT_BOOTSEL:
                delay(10);
                rp2040.rebootToBootloader();
                break;
            default:
                break;
        }
        processed++;
    }
}

static bool build_config_payload(uint8_t *payload, uint16_t *payload_len) {
    if (!payload || !payload_len) return false;

    memset(payload, 0, SYSEX_DATA_LEN + FLASH_MAX_BANKS * SYSEX_BANK_NAME);
    payload[0] = current_settings.active_bank;
    payload[1] = (uint8_t)bankManager.num_valid_banks();
    for (int i = 0; i < 4; i++) {
        payload[2 + i * 3]     = (uint8_t)current_settings.outputs[i].type;
        payload[2 + i * 3 + 1] = current_settings.outputs[i].channel;
        payload[2 + i * 3 + 2] = current_settings.outputs[i].note;
    }
    payload[14] = (uint8_t)current_settings.cv_input_role[0];
    payload[15] = (uint8_t)current_settings.cv_input_role[1];
    payload[16] = (uint8_t)current_settings.knob_role[0];
    payload[17] = (uint8_t)current_settings.knob_role[1];
    payload[18] = (uint8_t)current_settings.knob_role[2];

    // Append bank names (only for slots with valid headers).
    // Mask each byte to 7 bits — SysEx data bytes must be 0x00–0x7F.
    for (int slot = 1; slot <= FLASH_MAX_BANKS; ++slot) {
        uint8_t *dst = payload + SYSEX_DATA_LEN + (slot - 1) * SYSEX_BANK_NAME;
        const BankHeader *hdr = bankManager.get_bank_header(slot);
        if (hdr) {
            for (int i = 0; i < SYSEX_BANK_NAME - 1; i++) {
                char c = hdr->bank_name[i];
                if (c == '\0') break;
                dst[i] = (uint8_t)c & 0x7F;
            }
        }
    }

    *payload_len = SYSEX_DATA_LEN + (FLASH_MAX_BANKS * SYSEX_BANK_NAME);
    return true;
}

static void send_config_sysex(uint8_t req_id) {
    if (SYSEX_DEBUG && Serial) Serial.printf("send_config_sysex() active_bank=%i req_id=%u\n", current_settings.active_bank, req_id);
    uint8_t payload[SYSEX_DATA_LEN + FLASH_MAX_BANKS * SYSEX_BANK_NAME];
    uint16_t payload_len = 0;
    if (!build_config_payload(payload, &payload_len)) {
        send_nack(req_id, SYSEX_NACK_NOT_READY);
        return;
    }
    if (!send_sysex_frame(SYSEX_TYPE_CONFIG_RESP, req_id, SYSEX_STATUS_OK, payload, payload_len)) {
        send_nack(req_id, SYSEX_NACK_NOT_READY);
    }
}

static void handle_sysex(uint8_t *data, unsigned int size) {
    // The MIDI library includes 0xF0 at data[0] and 0xF7 at data[size-1].
    // Layout: [0xF0, MFR_ID, DEVICE_ID, VER, TYPE, REQ_ID, STATUS, ...payload..., 0xF7]
    if (size < 8) return;
    if (data[0] != 0xF0) return;
    if (data[size - 1] != 0xF7) return;
    if (data[1] != SYSEX_MFR_ID || data[2] != SYSEX_DEVICE_ID) return;

    const uint8_t ver = data[3];
    const uint8_t type = data[4];
    const uint8_t req_id = data[5] & 0x7F;
    const uint8_t payload_status = data[6];
    const uint8_t *d = data + 7;
    const unsigned payload_len = size - 8;

    if (SYSEX_DEBUG && Serial) {
        Serial.printf("sysex rx: ver=0x%02X type=0x%02X req=%u status=0x%02X payload=%u\n",
            ver, type, req_id, payload_status, payload_len);
    }

    if (ver != SYSEX_PROTO_VER) {
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_UNSUPPORTED_VERSION);
        return;
    }

    if (type == SYSEX_TYPE_GET_CONFIG_REQ) {
        if (payload_len != 0) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        enqueue_pending_sysex(PENDING_SEND_CONFIG, req_id);

    } else if (type == SYSEX_TYPE_SET_CONFIG_REQ) {
        if (payload_len < SYSEX_DATA_LEN) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }

        if (d[0] > FLASH_MAX_BANKS) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_INVALID_VALUE);
            return;
        }

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
        if ((int)current_settings.active_bank != bankManager.active_bank()) {
            if (!bankManager.switch_bank(current_settings.active_bank)) {
                enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_INVALID_VALUE);
                return;
            }
        }

        enqueue_pending_sysex(PENDING_SEND_ACK, req_id);
        enqueue_pending_sysex(PENDING_SEND_CONFIG, req_id);

    } else if (type == SYSEX_TYPE_REBOOT_REQ) {
        enqueue_pending_sysex(PENDING_SEND_ACK, req_id);
        enqueue_pending_sysex(PENDING_REBOOT_BOOTSEL, req_id);
    } else {
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_UNKNOWN_TYPE);
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
