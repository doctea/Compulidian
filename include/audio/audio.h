#pragma once

#include <SPI.h>

#define NUM_VOICES 14

// DAC parameters
//  A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
//  A-channel, 2x, active -- NB 2x = ~3.27v max, not 4.096
#define DAC_config_chan_A_gain 0b0001000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000
#define DAC_config_chan_B_gain 0b1001000000000000

struct voice_t {
    int16_t sample;   // index of the sample structure in sampledefs.h
    int16_t level;   // 0-1000 for legacy reasons
    volatile uint32_t sampleindex; // 20:12 fixed point index into the sample array
    uint16_t sampleincrement; // 1:12 fixed point sample step for pitch changes 
};

extern voice_t voice[NUM_VOICES];


void setup_samples();

