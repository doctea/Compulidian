#include "Config.h"

#include <Arduino.h>

#include "sequencer/sequencing.h"
#include "outputs/output_processor.h"

#include "workshop_output.h"
#include "audio/audio.h"

#include <clock.h>
#include <bpm.h>

#ifdef ENABLE_PARAMETERS
  #include "ParameterManager.h"
#endif

#ifdef ENABLE_CV_INPUT
    #include "cv_input.h"
    #ifdef ENABLE_CLOCK_INPUT_CV
        #include "cv_input_clock.h"
    #endif
#endif

#ifdef ENABLE_SHUFFLE
  #include "sequencer/shuffle.h"
#endif

#include <SimplyAtomic.h>

WorkshopOutputWrapper output_wrapper(&sw);

volatile std::atomic<bool> started = false;
volatile std::atomic<bool> ticked = false;

bool debug_enable_output_parameter_input = false;

void do_tick(uint32_t ticks);

// serial console to host, for debug etc
void setup_serial() {
  Serial.begin(115200);
  Serial.setTimeout(0);

  #ifdef WAIT_FOR_SERIAL
      while(!Serial) {};
      //while (1)
        //Serial.println(F("Serial started!"));
      delay(500);
  #endif
}

// call this when global clock should be reset
void global_on_restart() {
  set_restart_on_next_bar(false);

  //Serial.println(F("on_restart()==>"));
  clock_reset();
  last_processed_tick = -1;
  //Serial.println(F("<==on_restart()"));
}

repeating_timer_t parameter_timer;
bool parameter_repeating_callback(repeating_timer_t *rt) {
  #ifdef ENABLE_PARAMETERS
    if (started) {
      parameter_manager->throttled_update_cv_input__all(5, false, false);
    }
  #endif
  return true;
}

repeating_timer_t usb_timer;
bool usb_repeating_callback(repeating_timer_t *rt) {
  #ifdef USE_TINYUSB
    USBMIDI.read();
  #endif
  return true;
}

#ifdef ENABLE_SHUFFLE
  void shuffled_step_callback(uint32_t step) {
    sequencer->on_step_shuffled(0, step);
  }
#endif

void setup() {

  set_sys_clock_khz(220000, true);

  setup_serial();

  #ifdef USE_TINYUSB
    if (Serial) { Serial.println("setup_usb!"); Serial.flush(); }
    setup_usb();
    setup_midi();
  #endif

  //if (Serial) { Serial.println(F("done setup_serial; now gonna SetupComputerIO()")); Serial.flush(); }
  //SetupComputerIO();
  //if (Serial) { Serial.println(F("done SetupComputerIO; now gonna setup_uclock()")); Serial.flush(); }

  output_wrapper.reset();

  setup_samples();

  setup_uclock(do_tick, uClock.PPQN_96);
  set_global_restart_callback(global_on_restart);

  #ifdef ENABLE_SHUFFLE
    // set up shuffle pattern
    int8_t shuffle_75[] = {0, 12, 0, 12, 0, 12, 0, 12};
    shuffle_pattern_wrapper[0]->set_amount(0.5);
    shuffle_pattern_wrapper[0]->set_steps(shuffle_75, sizeof(shuffle_75));
    shuffle_pattern_wrapper[0]->set_active(true);
    uClock.setOnStep(shuffled_step_callback);
  #endif

  if (Serial) { Serial.println(F("done setup_uclock()")); }
  
  #ifdef ENABLE_EUCLIDIAN
    //Serial.println("setting up sequencer..");
    output_processor = new ChosenDrumKitMIDIOutputProcessor(&output_wrapper);
    setup_sequencer();
    output_processor->configure_sequencer(sequencer);
    sequencer->initialise_patterns();
    sequencer->reset_patterns();
  #endif

  #ifdef ENABLE_PARAMETERS
    #ifdef ENABLE_CV_INPUT
      if (Serial) { Serial.println(F("setting up cv input..")); Serial.flush(); }
      setup_cv_input();
      if (Serial) { Serial.println(F("..done setup_cv_input")); Serial.flush(); }
      
      if (Serial) { Serial.println(F("setting up parameter inputs..")); Serial.flush(); }
      setup_parameter_inputs();
      if (Serial) { Serial.println(F("..done setup_parameter_inputs")); Serial.flush(); }
      Debug_printf("after setup_parameter_inputs(), free RAM is\t%u\n", freeRam());
    #endif
    /*#ifdef ENABLE_CV_INPUT  // these are midi outputs!
        setup_parameter_outputs(output_wrapper);
        Debug_printf("after setup_parameter_outputs(), free RAM is\t%u\n", freeRam());
    #endif*/
    setup_output_processor_parameters();
    Debug_printf("after setup_output_processor_parameters(), free RAM is\t%u\n", freeRam());
  #endif

  #if defined(ENABLE_PARAMETERS)
    //Serial.println("..calling sequencer.getParameters()..");
    LinkedList<FloatParameter*> *params = sequencer->getParameters();
    Debug_printf("after setting up sequencer parameters, free RAM is %u\n", freeRam());
  #endif

  #ifdef ENABLE_PARAMETERS
    //parameter_manager->setDefaultParameterConnections();
    // ^^ don't do this here, as it will overwrite the connections made in setup_parameter_inputs()
  #endif

  #ifdef USE_UCLOCK
    //if (Serial) Serial.println("Starting uClock...");
    //Serial_flush();
    clock_reset();
    clock_start();
    //if (Serial) Serial.println("Started uClock!");
    //Serial_flush();
  #endif
  started = true;

  add_repeating_timer_ms(5, parameter_repeating_callback, nullptr, &parameter_timer);
  add_repeating_timer_us(200, usb_repeating_callback, nullptr, &usb_timer);
        
  if (Serial) { Serial.println(F("setup() done - starting!")); }

}

void do_tick(uint32_t in_ticks) {
  #ifdef USE_UCLOCK
      ::ticks = in_ticks;
      // todo: hmm non-USE_UCLOCK mode doesn't actually use the in_ticks passed in here..?
  #endif
  //if (Serial) Serial.printf("ticked %u\n", ticks);
  if (is_restart_on_next_bar() && is_bpm_on_bar(ticks)) {
      //if (debug) Serial.println(F("do_tick(): about to global_on_restart"));
      global_on_restart();

      set_restart_on_next_bar(false);
  }

  //output_wrapper->sendClock();

  #ifdef ENABLE_EUCLIDIAN
      if (sequencer->is_running()) sequencer->on_tick(ticks);
      if (is_bpm_on_sixteenth(ticks) && output_processor->is_enabled()) {
          output_processor->process();
      }
  #endif
}

char serial_input_buffer[100];
char serial_input_buffer_index = 0;
void process_serial_input() {
  //Serial.println(F("process_serial_input()"));
  while (Serial.available()) {
    char c = Serial.read();
    //if(c=='\r') continue; // ignore carriage return

    if (serial_input_buffer_index >= sizeof(serial_input_buffer)-1) {
      if (Serial) { Serial.printf("serial_input_buffer overflow, resetting\n"); }
      serial_input_buffer_index = 0;
    }

    // handle backspace
    if (c=='\b') {
      Serial.printf("backspace?");
      if (serial_input_buffer_index > 0) {
        serial_input_buffer_index--;
        Serial.printf("\b");
      }
    } else if (c=='\t') {
      // recall previous command
      serial_input_buffer_index = strlen(serial_input_buffer);
      Serial.printf("\n%s", serial_input_buffer);
    } else if (c=='\r' || c=='\n' || c=='#') {
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
      } else if (serial_input_buffer[0]=='p') {
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
      } else if (serial_input_buffer[0]=='f') {
        // toggle fills_enabled
        if (Serial) Serial.printf("f command received - fills_enabled was %s!\n", sequencer->is_fills_enabled() ? "true" : "false");
        sequencer->set_fills_enabled(!sequencer->is_fills_enabled());
        if (Serial) Serial.printf("f command received - fills_enabled is now %s!\n", sequencer->is_fills_enabled() ? "true" : "false");
      } else if (serial_input_buffer[0]=='I') {
        Serial.println(F("I command received!"));
        debug_enable_output_parameter_input = !debug_enable_output_parameter_input;
      } else if (serial_input_buffer[0]=='d' || serial_input_buffer[0]=='D') {
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
      } else if (serial_input_buffer[0]=='s') {
        sw.interpolate_enabled = !sw.interpolate_enabled;
        Serial.printf("Interpolation %s\n", sw.interpolate_enabled ? "enabled" : "disabled");
      } else if (serial_input_buffer[0]=='c') {
        Serial.printf("calculate_mode was %s\n", sw.calculate_mode == IN_MAIN_LOOP ? "IN_MAIN_LOOP" : "IN_PROCESS_SAMPLE");
        sw.calculate_mode = sw.calculate_mode == IN_MAIN_LOOP ? IN_PROCESS_SAMPLE : IN_MAIN_LOOP;
        Serial.printf("calculate_mode now %s\n", sw.calculate_mode == IN_MAIN_LOOP ? "IN_MAIN_LOOP" : "IN_PROCESS_SAMPLE");
      } else if (serial_input_buffer[0]=='v') {
        Serial.printf("volume was %s\n", sw.enable_volume ? "enabled" : "disabled");
        sw.enable_volume = !sw.enable_volume;
        Serial.printf("volume now %s\n", sw.enable_volume ? "enabled" : "disabled");
      }
      serial_input_buffer_index = 0;
    } else {
      serial_input_buffer[serial_input_buffer_index++] = c;
      Serial.printf("%c", c);  
    }
  }
}

void loop() {

  /*#ifdef USE_TINYUSB
    ATOMIC() {
      USBMIDI.read();
    }
  #endif*/

  ATOMIC_BLOCK(SA_ATOMIC_RESTORESTATE) 
  {
      ticked = update_clock_ticks();
  }

  ATOMIC_BLOCK(SA_ATOMIC_RESTORESTATE) 
  {
    if (output_processor->is_enabled()) {
        output_processor->loop();
    }
  }

  if (cv_input_enabled) {
    /*if (ticked && parameter_manager->ready_for_next_update()) {
        parameter_manager->throttled_update_cv_input__all(5, false, false);
    }*/

    if (ticked && debug_enable_output_parameter_input) 
      parameter_manager->output_parameter_representation();

    //if (ticked) sequencer->output_trigger_representation();
  }

  //if (ticked) 
  //ATOMIC_BLOCK(SA_ATOMIC_RESTORESTATE) 
  {
    process_serial_input();
  }

  // flash LEDs on the beat if muted
  if (ticked && output_wrapper.is_muted() && is_bpm_on_beat(ticks)) {
    output_wrapper.all_leds_on();
  } else if (ticked && output_wrapper.is_muted() && is_bpm_on_beat(ticks,6)) {
    output_wrapper.all_leds_off();
  }

  if (sw.calculate_mode==IN_MAIN_LOOP) {
    static uint32_t last_sample_at_us = 0;
    if (micros()-last_sample_at_us >= 10) {
      sw.CalculateSamples();
      last_sample_at_us = micros();
    }
  }

}
