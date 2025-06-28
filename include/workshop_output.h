#pragma once

//#include "computer.h"
#include "outputs/output_processor.h"

#include "audio/samps.h"
#include "audio/audio.h"

#include "Drums.h"

#include <SimplyAtomic.h>
class WorkshopOutputWrapper : public IMIDINoteAndCCTarget {
  //uint leds_map[NUM_LEDS] = { LED5, LED6, LED4, LED3, LED1, LED2 };

  public:
    bool muted = false;
    bool debug = false;
    ComputerCard *sw = nullptr;

    WorkshopOutputWrapper(ComputerCard *sw) {
        this->sw = sw;
    }

    void set_muted(bool muted) {
        this->muted = muted;
    }
    bool is_muted() {
        return this->muted;
    }

    void reset() {
        all_gates_off();
        all_leds_on();
        delay(50);
        all_leds_off();
    }

    void all_leds_on() {
        for (int i = 0 ; i < sw->numLeds ; i++) {
            sw->LedOn(i);
        }
    }
    void all_leds_off() {
        for (int i = 0 ; i < sw->numLeds ; i++) {
            sw->LedOff(i);
        }
    }
    void all_gates_off() {
        for (int i = 0 ; i < sw->numLeds ; i++) {
            gateWrite(i, LOW);
            sw->LedOff(i);
        }
    }

    // output to Pulse1+2 outputs and CV1+2 outputs
    void gateWrite(int output_number, bool value) {
        if (output_number == 0 || output_number == 1) {
            sw->PulseOut(output_number, value);
        } else if (output_number == 2 || output_number == 3) {
            sw->CVOut(output_number-2, value ? 2047 : 0);
        }
    }

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
            case GM_NOTE_HAND_CLAP:
                return 6;
            case GM_NOTE_SIDE_STICK:
                return 7;
            case GM_NOTE_TAMBOURINE:
                return 8;
            case GM_NOTE_LOW_TOM:
                return 9;
            case GM_NOTE_RIDE_BELL:
                return 10;
            case GM_NOTE_CLAVES:
                return 11;
            case GM_NOTE_RIDE_CYMBAL_1:
                return 12;
            case GM_NOTE_VIBRA_SLAP:
                return 13;
            case GM_NOTE_SPLASH_CYMBAL:
                return 14;
            // todo: add more outputs here
            default:
                return 15;            
        }
        return -1;
    }

    int8_t get_voice_number_for_note(uint8_t note) {
        if (!is_valid_note(note))
            return -1;

        for (int i = 0 ; i < NUM_VOICES ; i++) {
            int_fast8_t sample_number = voice[i].sample;
            if (sample_number < 0 || sample_number >= NUM_SAMPLES) {
                if (Serial) Serial.printf("get_voice_number_for_note(%i) invalid sample number %i\n", note, sample_number);
                continue;
            }
            sample_t *cs = &sample[sample_number];
            //if (Serial) Serial.printf("get_voice_number_for_note(%i) checking sample %i ", note, sample_number);
            // matches note %i - , i
            ////if (Serial) Serial.printf("%s (aka %s)", cs->sname, get_note_name_c(note, GM_CHANNEL_DRUMS));
            //if (Serial) Serial.println();
            if (cs->MIDINOTE == note) {
                //if (Serial) Serial.printf("\tget_voice_number_for_note(%i) found voice %i\n", note, i);
                return i;
            }
        }

        return -1;
    }

    virtual void sendNoteOn(uint8_t pitch, uint8_t velocity, uint8_t channel) {
        //Serial.printf("WorkshopOutputTarget::sendNoteOn(%i, %i, %i)\n", pitch, velocity, channel);
        if (this->muted) {
            return;
        }
        #ifdef USE_TINYUSB
            USBMIDI.sendNoteOn(pitch, velocity, channel);
        #endif
        int8_t output_number = get_output_number_for_note(pitch);
        if (debug)
            // this oddly crashes!
            if (Serial) { 
                //Serial.printf("WorkshopOutputTarget::sendNoteOn(%i, %i, %i) to output_number %i\n", pitch, velocity, channel, output_number);
                //Serial.flush();
                ATOMIC() {
                    //Serial.printf("WorkshopOutputTarget::sendNoteOn(%i, %i, %i) aka %s to output_number %i\n", pitch, velocity, channel, get_note_name_c(pitch, channel), output_number);
                    Serial.printf("WorkshopOutputTarget::sendNoteOn(");
                    Serial.printf("%i, ", pitch);
                    Serial.printf("%i, ", velocity);
                    Serial.printf("%i) aka ", channel);
                    Serial.printf("%s ", get_note_name_c(pitch, channel));
                    Serial.printf("to output_number %i\n", output_number);
                }
            }

        if (channel==GM_CHANNEL_DRUMS && output_number>=0) {
            sw->LedOn(output_number % sw->numLeds);

            gateWrite(output_number, HIGH);
        }

        int8_t voice_number = get_voice_number_for_note(pitch);
        if (channel==GM_CHANNEL_DRUMS && voice_number >= 0 && voice_number < NUM_VOICES) {
            if (this->debug) Serial.printf("Playing sample %i aka %s\n", voice_number, sample[voice[voice_number].sample].sname);
            voice[voice_number].sampleindex = 0;
            //sample[voice_number].play_volume = velocity; // set the velocity for the sample
        } else {
            if (Serial) Serial.printf("WorkshopOutputTarget::sendNoteOn(%i, %i, %i) got invalid voice_number %i\n", pitch, velocity, channel, voice_number);
            //Serial.flush();
        }
    }
    virtual void sendNoteOff(uint8_t pitch, uint8_t velocity, uint8_t channel) {
        #ifdef USE_TINYUSB
            USBMIDI.sendNoteOff(pitch, velocity, channel);
        #endif
        int8_t output_number = get_output_number_for_note(pitch);

        if (debug) Serial.printf("WorkshopOutputTarget::sendNoteOff(%i, %i, %i) to output_number %i\n", pitch, velocity, channel, output_number);
        if (channel==GM_CHANNEL_DRUMS && output_number>=0) {
            sw->LedOff(output_number % sw->numLeds);

            gateWrite(output_number, LOW);
        }

        // don't bother stopping sample playing for now
    }

};