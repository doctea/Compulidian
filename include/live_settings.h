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

class DeviceSettingsHost : public SHStorage<0, 24> {
    Settings *target;

public:
    DeviceSettingsHost(Settings *target) : target(target) {
        set_path_segment("DeviceSettings");
    }

    virtual void setup_saveable_settings() override {
        ISaveableSettingHost::setup_saveable_settings();

        register_setting(new VarSetting<uint8_t>("active_bank", "DeviceSettings", &target->active_bank), SL_SCOPE_SYSTEM);

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
