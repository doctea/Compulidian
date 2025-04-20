#pragma once

#include <Arduino.h>

struct sample_t {
    const int16_t * samplearray; // pointer to sample array
    uint32_t samplesize; // size of the sample array
    uint32_t sampleindex; // current sample array index when playing. index at last sample= not playing
    uint8_t MIDINOTE;  // MIDI note on that plays this sample
    uint8_t play_volume; // play volume 0-127
    char sname[20];        // sample name
};

extern sample_t sample[]; // array of samples

#include "GMSamples/samples.h"

//#define NUM_SAMPLES (sizeof(sample)/sizeof(sample_t)) 
//extern sample_t sample[NUM_SAMPLES];

extern const size_t NUM_SAMPLES;