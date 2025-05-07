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

#include "audio/samps.h"

void setup_samples();


#include "ComputerCard.h"
#include <cmath>

class SamplePlayer : public ComputerCard
{
public:

	/*SamplePlayer()
	{

	}*/

    int_fast32_t newsample,samplesum=0;
    uint_fast32_t index;
    int_fast16_t samp0,samp1,delta,tracksample;

    void CalculateSamples() {
        for (int_fast8_t track = 0 ; track < NUM_VOICES ; ++track) {  // look for samples that are playing, scale their volume, and add them up
            tracksample=voice[track].sample; // precompute for a little more speed below
            index=voice[track].sampleindex>>12; // get the integer part of the sample increment
            if (index <= sample[tracksample].samplesize) { // if sample is playing, do interpolation   
              //Serial.printf("track %i is playing sample %i\n", track, tracksample); Serial.flush();
              samp0=sample[tracksample].samplearray[index]; // get the first sample to interpolate
              samp1=sample[tracksample].samplearray[index+1];// get the second sample
              delta=samp1-samp0;
              newsample=(int_fast32_t)samp0+((int_fast32_t)delta*((int_fast32_t)voice[track].sampleindex & 0x0fff))/4096; // interpolate between the two samples
              //samplesum+=(newsample*voice[track].level); // changed to MIDI velocity levels 0-127
              samplesum+=newsample;
              voice[track].sampleindex+=voice[track].sampleincrement; // add step increment
            }
            /*if (index <= sample[tracksample].samplesize) {
                newsample = sample[tracksample].samplearray[index]; // get the first sample to interpolate
                samplesum+=newsample;
                voice[track].sampleindex+=voice[track].sampleincrement; // add step increment
            }*/
        }

        //samplesum = random() % 32768; // random sample for testing
    
        samplesum=samplesum>>7;  // adjust for volume multiply above
        //samplesum=>>=6;  // scale down to 12 bit range
        if  (samplesum>32767) samplesum=32767; // clip if sample sum is too large
        if  (samplesum<-32767) samplesum=-32767;
    }

    virtual void ProcessSample()
	{
        //CalculateSamples();
        
		AudioOut1(samplesum);
		AudioOut2(samplesum);		
	}
};

extern SamplePlayer sw;