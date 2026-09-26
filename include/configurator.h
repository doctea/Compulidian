#pragma once

#include <Arduino.h>

#include "settings.h"
#include "audio/flash_layout.h"
#include "audio/bank_manager.h"
#include "audio/bank_config.h"
#include "audio/sample_store.h"
#include "workshop_output.h"
#include "outputs/output_processor.h"

#ifdef ENABLE_LITTLEFS
#include <LittleFS.h>
#endif

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
#define SYSEX_TYPE_GET_BANK_INFO_REQ 0x12
#define SYSEX_TYPE_GET_BANK_CHUNK_REQ 0x13
#define SYSEX_TYPE_GET_SLOTS_REQ  0x14
#define SYSEX_TYPE_SET_BANK_CHUNK_REQ 0x15
#define SYSEX_TYPE_GET_SAMPLES_INFO_REQ  0x16
#define SYSEX_TYPE_GET_SAMPLES_CHUNK_REQ 0x17
#define SYSEX_TYPE_GET_SAMPLE_META_INFO_REQ  0x18
#define SYSEX_TYPE_GET_SAMPLE_META_CHUNK_REQ 0x19
#define SYSEX_TYPE_SET_SAMPLE_META_CHUNK_REQ 0x1A
#define SYSEX_TYPE_REBOOT_REQ     0x20

#define SYSEX_TYPE_CONFIG_RESP    0x30
#define SYSEX_TYPE_BANK_INFO_RESP 0x40
#define SYSEX_TYPE_BANK_CHUNK_RESP 0x41
#define SYSEX_TYPE_SLOTS_INFO_RESP 0x42
#define SYSEX_TYPE_SAMPLES_INFO_RESP  0x43
#define SYSEX_TYPE_SAMPLES_CHUNK_RESP 0x44
#define SYSEX_TYPE_SAMPLE_META_INFO_RESP  0x45
#define SYSEX_TYPE_SAMPLE_META_CHUNK_RESP 0x46
#define SYSEX_TYPE_ACK            0x31
#define SYSEX_TYPE_NACK           0x32

#define SYSEX_STATUS_OK                    0x00
#define SYSEX_NACK_BAD_LENGTH             0x01
#define SYSEX_NACK_UNSUPPORTED_VERSION    0x02
#define SYSEX_NACK_UNKNOWN_TYPE           0x03
#define SYSEX_NACK_INVALID_VALUE          0x04
#define SYSEX_NACK_NOT_READY              0x05
#define SYSEX_NACK_WRITE_FAILED           0x06

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
// Raw chunk size must fit in 14 bits (encoded as two 7-bit bytes) - see send_bank_chunk_sysex().
// Kept moderate (vs. e.g. 1024+) to bound stack usage of the payload buffers below.
// This is the DOWNLOAD (device -> browser) direction only - send_sysex_frame()'s
// large outgoing payloads bypass the MIDI library's RX assembly entirely (see
// send_sysex_bytes_reliable_() above), so this can be much bigger than what
// the device can *receive* (see SYSEX_BANK_WRITE_RAW_CHUNK_BYTES below).
#define SYSEX_BANK_RAW_CHUNK_BYTES 512
#define SYSEX_BANK_CHUNK_HEX_BYTES (SYSEX_BANK_RAW_CHUNK_BYTES * 2)
#define SYSEX_BANK_CHUNK_HEADER_BYTES 6
#define SYSEX_MAX_PAYLOAD (SYSEX_BANK_CHUNK_HEADER_BYTES + SYSEX_BANK_CHUNK_HEX_BYTES)

// UPLOAD (browser -> device) direction: incoming SysEx is parsed by the
// FortySevenEffects MIDI library using DefaultSettings::SysExMaxSize = 128
// bytes TOTAL (including the F0/F7 delimiters) - see
// ".pio/libdeps/*/MIDI Library/src/midi_Settings.h". A frame over that size
// is silently truncated/dropped before handle_sysex() ever sees it, so the
// raw chunk size for SET_BANK_CHUNK_REQ must keep
// 8 (F0 + 6 header bytes + F7) + SYSEX_BANK_CHUNK_HEADER_BYTES + raw*2 <= 128.
#define SYSEX_BANK_WRITE_RAW_CHUNK_BYTES 32
// Total bytes buffered in RAM across all chunks of one incoming bank-config
// upload before it's written to LittleFS - generously above the expected
// config file size (~700 bytes for a full 32-slot bank) to allow headroom.
#define SYSEX_BANK_WRITE_MAX_BYTES 2048

// SampleStore listing (device -> browser, so - like GET_BANK_CHUNK - not
// subject to the 128-byte incoming SysEx cap; chunk size is just kept
// moderate to bound stack usage).
#define SYSEX_SAMPLES_CHUNK_HEADER_BYTES 5
#define SYSEX_SAMPLES_ENTRIES_PER_CHUNK 8
#define SYSEX_SAMPLES_CHUNK_RAW_BYTES (SYSEX_SAMPLES_ENTRIES_PER_CHUNK * SAMPLESTORE_ENTRY_SIZE)

// Slot-info response: one byte per note/channel, one length byte + label per
// slot. Labels are truncated - they're only for display, not identity.
#define SYSEX_SLOT_LABEL_MAX      23
#define SYSEX_MAX_SLOTS_REPORTED  32
#define SYSEX_SLOTS_MAX_PAYLOAD   (1 + SYSEX_MAX_SLOTS_REPORTED * (3 + SYSEX_SLOT_LABEL_MAX))

// Sample metadata file (Phase F.4): opaque byte streaming, same pattern as
// the bank-config chunk pair but for one fixed file (no slot byte needed).
// Download direction (device -> browser) bypasses the incoming-SysEx size
// cap (see send_bank_chunk_sysex()'s comment) so its chunk can be large;
// upload direction (browser -> device) must obey the ~128-byte incoming
// SysEx frame limit, same reasoning as SYSEX_BANK_WRITE_RAW_CHUNK_BYTES.
#define SYSEX_META_CHUNK_HEADER_BYTES      5
#define SYSEX_META_RAW_CHUNK_BYTES         512
#define SYSEX_META_CHUNK_HEX_BYTES         (SYSEX_META_RAW_CHUNK_BYTES * 2)
#define SYSEX_META_WRITE_RAW_CHUNK_BYTES   32
#define SYSEX_META_WRITE_MAX_BYTES         8192

static Settings current_settings;

enum PendingSysexAction : uint8_t {
    PENDING_NONE = 0,
    PENDING_SEND_CONFIG = 1,
    PENDING_SEND_ACK = 2,
    PENDING_SEND_NACK = 3,
    PENDING_REBOOT_BOOTSEL = 4,
    PENDING_SEND_BANK_INFO = 5,
    PENDING_SEND_BANK_CHUNK = 6,
    PENDING_SEND_SLOTS_INFO = 7,
    PENDING_SEND_SAMPLES_INFO = 8,
    PENDING_SEND_SAMPLES_CHUNK = 9,
    PENDING_SEND_SAMPLE_META_INFO = 10,
    PENDING_SEND_SAMPLE_META_CHUNK = 11,
};

struct PendingSysexItem {
    uint8_t action;
    uint8_t req_id;
    uint8_t arg;
    uint8_t arg2;
    uint8_t arg3;
};

static volatile uint8_t pending_sysex_head = 0;
static volatile uint8_t pending_sysex_tail = 0;
static PendingSysexItem pending_sysex_queue[8];

static void send_config_sysex(uint8_t req_id = 0);
static void send_bank_info_sysex(uint8_t req_id, uint8_t slot);
static void send_bank_chunk_sysex(uint8_t req_id, uint8_t slot, uint16_t chunk_idx);
static void send_slots_info_sysex(uint8_t req_id);
static void send_samples_info_sysex(uint8_t req_id);
static void send_samples_chunk_sysex(uint8_t req_id, uint16_t chunk_idx);
static void send_sample_meta_info_sysex(uint8_t req_id);
static void send_sample_meta_chunk_sysex(uint8_t req_id, uint16_t chunk_idx);
static void handle_set_bank_chunk_(uint8_t req_id, uint8_t slot, uint16_t chunk_idx,
                                    const uint8_t *hex, uint16_t raw_len, bool is_last);
static void handle_set_sample_meta_chunk_(uint8_t req_id, uint16_t chunk_idx,
                                           const uint8_t *hex, uint16_t raw_len, bool is_last);

// State for an in-progress SET_BANK_CHUNK_REQ upload (one at a time; a
// chunk_idx of 0 (re)starts it, discarding anything not yet finalised).
// See handle_set_bank_chunk_().
static uint8_t  bank_write_buf_[SYSEX_BANK_WRITE_MAX_BYTES];
static uint32_t bank_write_len_ = 0;
static uint8_t  bank_write_slot_ = 0;
static bool     bank_write_active_ = false;

// State for an in-progress SET_SAMPLE_META_CHUNK_REQ upload - same pattern
// as bank_write_*_ above but for the single sample-metadata file (no slot).
static uint8_t  meta_write_buf_[SYSEX_META_WRITE_MAX_BYTES];
static uint32_t meta_write_len_ = 0;
static bool     meta_write_active_ = false;

static bool enqueue_pending_sysex(uint8_t action, uint8_t req_id, uint8_t arg = 0, uint8_t arg2 = 0, uint8_t arg3 = 0) {
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
    pending_sysex_queue[pending_sysex_head].arg2 = arg2;
    pending_sysex_queue[pending_sysex_head].arg3 = arg3;
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

// The TinyUSB MIDI TX FIFO is only CFG_TUD_MIDI_TX_BUFSIZE bytes (64 on RP2040).
// USBMIDI.sendSysEx()/mTransport.write() ignore the write() return value, so once
// the FIFO fills mid-message, remaining bytes are silently dropped rather than
// queued or retried - corrupting/truncating any sysex frame bigger than ~60 bytes.
// Write bytes ourselves with backpressure (retry + tud_task() to drain) instead.
static bool midi_write_byte_blocking_(uint8_t b, uint32_t start_ms) {
    while (usb_midi.write(b) != 1) {
        tud_task();
        if ((millis() - start_ms) > 2000) return false; // host likely gone; don't hang forever
    }
    return true;
}

static bool send_sysex_bytes_reliable_(const uint8_t *data, size_t len) {
    // Held for the whole frame - do_tick() can re-enter mid blocking-write
    // (via tud_task()), so a note echo firing in between would otherwise
    // interleave its raw bytes into this sysex frame. See workshop_output.h.
    g_usbmidi_tx_busy = true;
    const uint32_t start_ms = millis();
    bool ok = midi_write_byte_blocking_(0xF0, start_ms);
    for (size_t i = 0; ok && i < len; ++i) {
        ok = midi_write_byte_blocking_(data[i], start_ms);
    }
    ok = ok && midi_write_byte_blocking_(0xF7, start_ms);
    g_usbmidi_tx_busy = false;
    return ok;
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

    uint8_t buf[SYSEX_FRAME_BYTES + SYSEX_MAX_PAYLOAD];
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
    if (!send_sysex_bytes_reliable_(buf, SYSEX_FRAME_BYTES + payload_len)) {
        if (SYSEX_DEBUG && Serial) Serial.println("send_sysex_frame(): USB write timed out (FIFO congested)");
        return false;
    }
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
            case PENDING_SEND_BANK_INFO:
                send_bank_info_sysex(item.req_id, item.arg);
                break;
            case PENDING_SEND_BANK_CHUNK: {
                const uint16_t idx = (uint16_t)item.arg2 | ((uint16_t)item.arg3 << 7);
                send_bank_chunk_sysex(item.req_id, item.arg, idx);
                break;
            }
            case PENDING_SEND_SLOTS_INFO:
                send_slots_info_sysex(item.req_id);
                break;
            case PENDING_SEND_SAMPLES_INFO:
                send_samples_info_sysex(item.req_id);
                break;
            case PENDING_SEND_SAMPLES_CHUNK: {
                const uint16_t idx = (uint16_t)item.arg | ((uint16_t)item.arg2 << 7);
                send_samples_chunk_sysex(item.req_id, idx);
                break;
            }
            case PENDING_SEND_SAMPLE_META_INFO:
                send_sample_meta_info_sysex(item.req_id);
                break;
            case PENDING_SEND_SAMPLE_META_CHUNK: {
                const uint16_t idx = (uint16_t)item.arg | ((uint16_t)item.arg2 << 7);
                send_sample_meta_chunk_sysex(item.req_id, idx);
                break;
            }
            default:
                break;
        }
        processed++;
    }
}

// Size in bytes of a bank's small saveloadlib config file at
// /save/bank_N.txt (see audio/bank_config.h). Phase C retired the old
// per-bank raw PCM blob format - samples are shared across banks in the
// content-addressed SampleStore (audio/sample_store.h), so there is no
// longer a single contiguous per-bank byte stream to report/download the
// old way. The web tool's "download bank" chunk protocol below is repointed
// at this (much smaller) config file instead.
static uint32_t compute_bank_config_file_size_(uint8_t slot) {
#ifdef ENABLE_LITTLEFS
    char path[32];
    snprintf(path, sizeof(path), BANK_CONFIG_SAVE_PATH_FMT, slot);
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    uint32_t sz = (uint32_t)f.size();
    f.close();
    return sz;
#else
    (void)slot;
    return 0;
#endif
}

static inline uint8_t to_hex_nibble_(uint8_t v) {
    v &= 0x0F;
    return (v < 10) ? (uint8_t)('0' + v) : (uint8_t)('A' + (v - 10));
}

static void put_u32_7bit_(uint8_t *dst, uint32_t v) {
    // 35-bit container for 32-bit values, little-endian 7-bit groups.
    dst[0] = (uint8_t)(v & 0x7F);
    dst[1] = (uint8_t)((v >> 7) & 0x7F);
    dst[2] = (uint8_t)((v >> 14) & 0x7F);
    dst[3] = (uint8_t)((v >> 21) & 0x7F);
    dst[4] = (uint8_t)((v >> 28) & 0x0F);
}

static void send_bank_info_sysex(uint8_t req_id, uint8_t slot) {
    if (slot < 1 || slot > FLASH_MAX_BANKS || !bankManager.is_bank_valid(slot)) {
        send_nack(req_id, SYSEX_NACK_INVALID_VALUE);
        return;
    }

    const uint32_t config_bytes = compute_bank_config_file_size_(slot);

    uint8_t payload[12];
    memset(payload, 0, sizeof(payload));
    payload[0] = slot & 0x7F;
    payload[1] = (uint8_t)bankManager.get_bank_slot_count(slot) & 0x7F;
    // "pcm_bytes" no longer applies per-bank (samples are shared across banks
    // in the SampleStore) - reported as 0. "total_bytes" is now the size of
    // the bank's small config file, which is what GET_BANK_CHUNK streams.
    put_u32_7bit_(payload + 2, 0);
    put_u32_7bit_(payload + 7, config_bytes);

    send_sysex_frame(SYSEX_TYPE_BANK_INFO_RESP, req_id, SYSEX_STATUS_OK, payload, sizeof(payload));
}

static void send_bank_chunk_sysex(uint8_t req_id, uint8_t slot, uint16_t chunk_idx) {
    if (slot < 1 || slot > FLASH_MAX_BANKS || !bankManager.is_bank_valid(slot)) {
        send_nack(req_id, SYSEX_NACK_INVALID_VALUE);
        return;
    }

#ifdef ENABLE_LITTLEFS
    char path[32];
    snprintf(path, sizeof(path), BANK_CONFIG_SAVE_PATH_FMT, slot);
    File f = LittleFS.open(path, "r");
    if (!f) {
        send_nack(req_id, SYSEX_NACK_NOT_READY);
        return;
    }

    const uint32_t total_bytes = (uint32_t)f.size();
    const uint32_t offset = (uint32_t)chunk_idx * SYSEX_BANK_RAW_CHUNK_BYTES;
    if (offset >= total_bytes) {
        f.close();
        send_nack(req_id, SYSEX_NACK_INVALID_VALUE);
        return;
    }

    const uint32_t remaining = total_bytes - offset;
    const uint16_t raw_len = (uint16_t)((remaining >= SYSEX_BANK_RAW_CHUNK_BYTES) ? SYSEX_BANK_RAW_CHUNK_BYTES : remaining);
    const bool is_last = (offset + raw_len) >= total_bytes;

    uint8_t raw[SYSEX_BANK_RAW_CHUNK_BYTES];
    f.seek(offset);
    size_t got = f.read(raw, raw_len);
    f.close();
    if (got != raw_len) {
        send_nack(req_id, SYSEX_NACK_NOT_READY);
        return;
    }

    uint8_t payload[SYSEX_BANK_CHUNK_HEADER_BYTES + SYSEX_BANK_CHUNK_HEX_BYTES];
    payload[0] = slot & 0x7F;
    payload[1] = (uint8_t)(chunk_idx & 0x7F);
    payload[2] = (uint8_t)((chunk_idx >> 7) & 0x7F);
    payload[3] = (uint8_t)(raw_len & 0x7F);
    payload[4] = (uint8_t)((raw_len >> 7) & 0x7F);
    payload[5] = is_last ? 1 : 0;

    for (uint16_t i = 0; i < raw_len; ++i) {
        const uint8_t b = raw[i];
        payload[SYSEX_BANK_CHUNK_HEADER_BYTES + i * 2] = to_hex_nibble_(b >> 4);
        payload[SYSEX_BANK_CHUNK_HEADER_BYTES + i * 2 + 1] = to_hex_nibble_(b);
    }

    send_sysex_frame(SYSEX_TYPE_BANK_CHUNK_RESP, req_id, SYSEX_STATUS_OK, payload, (uint16_t)(SYSEX_BANK_CHUNK_HEADER_BYTES + raw_len * 2));
#else
    (void)chunk_idx;
    send_nack(req_id, SYSEX_NACK_NOT_READY);
#endif
}

static inline bool from_hex_nibble_(uint8_t c, uint8_t *out) {
    if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0'); return true; }
    if (c >= 'A' && c <= 'F') { *out = (uint8_t)(c - 'A' + 10); return true; }
    if (c >= 'a' && c <= 'f') { *out = (uint8_t)(c - 'a' + 10); return true; }
    return false;
}

// Write counterpart to send_bank_chunk_sysex()/GET_BANK_CHUNK: the browser
// tool pushes a bank config file to the device live (running, no reboot),
// one small chunk at a time (see SYSEX_BANK_WRITE_RAW_CHUNK_BYTES for why
// chunks are much smaller here than the download direction). Chunk payload
// layout mirrors BANK_CHUNK_RESP: [slot, idx_lo, idx_hi, raw_len_lo,
// raw_len_hi, is_last, hex_data...].
static void handle_set_bank_chunk_(uint8_t req_id, uint8_t slot, uint16_t chunk_idx,
                                    const uint8_t *hex, uint16_t raw_len, bool is_last) {
    if (slot < 1 || slot > FLASH_MAX_BANKS) {
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_INVALID_VALUE);
        return;
    }

    if (chunk_idx == 0) {
        // (Re)start an upload for this slot - discard anything buffered
        // for a previous, never-finalised upload.
        bank_write_active_ = true;
        bank_write_slot_ = slot;
        bank_write_len_ = 0;
    }

    if (!bank_write_active_ || bank_write_slot_ != slot) {
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_INVALID_VALUE);
        return;
    }

    const uint32_t offset = (uint32_t)chunk_idx * SYSEX_BANK_WRITE_RAW_CHUNK_BYTES;
    if (offset != bank_write_len_ || offset + raw_len > sizeof(bank_write_buf_)) {
        // Chunks must arrive in order with no gaps, and the whole upload
        // must fit within our fixed RAM buffer.
        bank_write_active_ = false;
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
        return;
    }

    for (uint16_t i = 0; i < raw_len; ++i) {
        uint8_t hi, lo;
        if (!from_hex_nibble_(hex[i * 2], &hi) || !from_hex_nibble_(hex[i * 2 + 1], &lo)) {
            bank_write_active_ = false;
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_INVALID_VALUE);
            return;
        }
        bank_write_buf_[offset + i] = (uint8_t)((hi << 4) | lo);
    }
    bank_write_len_ = offset + raw_len;

    if (!is_last) {
        enqueue_pending_sysex(PENDING_SEND_ACK, req_id);
        return;
    }

    bank_write_active_ = false;

#ifdef ENABLE_LITTLEFS
    char path[32];
    snprintf(path, sizeof(path), BANK_CONFIG_SAVE_PATH_FMT, slot);
    File f = LittleFS.open(path, "w");
    if (!f) {
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_WRITE_FAILED);
        return;
    }
    size_t written = f.write(bank_write_buf_, bank_write_len_);
    f.close();
    if (written != bank_write_len_) {
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_WRITE_FAILED);
        return;
    }

    bankManager.refresh_bank(slot);
#else
    enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_NOT_READY);
    return;
#endif

    enqueue_pending_sysex(PENDING_SEND_ACK, req_id);
}

// Sample metadata file (Phase F.4): opaque byte streaming for the single
// fixed file at SAMPLE_META_SAVE_PATH. Firmware never parses this file's
// contents - it's purely for the PC tool's type/tag UI. Mirrors
// send_bank_chunk_sysex()/handle_set_bank_chunk_() but without a slot
// parameter (there's only ever one metadata file).
static void send_sample_meta_info_sysex(uint8_t req_id) {
#ifdef ENABLE_LITTLEFS
    uint32_t total_bytes = 0;
    File f = LittleFS.open(SAMPLE_META_SAVE_PATH, "r");
    if (f) {
        total_bytes = (uint32_t)f.size();
        f.close();
    }
    uint8_t payload[5];
    put_u32_7bit_(payload, total_bytes);
    send_sysex_frame(SYSEX_TYPE_SAMPLE_META_INFO_RESP, req_id, SYSEX_STATUS_OK, payload, sizeof(payload));
#else
    send_nack(req_id, SYSEX_NACK_NOT_READY);
#endif
}

static void send_sample_meta_chunk_sysex(uint8_t req_id, uint16_t chunk_idx) {
#ifdef ENABLE_LITTLEFS
    File f = LittleFS.open(SAMPLE_META_SAVE_PATH, "r");
    if (!f) {
        send_nack(req_id, SYSEX_NACK_NOT_READY);
        return;
    }

    const uint32_t total_bytes = (uint32_t)f.size();
    const uint32_t offset = (uint32_t)chunk_idx * SYSEX_META_RAW_CHUNK_BYTES;
    if (offset >= total_bytes && total_bytes != 0) {
        f.close();
        send_nack(req_id, SYSEX_NACK_INVALID_VALUE);
        return;
    }

    const uint32_t remaining = total_bytes - offset;
    const uint16_t raw_len = (uint16_t)((remaining >= SYSEX_META_RAW_CHUNK_BYTES) ? SYSEX_META_RAW_CHUNK_BYTES : remaining);
    const bool is_last = (offset + raw_len) >= total_bytes;

    uint8_t raw[SYSEX_META_RAW_CHUNK_BYTES];
    if (raw_len > 0) {
        f.seek(offset);
        size_t got = f.read(raw, raw_len);
        f.close();
        if (got != raw_len) {
            send_nack(req_id, SYSEX_NACK_NOT_READY);
            return;
        }
    } else {
        f.close();
    }

    uint8_t payload[SYSEX_META_CHUNK_HEADER_BYTES + SYSEX_META_CHUNK_HEX_BYTES];
    payload[0] = (uint8_t)(chunk_idx & 0x7F);
    payload[1] = (uint8_t)((chunk_idx >> 7) & 0x7F);
    payload[2] = (uint8_t)(raw_len & 0x7F);
    payload[3] = (uint8_t)((raw_len >> 7) & 0x7F);
    payload[4] = is_last ? 1 : 0;

    for (uint16_t i = 0; i < raw_len; ++i) {
        const uint8_t b = raw[i];
        payload[SYSEX_META_CHUNK_HEADER_BYTES + i * 2] = to_hex_nibble_(b >> 4);
        payload[SYSEX_META_CHUNK_HEADER_BYTES + i * 2 + 1] = to_hex_nibble_(b);
    }

    send_sysex_frame(SYSEX_TYPE_SAMPLE_META_CHUNK_RESP, req_id, SYSEX_STATUS_OK, payload, (uint16_t)(SYSEX_META_CHUNK_HEADER_BYTES + raw_len * 2));
#else
    (void)chunk_idx;
    send_nack(req_id, SYSEX_NACK_NOT_READY);
#endif
}

// Write counterpart to send_sample_meta_chunk_sysex()/GET_SAMPLE_META_CHUNK:
// chunk payload layout mirrors SAMPLE_META_CHUNK_RESP but without the slot
// byte: [idx_lo, idx_hi, raw_len_lo, raw_len_hi, is_last, hex_data...].
static void handle_set_sample_meta_chunk_(uint8_t req_id, uint16_t chunk_idx,
                                           const uint8_t *hex, uint16_t raw_len, bool is_last) {
    if (chunk_idx == 0) {
        meta_write_active_ = true;
        meta_write_len_ = 0;
    }

    if (!meta_write_active_) {
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_INVALID_VALUE);
        return;
    }

    const uint32_t offset = (uint32_t)chunk_idx * SYSEX_META_WRITE_RAW_CHUNK_BYTES;
    if (offset != meta_write_len_ || offset + raw_len > sizeof(meta_write_buf_)) {
        meta_write_active_ = false;
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
        return;
    }

    for (uint16_t i = 0; i < raw_len; ++i) {
        uint8_t hi, lo;
        if (!from_hex_nibble_(hex[i * 2], &hi) || !from_hex_nibble_(hex[i * 2 + 1], &lo)) {
            meta_write_active_ = false;
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_INVALID_VALUE);
            return;
        }
        meta_write_buf_[offset + i] = (uint8_t)((hi << 4) | lo);
    }
    meta_write_len_ = offset + raw_len;

    if (!is_last) {
        enqueue_pending_sysex(PENDING_SEND_ACK, req_id);
        return;
    }

    meta_write_active_ = false;

#ifdef ENABLE_LITTLEFS
    File f = LittleFS.open(SAMPLE_META_SAVE_PATH, "w");
    if (!f) {
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_WRITE_FAILED);
        return;
    }
    size_t written = f.write(meta_write_buf_, meta_write_len_);
    f.close();
    if (written != meta_write_len_) {
        enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_WRITE_FAILED);
        return;
    }
#else
    enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_NOT_READY);
    return;
#endif

    enqueue_pending_sysex(PENDING_SEND_ACK, req_id);
}

// Reports the currently active MIDIOutputProcessor's slots generically (by
// index: note, channel, label) so the web tool never has to hardcode which
// processor subclass (Full/Half drum kit etc.) is compiled in.
static void send_slots_info_sysex(uint8_t req_id) {
    GenericList<BaseOutput*> *nodes = output_processor ? output_processor->get_available_outputs() : nullptr;
    if (!nodes) {
        send_nack(req_id, SYSEX_NACK_NOT_READY);
        return;
    }

    unsigned count = (unsigned)nodes->size();
    if (count > SYSEX_MAX_SLOTS_REPORTED) count = SYSEX_MAX_SLOTS_REPORTED;

    uint8_t payload[SYSEX_SLOTS_MAX_PAYLOAD];
    uint16_t pos = 0;
    payload[pos++] = (uint8_t)count;

    for (unsigned i = 0; i < count; ++i) {
        BaseOutput *node = nodes->get(i);
        // BaseOutput::get_note_number()/get_channel() default to -1 for non-MIDI
        // outputs; MIDIBaseOutput (and its subclasses) override them. No RTTI needed.
        int8_t note = node ? (int8_t)node->get_note_number() : -1;
        int8_t channel = node ? (int8_t)node->get_channel() : -1;
        const char *label = (node && node->label[0]) ? node->label : "";

        uint8_t label_len = 0;
        while (label[label_len] != '\0' && label_len < SYSEX_SLOT_LABEL_MAX) ++label_len;

        payload[pos++] = (uint8_t)note & 0x7F;
        payload[pos++] = (uint8_t)channel & 0x7F;
        payload[pos++] = label_len;
        memcpy(payload + pos, label, label_len);
        pos += label_len;
    }

    send_sysex_frame(SYSEX_TYPE_SLOTS_INFO_RESP, req_id, SYSEX_STATUS_OK, payload, pos);
}

// Lists the shared content-addressed SampleStore (see audio/sample_store.h)
// so the web tool can offer a "assign an already-uploaded sample to this
// voice" picker without the user having to know/enter raw content hashes.
static void send_samples_info_sysex(uint8_t req_id) {
    uint8_t payload[5];
    put_u32_7bit_(payload, sampleStore.is_valid() ? sampleStore.num_entries() : 0);
    send_sysex_frame(SYSEX_TYPE_SAMPLES_INFO_RESP, req_id, SYSEX_STATUS_OK, payload, sizeof(payload));
}

static void send_samples_chunk_sysex(uint8_t req_id, uint16_t chunk_idx) {
    if (!sampleStore.is_valid()) {
        send_nack(req_id, SYSEX_NACK_NOT_READY);
        return;
    }

    const uint32_t total_entries = sampleStore.num_entries();
    const uint32_t start = (uint32_t)chunk_idx * SYSEX_SAMPLES_ENTRIES_PER_CHUNK;
    if (start >= total_entries) {
        send_nack(req_id, SYSEX_NACK_INVALID_VALUE);
        return;
    }

    const uint32_t remaining = total_entries - start;
    const uint32_t count = (remaining >= SYSEX_SAMPLES_ENTRIES_PER_CHUNK) ? SYSEX_SAMPLES_ENTRIES_PER_CHUNK : remaining;
    const bool is_last = (start + count) >= total_entries;

    uint8_t raw[SYSEX_SAMPLES_CHUNK_RAW_BYTES];
    for (uint32_t i = 0; i < count; ++i) {
        const SampleStoreEntryHeader *entry = sampleStore.entry_at(start + i);
        memcpy(raw + i * SAMPLESTORE_ENTRY_SIZE, entry, SAMPLESTORE_ENTRY_SIZE);
    }
    const uint16_t raw_len = (uint16_t)(count * SAMPLESTORE_ENTRY_SIZE);

    uint8_t payload[SYSEX_SAMPLES_CHUNK_HEADER_BYTES + SYSEX_SAMPLES_CHUNK_RAW_BYTES * 2];
    payload[0] = (uint8_t)(chunk_idx & 0x7F);
    payload[1] = (uint8_t)((chunk_idx >> 7) & 0x7F);
    payload[2] = (uint8_t)(raw_len & 0x7F);
    payload[3] = (uint8_t)((raw_len >> 7) & 0x7F);
    payload[4] = is_last ? 1 : 0;

    for (uint16_t i = 0; i < raw_len; ++i) {
        const uint8_t b = raw[i];
        payload[SYSEX_SAMPLES_CHUNK_HEADER_BYTES + i * 2] = to_hex_nibble_(b >> 4);
        payload[SYSEX_SAMPLES_CHUNK_HEADER_BYTES + i * 2 + 1] = to_hex_nibble_(b);
    }

    send_sysex_frame(SYSEX_TYPE_SAMPLES_CHUNK_RESP, req_id, SYSEX_STATUS_OK, payload, (uint16_t)(SYSEX_SAMPLES_CHUNK_HEADER_BYTES + raw_len * 2));
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
        const char *name = bankManager.get_bank_name(slot);
        if (name) {
            for (int i = 0; i < SYSEX_BANK_NAME - 1; i++) {
                char c = name[i];
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
    // Accept either callback layout:
    // 1) framed:   [0xF0, MFR, DEV, VER, TYPE, REQ, STATUS, ...payload..., 0xF7]
    // 2) unframed: [MFR, DEV, VER, TYPE, REQ, STATUS, ...payload...]
    if (!data || size < 6) return;

    const bool framed = (size >= 8 && data[0] == 0xF0 && data[size - 1] == 0xF7);
    const uint8_t *m = framed ? (data + 1) : data;
    const unsigned mlen = framed ? (size - 2) : size;

    if (mlen < 6) return;
    if (m[0] != SYSEX_MFR_ID || m[1] != SYSEX_DEVICE_ID) return;

    const uint8_t ver = m[2];
    const uint8_t type = m[3];
    const uint8_t req_id = m[4] & 0x7F;
    const uint8_t payload_status = m[5];
    const uint8_t *d = m + 6;
    const unsigned payload_len = mlen - 6;

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

    } else if (type == SYSEX_TYPE_GET_BANK_INFO_REQ) {
        if (payload_len != 1) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        const uint8_t slot = d[0] & 0x7F;
        enqueue_pending_sysex(PENDING_SEND_BANK_INFO, req_id, slot);

    } else if (type == SYSEX_TYPE_GET_BANK_CHUNK_REQ) {
        if (payload_len != 3) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        const uint8_t slot = d[0] & 0x7F;
        const uint8_t idx_lo = d[1] & 0x7F;
        const uint8_t idx_hi = d[2] & 0x7F;
        enqueue_pending_sysex(PENDING_SEND_BANK_CHUNK, req_id, slot, idx_lo, idx_hi);

    } else if (type == SYSEX_TYPE_GET_SLOTS_REQ) {
        if (payload_len != 0) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        enqueue_pending_sysex(PENDING_SEND_SLOTS_INFO, req_id);

    } else if (type == SYSEX_TYPE_SET_BANK_CHUNK_REQ) {
        if (payload_len < SYSEX_BANK_CHUNK_HEADER_BYTES) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        const uint8_t slot = d[0] & 0x7F;
        const uint16_t chunk_idx = (uint16_t)(d[1] & 0x7F) | ((uint16_t)(d[2] & 0x7F) << 7);
        const uint16_t raw_len   = (uint16_t)(d[3] & 0x7F) | ((uint16_t)(d[4] & 0x7F) << 7);
        const bool is_last = d[5] != 0;
        const unsigned hex_len = payload_len - SYSEX_BANK_CHUNK_HEADER_BYTES;
        if (raw_len > SYSEX_BANK_WRITE_RAW_CHUNK_BYTES || hex_len != (unsigned)raw_len * 2) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        handle_set_bank_chunk_(req_id, slot, chunk_idx, d + SYSEX_BANK_CHUNK_HEADER_BYTES, raw_len, is_last);

    } else if (type == SYSEX_TYPE_GET_SAMPLES_INFO_REQ) {
        if (payload_len != 0) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        enqueue_pending_sysex(PENDING_SEND_SAMPLES_INFO, req_id);

    } else if (type == SYSEX_TYPE_GET_SAMPLES_CHUNK_REQ) {
        if (payload_len != 2) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        const uint8_t idx_lo = d[0] & 0x7F;
        const uint8_t idx_hi = d[1] & 0x7F;
        enqueue_pending_sysex(PENDING_SEND_SAMPLES_CHUNK, req_id, idx_lo, idx_hi);

    } else if (type == SYSEX_TYPE_GET_SAMPLE_META_INFO_REQ) {
        if (payload_len != 0) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        enqueue_pending_sysex(PENDING_SEND_SAMPLE_META_INFO, req_id);

    } else if (type == SYSEX_TYPE_GET_SAMPLE_META_CHUNK_REQ) {
        if (payload_len != 2) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        const uint8_t idx_lo = d[0] & 0x7F;
        const uint8_t idx_hi = d[1] & 0x7F;
        enqueue_pending_sysex(PENDING_SEND_SAMPLE_META_CHUNK, req_id, idx_lo, idx_hi);

    } else if (type == SYSEX_TYPE_SET_SAMPLE_META_CHUNK_REQ) {
        if (payload_len < SYSEX_META_CHUNK_HEADER_BYTES) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        const uint16_t chunk_idx = (uint16_t)(d[0] & 0x7F) | ((uint16_t)(d[1] & 0x7F) << 7);
        const uint16_t raw_len   = (uint16_t)(d[2] & 0x7F) | ((uint16_t)(d[3] & 0x7F) << 7);
        const bool is_last = d[4] != 0;
        const unsigned hex_len = payload_len - SYSEX_META_CHUNK_HEADER_BYTES;
        if (raw_len > SYSEX_META_WRITE_RAW_CHUNK_BYTES || hex_len != (unsigned)raw_len * 2) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }
        handle_set_sample_meta_chunk_(req_id, chunk_idx, d + SYSEX_META_CHUNK_HEADER_BYTES, raw_len, is_last);

    } else if (type == SYSEX_TYPE_SET_CONFIG_REQ) {
        if (payload_len < SYSEX_DATA_LEN) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_BAD_LENGTH);
            return;
        }

        if (d[0] > FLASH_MAX_BANKS) {
            enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_INVALID_VALUE);
            return;
        }

        // Attempt the bank switch BEFORE touching current_settings.active_bank
        // (and before any flash write below) - only commit the new value once
        // we know it actually took effect. Otherwise a failed switch (e.g. an
        // empty/never-configured bank slot) would leave current_settings
        // pointing at a bank that was never actually loaded, desyncing it
        // from bankManager.active_bank() (the bank really playing). That
        // desync then makes a *later* request to switch back to the
        // previously-active bank look like a no-op (same value already
        // "set"), silently skipping the switch_bank() call that should have
        // restored playback.
        if ((int)d[0] != bankManager.active_bank()) {
            if (!bankManager.switch_bank(d[0])) {
                enqueue_pending_sysex(PENDING_SEND_NACK, req_id, SYSEX_NACK_INVALID_VALUE);
                return;
            }
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

        // Optional 20th payload byte: persist flag. 1 (or absent, for
        // backwards compatibility with older web-tool builds) writes the
        // settings to flash immediately. 0 applies them to the running
        // device only, without a flash write - lets the web tool "try out"
        // settings (e.g. active bank) live without wearing flash's limited
        // rewrite lifetime; an explicit save (persist=1) is needed to make
        // the change survive a reboot.
        const bool persist = (payload_len < SYSEX_DATA_LEN + 1) || (d[SYSEX_DATA_LEN] != 0);
        if (persist) settings_save(&current_settings);

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
