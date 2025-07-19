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

#include "serial_debug.h"

#include <SimplyAtomic.h>

WorkshopOutputWrapper output_wrapper(&sw);

volatile std::atomic<bool> started = false;
volatile std::atomic<bool> ticked = false;

void do_tick(uint32_t ticks);

#ifdef USE_TINYUSB
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
#endif

// call this when global clock should be reset
void global_on_restart() {
  set_restart_on_next_bar(false);

  //Serial.println(F("on_restart()==>"));
  clock_reset();
  last_processed_tick = -1;
  //Serial.println(F("<==on_restart()"));
}

#ifdef ENABLE_PARAMETERS
  repeating_timer_t parameter_timer;
  bool parameter_repeating_callback(repeating_timer_t *rt) {
    if (started) {
      parameter_manager->throttled_update_cv_input__all(5, false, false);
    }
    return true;
  }
#endif

#ifdef USE_TINYUSB
  repeating_timer_t usb_timer;
  bool usb_repeating_callback(repeating_timer_t *rt) {
    USBMIDI.read();
    return true;
  }
#endif

#ifdef ENABLE_SHUFFLE
  void shuffled_step_callback(uint32_t step) {
    sequencer->on_step_shuffled(0, step);
  }
#endif

void setup() {

  set_sys_clock_khz(150000, true);

  #ifdef USE_TINYUSB
    setup_serial();
    setup_usb();
    setup_midi();
  #endif

  //if (Serial) { Serial.println(F("done setup_serial; now gonna SetupComputerIO()")); Serial.flush(); }
  //SetupComputerIO();
  //if (Serial) { Serial.println(F("done SetupComputerIO; now gonna setup_uclock()")); Serial.flush(); }

  output_wrapper.reset();

  setup_samples();

  setup_uclock(do_tick, uClock.PPQN_24);
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
    clock_start();
    //if (Serial) Serial.println("Started uClock!");
  #endif
  started = true;

  // set up repeating timers to process tasks
  #ifdef ENABLE_PARAMETERS
    add_repeating_timer_ms(5, parameter_repeating_callback, nullptr, &parameter_timer);
  #endif
  #ifdef USE_TINYUSB
    add_repeating_timer_us(200, usb_repeating_callback, nullptr, &usb_timer);
  #endif
        
  if (Serial) { Serial.println(F("setup() done - starting!")); }

}

void __not_in_flash_func(do_tick)(uint32_t in_ticks) {
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



void __not_in_flash_func(loop)() {

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

    #ifdef USE_TINYUSB
      if (ticked && debug_enable_output_parameter_input) 
        parameter_manager->output_parameter_representation();
    #endif

    //if (ticked) sequencer->output_trigger_representation();
  }

  #ifdef USE_TINYUSB
    tud_task();
    //if (ticked) 
    //ATOMIC_BLOCK(SA_ATOMIC_RESTORESTATE) 
    {
      process_serial_input();
    }
  #endif

  // flash LEDs on the beat if muted
  if (ticked && output_wrapper.is_muted() && is_bpm_on_beat(ticks)) {
    output_wrapper.all_leds_on();
  } else if (ticked && output_wrapper.is_muted() && is_bpm_on_beat(ticks,6)) {
    output_wrapper.all_leds_off();
  }

  if (sw.calculate_mode==IN_MAIN_LOOP) {
    static uint32_t last_sample_at_us = 0;
    sw.CalculateSamples();
    last_sample_at_us = micros();
  }

}
