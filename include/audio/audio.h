#pragma once

#include <SPI.h>

#define IN_PROCESS_SAMPLE 1
#define IN_MAIN_LOOP 2

//#define NUM_VOICES 16
extern int NUM_VOICES;

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

extern voice_t voice[];

#include "audio/samps.h"

void setup_samples();


#include "ComputerCard.h"
#include <cmath>

class SamplePlayer : public ComputerCard
{
public:

    int_fast32_t newsample,samplesum=0;
    volatile int_fast16_t samplesum16=0;

    volatile bool interpolate_enabled = false;
    volatile int calculate_mode = CALCULATE_SAMPLES_MODE; // default to processing in interrupt on second core
    volatile bool enable_volume = false; // default to volume disabled

    volatile int global_pitch = 4096; // default to normal pitch

    //volatile bool bufferReady = true; // set to true when the buffer is ready to be processed

    static const int BUFFER_SIZE = 256;
    volatile int32_t buffer[2][BUFFER_SIZE];
    volatile int bufferIndex = 0;

    volatile bool bufferReady[2] = {true, true}; // set to true when the buffer is ready to be processed
    volatile int read_buffer_id = 0;
    volatile int write_buffer_id = 0;

    void __not_in_flash_func(CalculateSamples)() override {

        if (calculate_mode != IN_PROCESS_SAMPLE) return;

        if(bufferReady[write_buffer_id] && read_buffer_id!=write_buffer_id) {
            //if (Serial) Serial.printf("bufferReady, calculating samples into buffer %i while read_buffer_id is %i\n", write_buffer_id, read_buffer_id);
            bufferReady[write_buffer_id] = false;
            for (int i = 0 ; i < BUFFER_SIZE ; i++) {
                samplesum=0; // reset sample sum for this sample
                newsample=0; // reset sample for this sample
                for (int_fast8_t track = 0 ; track < NUM_VOICES ; ++track) {  // look for samples that are playing, scale their volume, and add them up
                    uint_fast32_t index;
                    int_fast16_t samp0,samp1,delta,tracksample;
                
                    tracksample=voice[track].sample; // precompute for a little more speed below
                    index=voice[track].sampleindex>>12; // get the integer part of the sample increment
                    if (index <= sample[tracksample].samplesize) { // if sample is playing, do interpolation   
                        //Serial.printf("track %i is playing sample %i\n", track, tracksample); Serial.flush();
                        if (interpolate_enabled) {
                            samp0=sample[tracksample].samplearray[index]; // get the first sample to interpolate
                            samp1=sample[tracksample].samplearray[index+1];// get the second sample
                            delta=samp1-samp0;
                            newsample=(int_fast32_t)samp0+((int_fast32_t)delta*((int_fast32_t)voice[track].sampleindex & 0x0fff))/4096; // interpolate between the two samples
                        } else {
                            newsample=sample[tracksample].samplearray[index]; // get the first sample to interpolate
                        }
                        if (enable_volume) 
                            newsample*=voice[track].level; // changed to MIDI velocity levels 0-127
                        samplesum+=newsample;
                        //voice[track].sampleindex+=voice[track].sampleincrement; // add step increment
                        voice[track].sampleindex+=global_pitch; // add step increment
                    }
                }

                //samplesum = random() % 32768; // random sample for testing
            
                if (enable_volume)
                    samplesum=samplesum>>7;  // adjust for volume multiply above
                else
                    samplesum=samplesum>>3;  // adjust for volume multiply above
                //samplesum=>>=6;  // scale down to 12 bit range
                if  (samplesum>32767) samplesum=32767; // clip if sample sum is too large
                if  (samplesum<-32767) samplesum=-32767;

                samplesum16 = samplesum;

                buffer[write_buffer_id][i] = samplesum16;
            }
            bufferReady[write_buffer_id] = true;
            write_buffer_id = (write_buffer_id + 1) % 2;
        }

        //bufferReady = true;
    }

    virtual void ProcessSample()
	{
        /*if (calculate_mode == IN_PROCESS_SAMPLE) {
            CalculateSamples();
        }*/
        
		AudioOut1(buffer[read_buffer_id][bufferIndex]); //samplesum16);
		AudioOut2(buffer[read_buffer_id][bufferIndex]); //samplesum16);

        bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
        if (bufferIndex == 0) {
            //bufferReady = false;
            bufferReady[read_buffer_id] = true;
            read_buffer_id = (read_buffer_id + 1) % 2;
        }
    }

};

extern SamplePlayer sw;