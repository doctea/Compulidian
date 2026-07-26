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

#include "serial_debug.h"
#include "settings.h"
#include "audio/bank_manager.h"

#include <SimplyAtomic.h>
#include <RP2040.h>

// ---------------------------------------------------------------------------
// SysEx protocol  (matches tools/web/compulidian_manager.html)
// F0 7D 43 <cmd> [data...] F7
// ---------------------------------------------------------------------------
#define SYSEX_MFR_ID     0x7D
#define SYSEX_DEVICE_ID  0x43   // 'C'
#define SYSEX_CMD_GET    0x10   // host → device: request config
#define SYSEX_CMD_DATA   0x11   // bidirectional: config payload (19 bytes)
#define SYSEX_CMD_REBOOT 0x20   // host → device: reboot to bootloader

// Data layout for SYSEX_CMD_DATA (indices into the SysEx data array,
// i.e. after the F0, MFR_ID, DEVICE_ID, CMD bytes have been consumed):
//  [0]  active_bank
//  [1]  num_valid_banks (read-only, sent by device, ignored on receive)
//  [2..4]  outputs[0]: type, channel, note
//  [5..7]  outputs[1]: type, channel, note
//  [8..10] outputs[2]: type, channel, note
// [11..13] outputs[3]: type, channel, note
// [14]  cv_input_role[0]
// [15]  cv_input_role[1]
// [16]  knob_role[0]
// [17]  knob_role[1]
// [18]  knob_role[2]
#define SYSEX_DATA_LEN 19

static Settings current_settings;

static void send_config_sysex() {
    uint8_t buf[3 + SYSEX_DATA_LEN];
    buf[0] = SYSEX_MFR_ID;
    buf[1] = SYSEX_DEVICE_ID;
    buf[2] = SYSEX_CMD_DATA;
    buf[3] = current_settings.active_bank;
    buf[4] = (uint8_t)bankManager.num_valid_banks();
    for (int i = 0; i < 4; i++) {
        buf[5 + i * 3]     = (uint8_t)current_settings.outputs[i].type;
        buf[5 + i * 3 + 1] = current_settings.outputs[i].channel;
        buf[5 + i * 3 + 2] = current_settings.outputs[i].note;
    }
    buf[17] = (uint8_t)current_settings.cv_input_role[0];
    buf[18] = (uint8_t)current_settings.cv_input_role[1];
    buf[19] = (uint8_t)current_settings.knob_role[0];
    buf[20] = (uint8_t)current_settings.knob_role[1];
    buf[21] = (uint8_t)current_settings.knob_role[2];
    USBMIDI.sendSysEx(sizeof(buf), buf, false);
}

static void handle_sysex(uint8_t *data, unsigned int size) {
    // The MIDI library includes 0xF0 at data[0] and 0xF7 at data[size-1].
    // Layout: [0xF0, MFR_ID, DEVICE_ID, CMD, ...payload..., 0xF7]
    if (size < 5) return;
    if (data[0] != 0xF0) return;
    if (data[1] != SYSEX_MFR_ID || data[2] != SYSEX_DEVICE_ID) return;

    const uint8_t cmd = data[3];

    if (cmd == SYSEX_CMD_GET) {
        send_config_sysex();

    } else if (cmd == SYSEX_CMD_DATA && size >= 4 + SYSEX_DATA_LEN) {
        const uint8_t *d = data + 4; // payload start (after F0, MFR, DEVICE, CMD)
        current_settings.active_bank = d[0];
        // d[1] = num_valid_banks is read-only, skip
        for (int i = 0; i < 4; i++) {
            current_settings.outputs[i].type    = (OutputType)d[2 + i * 3];
            current_settings.outputs[i].channel = d[2 + i * 3 + 1];
            current_settings.outputs[i].note    = d[2 + i * 3 + 2];
        }
        current_settings.cv_input_role[0] = (InputRole)d[14];
        current_settings.cv_input_role[1] = (InputRole)d[15];
        current_settings.knob_role[0]     = (InputRole)d[16];
        current_settings.knob_role[1]     = (InputRole)d[17];
        current_settings.knob_role[2]     = (InputRole)d[18];
        settings_save(&current_settings);
        if ((int)current_settings.active_bank != bankManager.active_bank())
            bankManager.switch_bank(current_settings.active_bank);
        send_config_sysex(); // echo confirmation

    } else if (cmd == SYSEX_CMD_REBOOT) {
        rp2040.rebootToBootloader();
    }
}

// ---------------------------------------------------------------------------
// Boot-mode / config-mode state
// ---------------------------------------------------------------------------
static bool config_mode      = false;
static bool boot_check_done  = false;
static uint32_t boot_hold_start_ms  = 0;
static uint32_t boot_window_start_ms = 0;  // set on first loop() call
static int config_mode_displayed_bank = -1;

static void config_mode_update_leds(int bank) {
    // LEDs 0..N-1 lit to show which bank is selected (0 = all off → compiled-in).
    for (int i = 0; i < sw.numLeds; i++) {
        if (bank > 0 && i < bank) sw.LedOn(i);
        else                       sw.LedOff(i);
    }
}

// Called from loop() — handles all config-mode logic.
// Returns true if we are in config mode (caller should skip normal processing).
static uint32_t cfg_debug_last_ms = 0;  // rate-limit the boot-mode debug prints

static bool handle_config_mode() {
    if (!boot_check_done) {
        uint32_t now = millis();

        // Record the timestamp of the first loop() call.  By this point setup()
        // has completed and core1 has been running audio interrupts for the
        // full duration of setup(), so the switch hardware is definitely live.
        // We use elapsed time relative to this point so a slow setup() (e.g.
        // TinyUSB enumeration, flash reads) doesn't shrink or eliminate the
        // config-mode detection window.
        if (boot_window_start_ms == 0) {
            boot_window_start_ms = now;
            if (Serial) Serial.printf(
                "ConfigMode: window opened at %ums; hold Down for 1.5s within 8s\n", now);
        }
        uint32_t elapsed = now - boot_window_start_ms;

        // Periodic status print every ~500 ms while window is open
        if (Serial && (now - cfg_debug_last_ms >= 500)) {
            cfg_debug_last_ms = now;
            const char *sw_name[] = {"Down", "Middle", "Up"};
            int sv = (int)sw.SwitchVal();
            if (sv < 0 || sv > 2) sv = 2;
            Serial.printf("ConfigMode: elapsed=%ums switch=%s hold_for=%ums\n",
                elapsed, sw_name[sv],
                (boot_hold_start_ms && sv == 0) ? (now - boot_hold_start_ms) : 0u);
        }

        // Detection window: 8 seconds from first loop() call.
        // User must hold the switch Down for 1.5 s within this window.
        if (elapsed < 8000) {
            if (sw.SwitchVal() == ComputerCard::Down) {
                if (boot_hold_start_ms == 0) boot_hold_start_ms = now;
                if (now - boot_hold_start_ms >= 1500) {
                    // Switch held Down for 1.5 s → enter config mode
                    if (Serial) Serial.println("ConfigMode: ENTERED config mode");
                    config_mode = true;
                    boot_check_done = true;
                    // Flash all LEDs three times to signal entry
                    for (int flash = 0; flash < 3; flash++) {
                        for (int i = 0; i < sw.numLeds; i++) sw.LedOn(i);
                        delay(150);
                        for (int i = 0; i < sw.numLeds; i++) sw.LedOff(i);
                        delay(150);
                    }
                    config_mode_displayed_bank = bankManager.active_bank();
                    config_mode_update_leds(config_mode_displayed_bank);
                }
            } else {
                boot_hold_start_ms = 0;
            }
        } else {
            if (Serial) Serial.printf("ConfigMode: window closed at elapsed=%ums - boot-mode unavailable\n", elapsed);
            boot_check_done = true; // window expired, never entering config mode
        }
    }

    if (!config_mode) return false;

    // --- Config mode active ---
    // Main knob selects bank; switch Middle confirms and exits.
    int num_banks = bankManager.num_valid_banks();
    if (num_banks < 1) num_banks = 1;
    int32_t knob = sw.KnobVal(ComputerCard::Main);
    int desired = (int)((knob * num_banks) / 4096);
    if (desired >= num_banks) desired = num_banks - 1;

    if (desired != config_mode_displayed_bank) {
        config_mode_displayed_bank = desired;
        config_mode_update_leds(desired);
    }

    // Switch to Middle position → confirm selection
    if (sw.SwitchVal() == ComputerCard::Middle && sw.SwitchChanged()) {
        bankManager.switch_bank(desired);
        current_settings.active_bank = (uint8_t)desired;
        settings_save(&current_settings);
        config_mode = false;
        // Brief confirmation flash
        for (int i = 0; i < sw.numLeds; i++) sw.LedOn(i);
        delay(300);
        for (int i = 0; i < sw.numLeds; i++) sw.LedOff(i);
    }

    // Keep audio calculated even in config mode.
    if (sw.calculate_mode == IN_MAIN_LOOP)
        sw.CalculateSamples();

    return true; // skip normal loop body
}

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

  setup_uclock(do_tick);
  set_global_restart_callback(global_on_restart);

  #ifdef ENABLE_SHUFFLE
    // set up shuffle pattern
    int8_t shuffle_75[] = {0, 12, 0, 12, 0, 12, 0, 12};
    shuffle_pattern_wrapper[0]->set_steps(shuffle_75, sizeof(shuffle_75));
    //int8_t shuffle_straight[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    //shuffle_pattern_wrapper[0]->set_steps(shuffle_straight, sizeof(shuffle_straight));
    //shuffle_pattern_wrapper[0]->set_amount(0.5);
    //shuffle_pattern_wrapper[0]->set_active(true);
    uClock.setOnStep(shuffled_step_callback);
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
    add_repeating_timer_us(250, usb_repeating_callback, nullptr, &usb_timer);
  #endif
        
  #ifdef ENABLE_CLOCK_INPUT_CV
    set_check_cv_clock_ticked_callback(check_cv_clock_ticked);
  #endif

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

  if (handle_config_mode()) return;

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
    tud_task();
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
