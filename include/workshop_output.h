#pragma once

#include "computer.h"
#include "outputs/output_processor.h"

#include "audio/samps.h"
#include "audio/audio.h"

#include "Drums.h"

uint leds_map[NUM_LEDS] = { LED5, LED6, LED4, LED3, LED1, LED2 };
class WorkshopOutputWrapper : public IMIDINoteAndCCTarget  {
  public:
    bool debug = false;

    WorkshopOutputWrapper() {}

    int8_t get_output_number_for_note(uint8_t note) {
        switch(note) {
            case GM_NOTE_ACOUSTIC_BASS_DRUM: case GM_NOTE_ELECTRIC_BASS_DRUM:
                return 0;
            case GM_NOTE_ACOUSTIC_SNARE: case GM_NOTE_ELECTRIC_SNARE:
                return 1;
            case GM_NOTE_CLOSED_HI_HAT: 
                return 2;
            case GM_NOTE_OPEN_HI_HAT:
                return 3;
            case GM_NOTE_PEDAL_HI_HAT:
                return 4;
            case GM_NOTE_CRASH_CYMBAL_1: case GM_NOTE_CRASH_CYMBAL_2:
                return 5;
        }
        return -1;
    }

    int8_t get_voice_number_for_note(uint8_t note) {
        if (!is_valid_note(note))
            return -1;

        for (int i = 0 ; i < NUM_VOICES ; i++) {
            if (sample[voice[i].sample].MIDINOTE == note) {
                return i;
            }
        }

        return -1;        
    }

    virtual void sendNoteOn(uint8_t pitch, uint8_t velocity, uint8_t channel) {
        int8_t output_number = get_output_number_for_note(pitch);
        if (debug) Serial.printf("WorkshopOutputTarget::sendNoteOn(%i, %i, %i) to output_number %i\n", pitch, velocity, channel, output_number);
        if (output_number>=0 && output_number<NUM_LEDS) {
            digitalWrite(leds_map[output_number], HIGH);

            if (output_number == 0 || output_number == 1) {
                digitalWrite(PULSE_1_RAW_OUT+output_number, HIGH);
            } else if (output_number == 2 || output_number == 3) {
                //digitalWrite(CV_OUT_1+(output_number-2), HIGH); // set CV out high
                //pwm_set_chan_level(0, output_number-2, 1024);
                pwm_set_gpio_level(CV_OUT_2+(output_number-2), 4096);
            }
        }

        int8_t voice_number = get_voice_number_for_note(pitch);
        if (voice_number != -1) {
            Serial.printf("setting sample %i to play\n", voice_number);
            voice[voice_number].sampleindex = 0;
            //sample[voice_number].play_volume = velocity; // set the velocity for the sample
        }
    }
    virtual void sendNoteOff(uint8_t pitch, uint8_t velocity, uint8_t channel) {
        int8_t output_number = get_output_number_for_note(pitch);
        
        if (output_number==-1)
            return;

        if (debug) Serial.printf("WorkshopOutputTarget::sendNoteOff(%i, %i, %i) to output_number %i\n", pitch, velocity, channel, output_number);

        if (output_number>=0 && output_number<NUM_LEDS) {
            digitalWrite(leds_map[output_number], LOW);

            if (output_number == 0 || output_number == 1) {
                digitalWrite(PULSE_1_RAW_OUT+output_number, LOW);
            } else if (output_number == 2 || output_number == 3) {
                //digitalWrite(CV_OUT_1+(output_number-2), HIGH); // set CV out low
                //pwm_set_chan_level(0, output_number-2, 0);
                pwm_set_gpio_level(CV_OUT_2+(output_number-2), 0);
            }
        }

        // don't bother stopping sample playing for now
    }

};