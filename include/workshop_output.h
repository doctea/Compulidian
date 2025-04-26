#pragma once

#include "computer.h"
#include "outputs/output_processor.h"

#include "audio/samps.h"
#include "audio/audio.h"

#include "Drums.h"
class WorkshopOutputWrapper : public IMIDINoteAndCCTarget  {

  uint leds_map[NUM_LEDS] = { LED5, LED6, LED4, LED3, LED1, LED2 };

  public:
    bool muted = false;
    bool debug = false;

    WorkshopOutputWrapper() {}

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
        for (int i = 0 ; i < NUM_LEDS ; i++) {
            digitalWrite(leds_map[i], HIGH);
        }
    }
    void all_leds_off() {
        for (int i = 0 ; i < NUM_LEDS ; i++) {
            digitalWrite(leds_map[i], LOW);
        }
    }
    void all_gates_off() {
        for (int i = 0 ; i < NUM_LEDS ; i++) {
            gateWrite(i, LOW);
        }
    }

    // output to Pulse1+2 outputs and CV1+2 outputs
    void gateWrite(int output_number, bool value) {
        if (output_number == 0 || output_number == 1) {
            digitalWrite(PULSE_1_RAW_OUT+output_number, value ? HIGH : LOW);
        } else if (output_number == 2 || output_number == 3) {
            pwm_set_gpio_level(CV_OUT_2+(output_number-2), value ? 4096 : 0);
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
            digitalWrite(leds_map[output_number % NUM_LEDS], HIGH);

            gateWrite(output_number, HIGH);
        }

        int8_t voice_number = get_voice_number_for_note(pitch);
        if (channel==GM_CHANNEL_DRUMS && voice_number >= 0 && voice_number < NUM_VOICES) {
            Serial.printf("Playing sample %i aka %s\n", voice_number, sample[voice[voice_number].sample].sname);
            voice[voice_number].sampleindex = 0;
            //sample[voice_number].play_volume = velocity; // set the velocity for the sample
        }
    }
    virtual void sendNoteOff(uint8_t pitch, uint8_t velocity, uint8_t channel) {
        #ifdef USE_TINYUSB
            USBMIDI.sendNoteOff(pitch, velocity, channel);
        #endif
        int8_t output_number = get_output_number_for_note(pitch);

        if (debug) Serial.printf("WorkshopOutputTarget::sendNoteOff(%i, %i, %i) to output_number %i\n", pitch, velocity, channel, output_number);
        if (channel==GM_CHANNEL_DRUMS && output_number>=0) {
            digitalWrite(leds_map[output_number % NUM_LEDS], LOW);

            gateWrite(output_number, LOW);
        }

        // don't bother stopping sample playing for now
    }

};