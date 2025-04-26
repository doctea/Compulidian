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

    //#include "outputs/output_voice.h"
#endif

//#define WAIT_FOR_SERIAL

#include "computer.h"

WorkshopOutputWrapper output_wrapper;

std::atomic<bool> started = false;
std::atomic<bool> ticked = false;

void do_tick(uint32_t ticks);

// serial console to host, for debug etc
void setup_serial() {
  Serial.begin(115200);
  Serial.setTimeout(0);

  #ifdef WAIT_FOR_SERIAL
      while(!Serial) {};
      //while (1)
        //Serial.println(F("Serial started!"));
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


void setup() {
  setup_serial();

  #ifdef USE_TINYUSB
    Serial.println("setup_usb!"); Serial.flush();
    setup_usb();
    setup_midi();
  #endif

  Serial.println(F("done setup_serial; now gonna SetupComputerIO()")); Serial.flush();
  SetupComputerIO();
  Serial.println(F("done SetupComputerIO; now gonna setup_uclock()")); Serial.flush();

  setup_samples();

  setup_uclock(do_tick, uClock.PPQN_24);
  set_global_restart_callback(global_on_restart);

  //set_bpm(60);

  Serial.println(F("done setup_uclock()"));
  
  #ifdef ENABLE_EUCLIDIAN
    //Serial.println("setting up sequencer..");
    setup_output(&output_wrapper);
    setup_sequencer();
    output_processor->configure_sequencer(sequencer);
  #endif

  #ifdef ENABLE_PARAMETERS
    #ifdef ENABLE_CV_INPUT
      Serial.println(F("setting up cv input..")); Serial.flush();
      setup_cv_input();
      Serial.println(F("..done setup_cv_input")); Serial.flush();
      
      Serial.println(F("setting up parameter inputs..")); Serial.flush();
      setup_parameter_inputs();
      Serial.println(F("..done setup_parameter_inputs")); Serial.flush();
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

  Serial.println(F("setup() done - starting!"));

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

void loop() {

  #ifdef USE_TINYUSB
    ATOMIC() {
      USBMIDI.read();
    }
  #endif

  ATOMIC() 
  {
      ticked = update_clock_ticks();
  }

  ATOMIC() 
  {
      /*if (playing && clock_mode==CLOCK_INTERNAL && last_ticked_at_micros>0 && micros() + loop_average >= last_ticked_at_micros + micros_per_tick) {
          // don't process anything else this loop, since we probably don't have time before the next tick arrives
          //Serial.printf("early return because %i + %i >= %i + %i\n", micros(), loop_average, last_ticked_at_micros, micros_per_tick);
          //Serial.flush();
      } else {*/
          if (output_processor->is_enabled()) {
              output_processor->loop();
          }
      /*
          #ifdef ENABLE_SCREEN
          if (!is_locked()) {
              //acquire_lock();
              menu->update_inputs();
              //release_lock();
          }
          #endif

          add_loop_length(micros()-mics_start);
      }
      */
  }

  if (cv_input_enabled) {
    if (parameter_manager->ready_for_next_update()) {
        //parameter_manager->process_calibration();

        parameter_manager->throttled_update_cv_input__all(5, false, false);
    }
  }

  if (ticked)
    parameter_manager->output_parameter_representation();

}
