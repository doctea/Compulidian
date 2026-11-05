#pragma once

#include <stdint.h>

enum MomentaryRole : uint8_t {
    MOMENTARY_MUTE = 0,
    MOMENTARY_BANK = 1,
};

class DeviceControls {
public:
    enum Mode { BootCheck, BootRelease, BootSelect, BootExit, Performance, BankSelect, CommitPending };

    void update(uint32_t now, bool down, uint16_t main, uint16_t knob_x,
                uint8_t valid_banks, uint8_t active_bank, MomentaryRole role) {
        if (!initialized_) {
            initialized_ = true;
            raw_down_ = down;
            changed_at_ = now;
            main_value_ = main;
            x_value_ = knob_x;
        }
        if (down != raw_down_) {
            raw_down_ = down;
            changed_at_ = now;
        }
        const bool settled = uint32_t(now - changed_at_) >= 30;
        const bool pressed = settled && down && !down_;
        const bool released = settled && !down && down_;
        if (settled) down_ = down;

        if (mode_ == BootCheck && settled) {
            mode_ = down_ ? BootRelease : Performance;
            if (down_) begin_selection(main, knob_x, active_bank, role);
        } else if (mode_ == BootRelease && released) {
            mode_ = BootSelect;
        } else if (mode_ == BootSelect) {
            select_bank(main, valid_banks);
            if (distance(knob_x, x_anchor_) > 128) role_moved_ = true;
            if (role_moved_) {
                if (knob_x < 1952) candidate_role_ = MOMENTARY_MUTE;
                if (knob_x > 2144) candidate_role_ = MOMENTARY_BANK;
            }
            if (pressed) {
                setup_request_ = true;
                mode_ = CommitPending;
            }
        } else if (mode_ == BootExit && released) {
            mode_ = Performance;
        } else if (mode_ == Performance && role == MOMENTARY_BANK && pressed) {
            begin_selection(main, knob_x, active_bank, role);
            mode_ = BankSelect;
        } else if (mode_ == BankSelect) {
            if (role != MOMENTARY_BANK) {
                mode_ = Performance;
                return;
            }
            select_bank(main, valid_banks);
            if (released) {
                setup_request_ = false;
                mode_ = CommitPending;
            }
        }
    }

    void complete(bool success, uint16_t) {
        if (mode_ != CommitPending) return;
        if (setup_request_) {
            mode_ = success ? BootExit : BootSelect;
        } else {
            mode_ = Performance;
        }
    }

    uint16_t filter_main(uint16_t raw) {
        if (owns_main()) return main_value_;
        main_value_ = raw;
        return raw;
    }
    uint16_t filter_x(uint16_t raw) {
        if (setup_active()) return x_value_;
        x_value_ = raw;
        return raw;
    }

    bool owns_main() const { return mode_ != BootCheck && mode_ != Performance; }
    bool setup_active() const {
        return mode_ == BootRelease || mode_ == BootSelect || mode_ == BootExit
            || (mode_ == CommitPending && setup_request_);
    }
    bool request_pending() const { return mode_ == CommitPending; }
    bool setup_request() const { return setup_request_; }
    bool muted(MomentaryRole role) const {
        return mode_ == Performance && role == MOMENTARY_MUTE && down_;
    }
    bool suppress_hold() const { return mode_ != Performance; }
    uint8_t candidate_bank() const { return candidate_bank_; }
    MomentaryRole candidate_role() const { return candidate_role_; }
    Mode mode() const { return mode_; }

private:
    Mode mode_ = BootCheck;
    bool initialized_ = false;
    bool raw_down_ = false;
    bool down_ = false;
    bool main_moved_ = false;
    bool role_moved_ = false;
    bool setup_request_ = false;
    uint32_t changed_at_ = 0;
    uint16_t main_value_ = 0;
    uint16_t x_value_ = 0;
    uint16_t main_anchor_ = 0;
    uint16_t x_anchor_ = 0;
    uint8_t candidate_bank_ = 0;
    MomentaryRole candidate_role_ = MOMENTARY_MUTE;

    static uint16_t distance(uint16_t first, uint16_t second) {
        return first > second ? first - second : second - first;
    }
    void begin_selection(uint16_t main, uint16_t knob_x, uint8_t bank, MomentaryRole role) {
        main_anchor_ = main;
        x_anchor_ = knob_x;
        main_moved_ = false;
        role_moved_ = false;
        candidate_bank_ = bank;
        candidate_role_ = role;
    }
    void select_bank(uint16_t main, uint8_t valid_banks) {
        if (distance(main, main_anchor_) > 64) main_moved_ = true;
        if (!main_moved_) return;
        valid_banks = (valid_banks & 0x1f) | 1;
        uint8_t count = 0;
        uint8_t current_index = 0;
        for (uint8_t bank = 0; bank < 5; ++bank) {
            if (!(valid_banks & (1u << bank))) continue;
            if (bank == candidate_bank_) current_index = count;
            ++count;
        }
        const int lower = int(current_index) * 4096 / count - 24;
        const int upper = int(current_index + 1) * 4096 / count + 24;
        if (int(main) >= lower && int(main) < upper) return;
        uint8_t desired_index = uint32_t(main) * count / 4096;
        for (uint8_t bank = 0; bank < 5; ++bank) {
            if (!(valid_banks & (1u << bank))) continue;
            if (desired_index-- == 0) {
                candidate_bank_ = bank;
                return;
            }
        }
    }
};

extern DeviceControls device_controls;