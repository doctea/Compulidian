#pragma once
#include <stdint.h>

// Magic number written at the start of a valid settings block.
#define SETTINGS_MAGIC   0x43535447u  // "CSTG"
#define SETTINGS_VERSION 1u

// Settings are stored in the first 256 bytes of the settings flash sector.
// The rest of the 4 KB sector is left as 0xFF (erased flash).
#define SETTINGS_STORED_SIZE 256u

// ---------------------------------------------------------------------------
// Output jack type
// ---------------------------------------------------------------------------
enum OutputType : uint8_t {
    OUTPUT_TYPE_AUDIO = 0,  // audio sample output
    OUTPUT_TYPE_CLOCK = 1,  // MIDI clock pulse (rate controlled by PPQN)
    OUTPUT_TYPE_GATE  = 2,  // gate high/low following note on/off
    OUTPUT_TYPE_CV    = 3,  // 1V/oct CV pitch output
};

// ---------------------------------------------------------------------------
// Input/knob role
// ---------------------------------------------------------------------------
enum InputRole : uint8_t {
    INPUT_ROLE_NONE    = 0,  // not mapped
    INPUT_ROLE_BPM     = 1,  // modulate BPM
    INPUT_ROLE_DENSITY = 2,  // modulate pattern density / fill amount
    INPUT_ROLE_PITCH   = 3,  // global sample pitch offset
    INPUT_ROLE_BANK    = 4,  // select sample bank (quantised to integer banks)
};

// ---------------------------------------------------------------------------
// Per-output configuration (4 bytes, no implicit padding)
// ---------------------------------------------------------------------------
struct OutputConfig {
    OutputType type;      // what this output produces
    uint8_t    channel;   // MIDI channel (1-16) or voice index
    uint8_t    note;      // MIDI note for GATE/CV modes, or 0
    uint8_t    reserved;  // must be 0
};
// sizeof(OutputConfig) == 4

// ---------------------------------------------------------------------------
// Device settings — stored flat in flash, padded to SETTINGS_STORED_SIZE.
//
// Byte map:
//   0   magic           4 B
//   4   version         4 B
//   8   active_bank     1 B
//   9   reserved        3 B
//  12   outputs[4]     16 B   (4 × OutputConfig)
//  28   cv_input_role   2 B
//  30   knob_role       3 B
//  33   _pad          223 B
// 256   (end)
// ---------------------------------------------------------------------------
struct Settings {
    uint32_t    magic;              // SETTINGS_MAGIC
    uint32_t    version;            // SETTINGS_VERSION
    uint8_t     active_bank;        // 0 = compiled-in bank, 1-4 = flash bank slot
    uint8_t     reserved[3];        // must be 0
    OutputConfig outputs[4];        // pulse1, pulse2, cv1, cv2
    InputRole   cv_input_role[2];   // role of CV1 and CV2 inputs
    InputRole   knob_role[3];       // role of Main, X, Y knobs
    uint8_t     _pad[SETTINGS_STORED_SIZE - 4 - 4 - 1 - 3
                     - 4 * 4   // outputs[4], sizeof(OutputConfig)==4
                     - 2 - 3]; // cv_input_role + knob_role
};

#ifdef __cplusplus
static_assert(sizeof(Settings) == SETTINGS_STORED_SIZE,
              "Settings must be exactly SETTINGS_STORED_SIZE bytes");
static_assert(sizeof(OutputConfig) == 4,
              "OutputConfig layout mismatch");
#endif

// ---------------------------------------------------------------------------
// API (C++ only — Settings uses C++ enum types)
// ---------------------------------------------------------------------------

// Returns a Settings struct with all fields at their defaults.
Settings settings_defaults();

// Reads Settings from flash.  Returns defaults if the magic/version is wrong.
Settings settings_load();

// Writes Settings to the settings sector in flash.
// IMPORTANT: This pauses core 1, disables interrupts, erases the settings
// sector (4 KB), reprograms it, then resumes core 1.  Call only from core 0.
void settings_save(const Settings *s);
