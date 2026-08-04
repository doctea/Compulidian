#include "Config.h"

#include <Arduino.h>

#include "sequencer/Euclidian.h"

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
    // #ifdef ENABLE_CLOCK_INPUT_CV
    //     #include "cv_input_clock.h"
    // #endif
#endif

#ifdef ENABLE_SHUFFLE
  #include "sequencer/shuffle.h"
#endif

#ifdef ENABLE_ACCENTS
  #include "accent/StepAccentSource.h"
#endif

#include "serial_debug.h"
#include "audio/bank_manager.h"

#include <string.h>
#include <SimplyAtomic.h>
#include <RP2040.h>

#include "configurator.h"

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
    while(USBMIDI.read()) {}
    return true;
  }
#endif

#ifdef ENABLE_SHUFFLE
  void shuffled_step_callback(uint32_t step) {
    sequencer->on_step_shuffled(0, step);
  }
#endif

#ifdef ENABLE_CLOCK_INPUT_CV
  extern volatile bool cv_clock_ticked;
  extern volatile bool cv_clock_reset;
  bool check_cv_clock_ticked() {
    bool was_ticked = cv_clock_ticked;
    cv_clock_ticked = false;
    return was_ticked;
  }
#endif

void setup_usb();
void setup_midi();

// respond to incoming USB MIDI note on/off messages by sending them to the output wrapper
void pc_usb_midi_handle_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
  output_wrapper.sendNoteOn(note, velocity, channel);
}
void pc_usb_midi_handle_note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
  output_wrapper.sendNoteOff(note, velocity, channel);
}

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
  current_settings = settings_load(); // load after bankManager.setup() inside setup_samples()

  // Register SysEx handler for web config tool
  USBMIDI.setHandleSystemExclusive(handle_sysex);

  // Register callbacks for incoming USB MIDI note messages
  USBMIDI.setHandleNoteOn(pc_usb_midi_handle_note_on);
  USBMIDI.setHandleNoteOff(pc_usb_midi_handle_note_off);

  setup_uclock(do_tick);
  set_global_restart_callback(global_on_restart);

  #ifdef ENABLE_SHUFFLE
    // set up shuffle pattern
    int8_t shuffle_75[] = {0, 12, 0, 12, 0, 12, 0, 12}; //, 12, 0, 12, 0, 12, 0, 12};
    uClock.setOnStep(shuffled_step_callback);
    shuffle_pattern_wrapper[0]->set_steps(shuffle_75, sizeof(shuffle_75));
  #endif

  if (Serial) { Serial.println(F("done setup_uclock()")); }
  
  #ifdef ENABLE_EUCLIDIAN
    //Serial.println("setting up sequencer..");
    output_processor = new ChosenDrumKitMIDIOutputProcessor(&output_wrapper);
    sequencer = new EuclidianSequencer(output_processor->nodes, output_processor->nodes->size());
    sequencer->debug = true;
    output_processor->configure_sequencer(sequencer);
    ((EuclidianSequencer*)sequencer)->initialise_patterns();
    ((EuclidianSequencer*)sequencer)->reset_patterns();
  #endif

  #ifdef ENABLE_PARAMETERS
    #ifdef ENABLE_CV_INPUT
      if (Serial) { Serial.println(F("setting up cv input..")); Serial.flush(); }
      setup_cv_input();
      if (Serial) { Serial.println(F("..done setup_cv_input")); Serial.flush(); }

      // Must call getParameters() before setup_parameter_inputs(), so that
      // getParameterByName() calls inside setup_parameter_inputs() can find them.
      sequencer->getParameters();
      
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
    ParameterList *params = sequencer->getParameters();
    Debug_printf("after setting up sequencer parameters, free RAM is %u\n", freeRam());
  #endif

  #ifdef ENABLE_PARAMETERS
    //parameter_manager->setDefaultParameterConnections();
    // ^^ don't do this here, as it will overwrite the connections made in setup_parameter_inputs()
  #endif

  #ifdef ENABLE_ACCENTS
    global_accent_source = new StepAccentSource(16);
    const uint8_t strong_steps[] = {0, 4, 8, 12};
    ((StepAccentSource*)global_accent_source)->set_pattern(strong_steps, 4);
  #endif

  // set up repeating timers to process tasks
  #ifdef ENABLE_PARAMETERS
    add_repeating_timer_ms(5, parameter_repeating_callback, nullptr, &parameter_timer);
  #endif
  #ifdef USE_TINYUSB
    add_repeating_timer_us(250, usb_repeating_callback, nullptr, &usb_timer);
  #endif

  #ifdef ENABLE_CLOCK_INPUT_CV
    set_check_cv_clock_ticked_callback(check_cv_clock_ticked);
  #endif

  #ifdef USE_UCLOCK
    //if (Serial) Serial.println("Starting uClock...");
    clock_start();
    //if (Serial) Serial.println("Started uClock!");
  #endif
  started = true;

  if (Serial) { Serial.println(F("setup() done - starting!")); }
  if (Serial) {
    Serial.printf("Banks: %d valid  (active=%d)\n",
        bankManager.num_valid_banks(), bankManager.active_bank());
    for (int s = 0; s <= bankManager.MAX_FLASH_BANKS; ++s) {
        if (bankManager.is_bank_valid(s)) {
            if (s == 0) {
                Serial.printf("  Bank 0: compiled-in\n");
            } else {
                const BankHeader *bh = bankManager.get_bank_header(s);
                if (bh) Serial.printf("  Bank %d: '%s' (%u samples)\n",
                    s, bh->bank_name, bh->num_samples);
            }
        }
    }
    Serial.printf("Flash layout: banks start at 0x%08X, slot size 0x%X\n",
        FLASH_BANKS_ADDR, FLASH_BANK_SIZE);
  }
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

  parameter_manager->tick_sh();

  #ifdef ENABLE_EUCLIDIAN
      if (sequencer->is_running()) sequencer->on_tick(ticks);
      if (is_bpm_on_sixteenth(ticks) && output_processor->is_enabled()) {
          output_processor->process();
      }
  #endif
}



void __not_in_flash_func(loop)() {
  // Print a status summary whenever a serial terminal (re-)connects.
  // boot-time prints happen before USB CDC is ready, so this is the only
  // reliable way to see them after opening the monitor post-boot.
  static bool serial_prev = false;
  bool serial_now = (bool)Serial;
  if (serial_now && !serial_prev) {
      Serial.printf("\n=== Compulidian connected (uptime %ums) ===\n", millis());
      Serial.printf("Flash: XIP_BASE=0x%08X  BANKS_OFFSET=0x%08X  BANK_SIZE=0x%08X  MAX_BANKS=%d\n",
          XIP_BASE, FLASH_BANKS_OFFSET, FLASH_BANK_SIZE, FLASH_MAX_BANKS);
      Serial.printf("Banks: active=%d  valid_count=%d\n",
          bankManager.active_bank(), bankManager.num_valid_banks());
      for (int slot = 1; slot <= FLASH_MAX_BANKS; ++slot) {
          uint32_t addr = flash_bank_xip_addr(slot);
          const BankHeader *hdr = reinterpret_cast<const BankHeader *>(addr);
          Serial.printf("  slot %d @ 0x%08X: %s  magic=0x%08X ver=%u n=%u name='%.*s'\n",
              slot, addr,
              bankManager.is_bank_valid(slot) ? "VALID  " : "empty  ",
              hdr->magic, hdr->version, hdr->num_samples, 31, hdr->bank_name);
      }
      Settings s = settings_load();
      Serial.printf("Settings: active_bank=%u  magic=0x%08X\n", s.active_bank, s.magic);
      Serial.printf("Type '?' for command help, 'b' to re-print bank status.\n");
      Serial.flush();
  }
  serial_prev = serial_now;

  #ifdef USE_TINYUSB
    // Service USB + pending SysEx responses even if config mode is active.
    tud_task();
    process_pending_sysex_responses();
  #endif

  // if (handle_config_mode()) return;

  // todo: will need this if/when we convert the WorkshopOutputWrapper to use the ring buffer that Microlidian now uses
  //output_wrapper->drain();

  ATOMIC_BLOCK(SA_ATOMIC_RESTORESTATE) 
  {
      ticked = update_clock_ticks();
  }

  if (ticked) {
    if (is_bpm_on_sixteenth(ticks)) {
      sw.AudioOut2(32767);
    } else if (is_bpm_on_thirtysecond(ticks)) {
      sw.AudioOut2(0);
    }
  }

  sequencer->on_loop(ticks);

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
    //if (ticked) 
    //ATOMIC_BLOCK(SA_ATOMIC_RESTORESTATE) 
    {
      process_serial_input();
    }

    /*if (ticked && ticks % 6 == 0) {
      Serial.printf("uClock reckons BPM is %3.3f, ticks is %u, playing is %s\n", uClock.getTempo(), ticks, playing ? "true" : "false");
    }*/
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
