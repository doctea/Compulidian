#pragma once

#include <stdint.h>
#include "hardware/sync.h"
#include "pico/util/queue.h"

class USBMIDITxQueue {
    struct Packet {
        uint8_t bytes[4];
    };

    queue_t packets;
    Packet pending_packet = {};
    bool has_pending_packet = false;
    volatile bool recovery_pending = false;
    uint8_t recovery_channel = 0;

public:
    static constexpr unsigned capacity = 128;
    volatile uint32_t overflow_count = 0;
    volatile uint32_t dropped_count = 0;

    void begin() {
        queue_init(&packets, sizeof(Packet), capacity);
    }

    void enqueue_note(bool note_on, uint8_t pitch, uint8_t velocity, uint8_t channel) {
        if (channel < 1 || channel > 16 || pitch > 127 || velocity > 127) return;
        Packet packet = {{
            static_cast<uint8_t>(note_on ? 0x09 : 0x08),
            static_cast<uint8_t>((note_on ? 0x90 : 0x80) | (channel - 1)),
            pitch, velocity
        }};
        uint32_t irq_state = save_and_disable_interrupts();
        if (recovery_pending) {
            ++dropped_count;
        } else if (!queue_try_add(&packets, &packet)) {
            ++overflow_count;
            ++dropped_count;
            recovery_pending = true;
        }
        restore_interrupts(irq_state);
    }

    template <typename Transport>
    void drain(Transport &transport, bool sysex_busy) {
        uint32_t irq_state = save_and_disable_interrupts();
        restore_interrupts(irq_state);
        if (__get_current_exception() != 0 || irq_state != 0 || get_core_num() != 0 || sysex_busy) return;

        for (unsigned sent = 0; sent < 64; ++sent) {
            if (recovery_pending && recovery_channel == 0) {
                Packet discarded;
                uint32_t discarded_count = has_pending_packet ? 1 : 0;
                while (queue_try_remove(&packets, &discarded)) {
                    ++discarded_count;
                }
                uint32_t saved_irq_state = save_and_disable_interrupts();
                dropped_count += discarded_count;
                restore_interrupts(saved_irq_state);
                has_pending_packet = false;
                recovery_channel = 1;
            }
            if (recovery_channel != 0) {
                const uint8_t packet[4] = {
                    0x0B, static_cast<uint8_t>(0xB0 | (recovery_channel - 1)), 120, 0
                };
                if (!transport.writePacket(packet)) break;
                if (++recovery_channel == 17) {
                    recovery_channel = 0;
                    recovery_pending = false;
                }
            } else {
                if (!has_pending_packet) {
                    if (!queue_try_remove(&packets, &pending_packet)) break;
                    has_pending_packet = true;
                }
                if (!transport.writePacket(pending_packet.bytes)) break;
                has_pending_packet = false;
            }
        }
    }
};