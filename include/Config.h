#pragma once

#define KNOB_MIN_BPM 30.0f
#define KNOB_MAX_BPM 180.0f

// choose one of these
#define ChosenDrumKitMIDIOutputProcessor FullDrumKitMIDIOutputProcessor
//#define ChosenDrumKitMIDIOutputProcessor HalfDrumKitMIDIOutputProcessor

//#define WAIT_FOR_SERIAL   // for debugging - wait for serial to be connected before starting

//#define PLAY_SOUNDS_WITH_INTERRUPTS // play sounds using interrupts on second core -- this doesn't work