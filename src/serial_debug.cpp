
#include "serial_debug.h"

#ifdef USE_TINYUSB

  bool debug_enable_output_parameter_input = false;

  char serial_input_buffer[256];
  char serial_input_buffer_index = 0;
  void __not_in_flash_func(process_serial_input)() {
    //Serial.println(F("process_serial_input()"));
    while (Serial.available()) {
      char c = Serial.read();
      //if(c=='\r') continue; // ignore carriage return

      if (serial_input_buffer_index >= sizeof(serial_input_buffer)-1) {
        if (Serial) { Serial.printf("serial_input_buffer overflow, resetting\n"); }
        serial_input_buffer_index = 0;
      }

      if (c=='\b') {          // handle backspace -- does't actually seem to work?
        Serial.printf("backspace?");
        if (serial_input_buffer_index > 0) {
          serial_input_buffer_index--;
          Serial.printf("\b");
        }
      } else if (c=='\t') {   // handle tab: recall previous command
        serial_input_buffer_index = strlen(serial_input_buffer);
        Serial.printf("\n%s", serial_input_buffer);
      } else if (c=='\r' || c=='\n' || c=='#') {  // handle enter of line or # as substitute
        serial_input_buffer[serial_input_buffer_index] = 0;
        Serial.printf("\ngot '%s'\n", serial_input_buffer);

        if (serial_input_buffer[0]=='l') {
          // list the track names
          Serial.println("l command received!"); Serial.flush();
          for (int i = 0 ; i < sequencer->number_patterns ; i++) {
            //Serial.printf("Pattern %i/%i\n", i+1, sequencer->number_patterns); Serial.flush();
            SimplePattern *p = (SimplePattern *)sequencer->get_pattern(i);
            Serial.printf("Pattern [%i/%i]:\t%s\n", i+1, sequencer->number_patterns, p->get_output_label()); Serial.flush();
          }
          for (int i = 0 ; i < NUM_VOICES ; i++) {
            Serial.printf("Voice [%i/%i]:\t%s on %i (%s)\n", i+1, NUM_VOICES, sample[voice[i].sample].sname, sample[voice[i].sample].MIDINOTE, get_note_name_c(sample[voice[i].sample].MIDINOTE, GM_CHANNEL_DRUMS));
            Serial.flush();
          }
        } else if (serial_input_buffer[0]=='p') {  // trigger named pattern
          Serial.println(F("p command received!"));
          for (int i = 0 ; i < sequencer->number_patterns ; i++) {
            SimplePattern *p = (SimplePattern *)sequencer->get_pattern(i);
            if (p->get_output()->matches_label(serial_input_buffer+2)) {
              Serial.printf("Triggering pattern %i: %s\n", i, p->get_output_label());
              p->trigger_on_for_step(BPM_CURRENT_STEP_OF_PHRASE);
            } /*else {
              Serial.printf("pattern %i: null\n", i);
            }*/
          } 
        } else if (serial_input_buffer[0]=='f') {   // toggle fills_enabled
          if (Serial) Serial.printf("f command received - fills_enabled was %s!\n", sequencer->is_fills_enabled() ? "true" : "false");
          sequencer->set_fills_enabled(!sequencer->is_fills_enabled());
          if (Serial) Serial.printf("f command received - fills_enabled is now %s!\n", sequencer->is_fills_enabled() ? "true" : "false");
        } else if (serial_input_buffer[0]=='I') {   // toggle debug on/off for output parameter inputs
          Serial.println(F("I command received!"));
          debug_enable_output_parameter_input = !debug_enable_output_parameter_input;
        } else if (serial_input_buffer[0]=='d' || serial_input_buffer[0]=='D') {  // toggle debug on/off for parameters or inputs
          Serial.println(F("d command received!"));
          if (serial_input_buffer[1]=='p') {
            // set debug status on a Parameter
            Serial.println(F("d p command received!"));
            if(strlen(serial_input_buffer)>2) {
              // get the index of the parameter from 2nd byte
              int index = atoi(&serial_input_buffer[2]);
              if (index>=0 && index<parameter_manager->available_parameters->size()) {
                FloatParameter *p = parameter_manager->available_parameters->get(index);
                p->debug = serial_input_buffer[0]=='D' ? true : false;
                Serial.printf("parameter %i: %s set to debug=%s\n", index, p->label, serial_input_buffer[0]=='D' ? "true" : "false");
              } else {
                Serial.printf("invalid parameter index %i\n", index);
              }
            } else {
              Serial.println(F("d i command needs a parameter index!"));
            }
          } else {
            // set debug status on a ParameterInput
            Serial.println(F("d i command received!"));
            if(strlen(serial_input_buffer)>2) {
              // get the index of the parameter from 2nd byte
              int index = atoi(&serial_input_buffer[2]);
              if (index>=0 && index<parameter_manager->available_inputs->size()) {
                BaseParameterInput *p = parameter_manager->available_inputs->get(index);
                p->debug = serial_input_buffer[0]=='D' ? true : false;
                Serial.printf("input %i: %s set to debug=%s\n", index, p->name, serial_input_buffer[0]=='D' ? "true" : "false");
              } else {
                Serial.printf("invalid input index %i\n", index);
              }
            } else {
              Serial.println(F("d i command needs an input index!"));
            }
          }
          serial_input_buffer_index = 0;
          Serial.println("Finished parsing serial input");
          break;
        } else if (serial_input_buffer[0]=='s') {   // toggle sample interpolation on/off
          sw.interpolate_enabled = !sw.interpolate_enabled;
          Serial.printf("Interpolation %s\n", sw.interpolate_enabled ? "enabled" : "disabled");
        } else if (serial_input_buffer[0]=='c') {  // toggle calculate mode between main loop (core0) and audio worker (core1)
          Serial.printf("calculate_mode was %s\n", sw.calculate_mode == IN_MAIN_LOOP ? "IN_MAIN_LOOP" : "IN_PROCESS_SAMPLE");
          sw.calculate_mode = sw.calculate_mode == IN_MAIN_LOOP ? IN_AUDIO_WORKER: IN_MAIN_LOOP;
          Serial.printf("calculate_mode now %s\n", sw.calculate_mode == IN_MAIN_LOOP ? "IN_MAIN_LOOP" : "IN_PROCESS_SAMPLE");
        } else if (serial_input_buffer[0]=='v') {  // toggle volume on/off
          Serial.printf("volume was %s\n", sw.enable_volume ? "enabled" : "disabled");
          sw.enable_volume = !sw.enable_volume;
          Serial.printf("volume now %s\n", sw.enable_volume ? "enabled" : "disabled");
        } else if (serial_input_buffer[0]=='V') {
          // dump version/build information
            Serial.printf("COMMIT_INFO: %s\n", COMMIT_INFO);  
            Serial.printf("ENV_NAME: %s\n", ENV_NAME);
            Serial.printf("Built at " __TIME__ " on " __DATE__ "\n");
        }
        serial_input_buffer_index = 0;
      } else {
        serial_input_buffer[serial_input_buffer_index++] = c;
      }
    }
  }
#endif