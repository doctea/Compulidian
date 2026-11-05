#pragma once

#include <SPI.h>

#define IN_AUDIO_WORKER 1   // called from the audio worker loop on core1
#define IN_MAIN_LOOP    2   // called from the main loop on core0

extern int NUM_VOICES;

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

    volatile bool interpolate_enabled = DEFAULT_INTERPOLATION_ENABLED; // default to interpolation enabled
    volatile int calculate_mode = CALCULATE_SAMPLES_MODE; // default to processing in interrupt on second core
    volatile bool enable_volume = true; // default to volume enabled

    volatile uint32_t audio_callback_count = 0;
    volatile uint32_t audio_buffer_count = 0;

    volatile int global_pitch = 2048; // default to normal pitch

    static const int BUFFER_SIZE = 128;
    static const uint16_t BANK_TRANSITION_FADE_SAMPLES = 128;
    static const uint32_t OUTPUT_GAIN_ONE_Q15 = 1u << 15;
    volatile int_fast32_t buffer[2][BUFFER_SIZE];
    volatile uint_fast8_t bufferIndex = 0;

    volatile bool bufferReady[2] = {true, true}; // set to true when the buffer is ready to be written
    volatile int read_buffer_id = 0;
    volatile int write_buffer_id = 0;
    enum BankTransitionState : uint8_t {
        BANK_TRANSITION_IDLE,
        BANK_TRANSITION_FADING_OUT,
        BANK_TRANSITION_MUTED,
        BANK_TRANSITION_WAITING_FOR_BUFFER,
        BANK_TRANSITION_BUFFER_READY,
        BANK_TRANSITION_FADING_IN,
    };
    volatile BankTransitionState bank_transition_state = BANK_TRANSITION_IDLE;
    volatile uint16_t bank_transition_remaining = 0;
    volatile uint8_t bank_transition_buffer_id = 0;
    volatile uint32_t output_gain_q15 = OUTPUT_GAIN_ONE_Q15;
    volatile uint32_t bank_transition_start_gain_q15 = OUTPUT_GAIN_ONE_Q15;

    void begin_bank_transition() {
        bank_transition_start_gain_q15 = output_gain_q15;
        if (bank_transition_start_gain_q15 == 0) {
            bank_transition_remaining = 0;
            bank_transition_state = BANK_TRANSITION_MUTED;
            return;
        }
        bank_transition_remaining = BANK_TRANSITION_FADE_SAMPLES;
        bank_transition_state = BANK_TRANSITION_FADING_OUT;
    }

    bool bank_transition_is_silent() const {
        return bank_transition_state == BANK_TRANSITION_MUTED;
    }

    void finish_bank_transition() {
        bank_transition_state = BANK_TRANSITION_WAITING_FOR_BUFFER;
    }

    void __not_in_flash_func(CalculateSamples)() override {
        //return;

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
                    // sample_data[tracksample] can be null for an empty/unusable bank slot - guard against it.
                    if (sample_data[tracksample] && index < sample_data[tracksample]->size()) { // if sample is playing
                        //Serial.printf("track %i is playing sample %i\n", track, tracksample); Serial.flush();
                        if (interpolate_enabled) {  // do interpolation   
                            samp0 = sample_data[tracksample]->get_sample(index); // get the first sample to interpolate
                            samp1 = sample_data[tracksample]->get_sample(index + 1); // get the second sample
                            delta=samp1-samp0;
                            newsample=(int_fast32_t)samp0+((int_fast32_t)delta*((int_fast32_t)voice[track].sampleindex & 0x0fff))/global_pitch; // interpolate between the two samples
                        } else {
                            newsample = sample_data[tracksample]->get_sample(index); // get the first sample to interpolate
                        }
                        if (enable_volume) {
                            // newsample*=voice[track].level; // changed to MIDI velocity levels 0-127
                            newsample *= sample[tracksample].play_volume; // scale by the sample's volume (0-1000)
                            // newsample/=127; // scale down to 0-1000 for legacy reasons
                            // newsample*=sample_data[tracksample]->get_volume(); // scale by the sample's volume (0-1000)
                        }
                        samplesum+=newsample;
                        voice[track].sampleindex+=global_pitch; // add step increment
                    }

                    //samplesum = random() % 32768; // random sample for testing
                }
          
                if (enable_volume)
                    samplesum=samplesum>>10;  // adjust for volume multiply above
                else
                    samplesum=samplesum>>3;  // adjust for volume multiply above
                //samplesum=>>=6;  // scale down to 12 bit range
                if  (samplesum>32767) samplesum=32767; // clip if sample sum is too large
                if  (samplesum<-32767) samplesum=-32767;

                samplesum16 = samplesum;

                buffer[write_buffer_id][i] = samplesum16;
            }
            bufferReady[write_buffer_id] = true;
            if (bank_transition_state == BANK_TRANSITION_WAITING_FOR_BUFFER) {
                bank_transition_buffer_id = write_buffer_id;
                bank_transition_state = BANK_TRANSITION_BUFFER_READY;
            }
            write_buffer_id = (write_buffer_id + 1) % 2;
            ++audio_buffer_count;
        }
    }

    virtual void __not_in_flash_func(AudioWorkerLoop)() override
    {
        if (calculate_mode == IN_AUDIO_WORKER) CalculateSamples();
    }

    virtual void __not_in_flash_func(ProcessSample)()
	{       
        ++audio_callback_count;

        if (bank_transition_state == BANK_TRANSITION_BUFFER_READY
            && bufferIndex == 0
            && read_buffer_id == bank_transition_buffer_id) {
            bank_transition_remaining = BANK_TRANSITION_FADE_SAMPLES;
            bank_transition_state = BANK_TRANSITION_FADING_IN;
        }

        if (bank_transition_state == BANK_TRANSITION_FADING_OUT) {
            if (bank_transition_remaining > 0) {
                --bank_transition_remaining;
                output_gain_q15 = bank_transition_start_gain_q15 * bank_transition_remaining
                    / BANK_TRANSITION_FADE_SAMPLES;
            }
            if (bank_transition_remaining == 0) {
                output_gain_q15 = 0;
                bank_transition_state = BANK_TRANSITION_MUTED;
            }
        } else if (bank_transition_state == BANK_TRANSITION_FADING_IN) {
            if (bank_transition_remaining > 0) {
                const uint16_t progressed = BANK_TRANSITION_FADE_SAMPLES
                    - bank_transition_remaining + 1;
                output_gain_q15 = OUTPUT_GAIN_ONE_Q15 * progressed
                    / BANK_TRANSITION_FADE_SAMPLES;
                --bank_transition_remaining;
            }
            if (bank_transition_remaining == 0) {
                output_gain_q15 = OUTPUT_GAIN_ONE_Q15;
                bank_transition_state = BANK_TRANSITION_IDLE;
            }
        }

        const int32_t mixed_sample = buffer[read_buffer_id][bufferIndex];
		AudioOut1((mixed_sample * (int32_t)output_gain_q15) >> 15);
		// AudioOut2(buffer[read_buffer_id][bufferIndex]);

        bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
        if (bufferIndex == 0) {
            bufferReady[read_buffer_id] = true;
            read_buffer_id = (read_buffer_id + 1) % 2;
        }
    }

};

extern SamplePlayer sw;