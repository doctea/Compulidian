#pragma once

// Wraps the fields of a Settings struct (settings.h) as saveloadlib settings
// so they can be enumerated/get/set live and persisted to LittleFS, without
// changing the Settings struct itself or the settings_load()/settings_save()
// call sites (see settings.cpp for how this is wired in).
// Gated on ENABLE_LITTLEFS (not ENABLE_STORAGE) - see platformio.ini comment:
// ENABLE_STORAGE would also switch on saveloadlib integration throughout
// seqlib/parameters/midihelpers, which we don't want/need here.
#ifdef ENABLE_LITTLEFS

#include "saveload_settings.h"
#include "settings.h"

// Path where the device-wide (SL_SCOPE_SYSTEM) settings file is persisted.
#define LIVE_SETTINGS_SAVE_PATH "/save/settings.txt"

class DeviceSettingsHost : public SHStorage<0, 32> {
    Settings *target;

public:
    DeviceSettingsHost(Settings *target) : target(target) {
        set_path_segment("DeviceSettings");
    }

    virtual void setup_saveable_settings() override {
        ISaveableSettingHost::setup_saveable_settings();

        register_setting(new VarSetting<uint8_t>("active_bank", "DeviceSettings", &target->active_bank), SL_SCOPE_SYSTEM);
        register_setting(new VarSetting<MomentaryRole>("momentary_role", "DeviceSettings", &target->momentary_role), SL_SCOPE_SYSTEM);
        register_setting(new VarSetting<uint8_t>("cv_clock_ppqn", "DeviceSettings", &target->cv_clock_ppqn), SL_SCOPE_SYSTEM);
        register_setting(new VarSetting<uint8_t>("clock_options_ver", "DeviceSettings", &target->extended_settings_version), SL_SCOPE_SYSTEM);
        register_setting(new VarSetting<uint8_t>("cv_reset_required", "DeviceSettings", &target->cv_reset_required), SL_SCOPE_SYSTEM);
        register_setting(new VarSetting<uint8_t>("cv_reset_song", "DeviceSettings", &target->cv_reset_song_position), SL_SCOPE_SYSTEM);
        register_setting(new VarSetting<uint8_t>("fills_enabled", "DeviceSettings", &target->fills_enabled), SL_SCOPE_SYSTEM);
        register_setting(new VarSetting<uint8_t>("mutation_amount", "DeviceSettings", &target->mutation_amount), SL_SCOPE_SYSTEM);
        register_setting(new VarSetting<uint8_t>("reset_mutation", "DeviceSettings", &target->reset_before_mutate), SL_SCOPE_SYSTEM);
        register_setting(new VarSetting<uint8_t>("seed_card_id", "DeviceSettings", &target->add_card_id_to_seed), SL_SCOPE_SYSTEM);

        for (int i = 0; i < 4; i++) {
            char lbl[24];
            snprintf(lbl, sizeof(lbl), "output%d_type", i);
            register_setting(new VarSetting<OutputType>(lbl, "DeviceSettings", &target->outputs[i].type), SL_SCOPE_SYSTEM);
            snprintf(lbl, sizeof(lbl), "output%d_channel", i);
            register_setting(new VarSetting<uint8_t>(lbl, "DeviceSettings", &target->outputs[i].channel), SL_SCOPE_SYSTEM);
            snprintf(lbl, sizeof(lbl), "output%d_note", i);
            register_setting(new VarSetting<uint8_t>(lbl, "DeviceSettings", &target->outputs[i].note), SL_SCOPE_SYSTEM);
        }

        for (int i = 0; i < 2; i++) {
            char lbl[24];
            snprintf(lbl, sizeof(lbl), "cv_input_role%d", i);
            register_setting(new VarSetting<InputRole>(lbl, "DeviceSettings", &target->cv_input_role[i]), SL_SCOPE_SYSTEM);
        }

        for (int i = 0; i < 3; i++) {
            char lbl[24];
            snprintf(lbl, sizeof(lbl), "knob_role%d", i);
            register_setting(new VarSetting<InputRole>(lbl, "DeviceSettings", &target->knob_role[i]), SL_SCOPE_SYSTEM);
        }
    }
};

#endif // ENABLE_LITTLEFS
