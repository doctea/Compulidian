
#include "shared_debug.h"
#include "serial_debug.h"

#include "sequencer/Base/Patterns.h"

#include "sequencer/Euclidian/Sequencer.h"

#include "audio/bank_manager.h"
#include "audio/flash_layout.h"
#include "audio/bank_header.h"
#include "audio/sample_store.h"
#include "settings.h"
#include "configurator.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include "hardware/clocks.h"

#ifdef ENABLE_LITTLEFS
  #include "live_settings.h"
#endif

extern int debug_step_offset;

volatile uint32_t pulse_0_transitions = 0;
volatile uint32_t pulse_1_transitions = 0;

#ifdef USE_TINYUSB

  #ifdef ENABLE_CLOCK_INPUT_CV
    static bool set_cv_clock_ppqn_from_serial(const char *command) {
      const char prefix[] = "clock-external-cv-ppqn";
      if (strncmp(command, prefix, sizeof(prefix) - 1) != 0 || command[sizeof(prefix) - 1] != ' ') return false;
      const char *start = command + sizeof(prefix);
      while (isspace((unsigned char)*start)) ++start;
      if (!isdigit((unsigned char)*start)) return false;
      char *end;
      errno = 0;
      const unsigned long value = strtoul(start, &end, 10);
      while (isspace((unsigned char)*end)) ++end;
      if (errno == ERANGE || *end != '\0' || value > 24 || !settings_valid_cv_clock_ppqn((uint8_t)value)) return false;
      if (!set_external_cv_clock_ppqn((uint16_t)value)) return false;
      current_settings.cv_clock_ppqn = (uint8_t)value;
      return true;
    }
  #endif

  static void print_clock_state() {
    const auto clock = get_external_clock_diagnostics();
    Serial_printf("Clock: set_bpm=%.3f playing=%u mode=%d ppqn=%u waiting=%u ticks=%lu pulses=%lu rejected=%lu delivered=%lu expected=%lu adapter_overflow=%lu\n",
        get_bpm(), (unsigned)playing, (int)clock_mode, (unsigned)clock.input_ppqn,
        (unsigned)waiting_for_external_clock, (unsigned long)ticks,
        (unsigned long)clock.received_pulses, (unsigned long)clock.rejected_pulses,
        (unsigned long)clock.delivered_sync_ticks, (unsigned long)clock.expected_sync_ticks,
        (unsigned long)clock.adapter_overflow);
    #ifdef USE_UCLOCK
      Serial_printf("Engine: bpm=%.3f state=%u strict=%u stalled=%u pending=%u accepted_interval_us=%lu pulse_age_us=%lu\n",
          uClock.getTempo(), (unsigned)uClock.clock_state, (unsigned)uClock.isStrictExternalMode(),
          (unsigned)uClock.isExternalClockStalled(), (unsigned)uClock.getExternalTicksRemaining(),
          (unsigned long)uClock.getLastAcceptedExternalInterval(), (unsigned long)uClock.getExternalClockPulseAge());
    #endif
    #ifdef ENABLE_CLOCK_INPUT_CV
      const auto input = get_cv_clock_input_diagnostics();
      const double detected = input.interval_us && current_settings.cv_clock_ppqn
          ? 60000000.0 / input.interval_us / current_settings.cv_clock_ppqn : 0;
      Serial_printf("CV: ppqn=%u detected_bpm=%.3f reset_required=%u reset_song=%u pulses=%lu resets=%lu lost=%lu interval_us=%lu min_us=%lu max_us=%lu lag_us=%lu max_lag_us=%lu\n",
          (unsigned)current_settings.cv_clock_ppqn, detected, (unsigned)current_settings.cv_reset_required,
          (unsigned)current_settings.cv_reset_song_position, (unsigned long)input.pulses, (unsigned long)input.resets,
          (unsigned long)input.overflow, (unsigned long)input.interval_us, (unsigned long)input.min_interval_us,
          (unsigned long)input.max_interval_us, (unsigned long)input.queue_lag_us, (unsigned long)input.max_queue_lag_us);
    #endif
  }

  static void print_sequencer_state() {
    Serial_printf("State: ticks=%lu ready=%u knobs=%ld,%ld,%ld cv=%ld,%ld audio=%ld,%ld\n",
        (unsigned long)ticks, (unsigned)sw.SwitchReady(),
        (long)sw.KnobVal(ComputerCard::Main), (long)sw.KnobVal(ComputerCard::X), (long)sw.KnobVal(ComputerCard::Y),
        (long)sw.CVIn(0), (long)sw.CVIn(1), (long)sw.AudioIn(0), (long)sw.AudioIn(1));
    Serial_printf("Clock: playing=%u mode=%d bpm=%.3f\n", (unsigned)playing, (int)clock_mode, get_bpm());
    if (sequencer == nullptr) {
      Serial_println("Sequencer not ready");
      return;
    }
    auto* euclidian = static_cast<EuclidianSequencer*>(sequencer);
    Serial_printf("Sequencer: running=%u seed=%d seed_lock=%u mutate=%u mutations=%d/%d\n",
        (unsigned)sequencer->is_running(), euclidian->get_euclidian_seed(),
        (unsigned)euclidian->is_euclidian_seed_lock(), (unsigned)euclidian->is_mutate_enabled(),
        (int)euclidian->get_mutation_count(), (int)euclidian->get_effective_mutation_count());
    for (unsigned int index = 0; index < sequencer->get_number_patterns(); ++index) {
      auto* pattern = static_cast<EuclidianPattern*>(sequencer->get_pattern(index));
      if (pattern == nullptr) continue;
      Serial_printf("Pattern[%u] %s density=%.5f locked=%u dirty=%u held=%u on_tick=%lu gate_ticks=%d output_enabled=%u\n",
          index, pattern->get_output_label(), pattern->get_global_density(), (unsigned)pattern->is_locked(),
          (unsigned)pattern->needs_recompute, (unsigned)pattern->note_held,
          (unsigned long)pattern->triggered_on_tick, (int)pattern->current_duration,
          (unsigned)(pattern->get_output() != nullptr && pattern->get_output()->is_enabled()));
      Serial_printf("  base=%d/%d+%d duration=%d used=%d/%d+%d duration=%d post_pulses=%d\n",
          (int)pattern->arguments.steps, (int)pattern->arguments.pulses,
          (int)pattern->arguments.rotation, (int)pattern->arguments.duration,
          (int)pattern->used_arguments.steps, (int)pattern->used_arguments.pulses,
          (int)pattern->used_arguments.rotation, (int)pattern->used_arguments.duration,
          (int)pattern->last_post_arguments.pulses);
    }
  }

  bool debug_enable_output_parameter_input = false;

  char serial_input_buffer[256];
  char serial_input_buffer_index = 0;
  void __not_in_flash_func(process_serial_input)() {
    if (!FOUNDLING_SERIAL_CONNECTED) {
      serial_input_buffer_index = 0;
      return;
    }
    //Serial.println(F("process_serial_input()"));
    while (Serial.available()) {
      char c = Serial.read();
      //if(c=='\r') continue; // ignore carriage return

      if (serial_input_buffer_index >= sizeof(serial_input_buffer)-1) {
        Serial_printf("serial_input_buffer overflow, resetting\n");
        serial_input_buffer_index = 0;
      }

      if (c=='\b') {          // handle backspace -- does't actually seem to work?
        Serial_printf("backspace?");
        if (serial_input_buffer_index > 0) {
          serial_input_buffer_index--;
          Serial_printf("\b");
        }
      } else if (c=='\t') {   // handle tab: recall previous command
        serial_input_buffer_index = strlen(serial_input_buffer);
        Serial_printf("\n%s", serial_input_buffer);
      } else if (c=='\r' || c=='\n' || c=='#') {  // handle enter of line or # as substitute
        serial_input_buffer[serial_input_buffer_index] = 0;
        Serial_printf("\ngot '%s'\n", serial_input_buffer);

        if (strcmp(serial_input_buffer, "transitions")==0) {
          Serial_printf("pulse_0_transitions: %lu\n", pulse_0_transitions);
          Serial_printf("pulse_1_transitions: %lu\n", pulse_1_transitions);
        } else if (strcmp(serial_input_buffer, "state")==0) {
          print_sequencer_state();
        } else if (strcmp(serial_input_buffer, "clock-state")==0) {
          print_clock_state();
        } else if (strcmp(serial_input_buffer, "clock-internal")==0) {
          Serial_println(F("Clock internal command received"));
          change_clock_mode(ClockMode::CLOCK_INTERNAL);
        } else if (strcmp(serial_input_buffer, "clock-external-cv")==0) {
          #ifdef ENABLE_CLOCK_INPUT_CV
              Serial_println(F("Clock external command received"));
              change_clock_mode(ClockMode::CLOCK_EXTERNAL_CV);
          #else
              Serial_println(F("CV clock input is not enabled in this firmware"));
          #endif
        } else if (strncmp(serial_input_buffer, "clock-external-cv-ppqn", 22)==0) {
          #ifdef ENABLE_CLOCK_INPUT_CV
              if (set_cv_clock_ppqn_from_serial(serial_input_buffer)) {
                Serial_printf("External CV PPQN set to %u (live, not saved)\n", (unsigned)current_settings.cv_clock_ppqn);
              } else {
                Serial_println(F("Usage: clock-external-cv-ppqn <1|2|4|8|12|16|24>"));
              }
          #else
              Serial_println(F("CV clock input is not enabled in this firmware"));
          #endif
        } else if (strcmp(serial_input_buffer, "clock-external-usb")==0) {
          Serial_println(F("Clock external USB command received"));
          change_clock_mode(ClockMode::CLOCK_EXTERNAL_USB_HOST);
        } else if (strcmp(serial_input_buffer, "play")==0) {
          Serial_println(F("Play command received"));
          clock_start();
        } else if (strcmp(serial_input_buffer, "stop")==0) {
          Serial_println(F("Stop command received"));
          clock_stop();
        } else if (strcmp(serial_input_buffer, "help")==0) {
          Serial_println(F("Available commands:"));
          Serial_println(F("Parameter debug commands:"));
          Serial_println(F("  I            - stream parameter input info to console"));
          Serial_println(F("  d i <index>  - toggle debug on a parameter input"));

          Serial_println(F("Sample player commands:"));
          Serial_println(F("  s            - toggle sample interpolation on/off"));
          Serial_println(F("  c            - toggle calculate mode between main loop and audio worker"));
          Serial_println(F("  v            - toggle volume on/off"));

          Serial_println(F("Seqlib commands:"));
          Serial_println(F("  f            - toggle fills on/off"));
          Serial_println(F("  state        - dump controls, clock, pattern and held-note state"));
          Serial_println(F("  clock-state  - dump BPM, pulse/reset counts, queue losses and strict clock state"));

          Serial_println(F("Kit bank commands:"));
          Serial_println(F("  b            - print bank detection status"));
          Serial_println(F("  B <slot>     - switch to bank slot <slot>"));
          Serial_println(F("  l            - list track patterns and assigned voice samples"));
          Serial_println(F("  lx           - list track patterns and assigned voice samples (extended)"));
          Serial_println(F("  p <pattern>  - trigger named pattern"));
          Serial_println(F("  X            - send sysex config to host"));
          Serial_println(F("  y            - dump saveloadlib settings tree"));
          Serial_println(F("  Y            - dump sample store status"));
          
          Serial_println(F("Clock commands:"));
          Serial_println(F("  play         - start the clock"));
          Serial_println(F("  stop         - stop the clock"));
          Serial_println(F("  clock-internal       - set clock mode to internal"));
          #ifdef ENABLE_CLOCK_INPUT_CV
            Serial_println(F("  clock-external-cv    - set clock mode to external CV"));
            Serial_printf("  clock-external-cv-ppqn <1|2|4|8|12|16|24> - set live CV PPQN (currently %u)\n", external_cv_ppqn);
            Serial_println(F("  transitions       - (debug) show pulse input transitions"));
          #endif
          Serial_println(F("  clock-external-usb   - set clock mode to external USB host"));
          
          Serial_println(F("  V            - dump version/build information"));
          Serial_println(F("  help         - show this help message"));

          Serial_println();
          Serial_printf("Clock mode: %s\n", get_clock_mode_string(clock_mode));
          
        } else if (serial_input_buffer[0]=='l') {
          // list the track names
          Serial_println("l command received!"); Serial_flush();
          bool extended = serial_input_buffer[1] == 'x';
          if (extended) Serial_println(F("Extended listing enabled"));
          for (int i = 0 ; i < sequencer->get_number_patterns() ; i++) {
            //Serial.printf("Pattern %i/%i\n", i+1, sequencer->number_patterns); Serial.flush();
            SimplePattern *p = (SimplePattern *)sequencer->get_pattern(i);
           Serial_printf("Pattern [%i/%i]:\t%s\n", i+1, sequencer->get_number_patterns(), p->get_output_label()); Serial_flush();
          }
          for (int i = 0 ; i < NUM_VOICES ; i++) {
            Serial_printf("Voice [%i/%i]:\t%s on %i (%s)\n", i+1, NUM_VOICES, sample[voice[i].sample].sname, sample[voice[i].sample].MIDINOTE, get_note_name_c(sample[voice[i].sample].MIDINOTE, GM_CHANNEL_DRUMS));
            if (extended) {
              const int sample_number = voice[i].sample;
              const bool has_data = sample_data && sample_data[sample_number];
              Serial_printf("  data=%s size=%u volume=%u position=%lu\n",
                  has_data ? "yes" : "no",
                  has_data ? (unsigned)sample_data[sample_number]->size() : 0u,
                  (unsigned)sample[sample_number].play_volume,
                  (unsigned long)(voice[i].sampleindex >> 12));
              }
            Serial_flush();
          }
          if (extended) {
              Serial_printf("Audio: bank=%d mode=%d callbacks=%lu buffers=%lu read=%d write=%d index=%u pitch=%d volume=%s\n",
              bankManager.active_bank(), (int)sw.calculate_mode,
              (unsigned long)sw.audio_callback_count, (unsigned long)sw.audio_buffer_count,
              (int)sw.read_buffer_id, (int)sw.write_buffer_id, (unsigned)sw.bufferIndex,
              (int)sw.global_pitch, sw.enable_volume ? "on" : "off");
                Serial_printf("Timing: sys_khz=%lu adc_divider=%u hardware_hz=%.2f playback_hz=%.2f samples=%lu\n",
                  (unsigned long)(clock_get_hz(clk_sys) / 1000), (unsigned)COMPUTERCARD_ADC_DIVIDER,
                  48000000.0 / (8.0 * (COMPUTERCARD_ADC_DIVIDER + 1)),
                  (double)COMPULIDIAN_PLAYBACK_RATE_HZ, (unsigned long)sw.playback_sample_count);
          }
        } else if (serial_input_buffer[0]=='p') {  // trigger named pattern
          Serial_println(F("p command received!"));
          for (int i = 0 ; i < sequencer->get_number_patterns() ; i++) {
            SimplePattern *p = (SimplePattern *)sequencer->get_pattern(i);
            if (p->get_output()->matches_label(serial_input_buffer+2)) {
              Serial_printf("Triggering pattern %i: %s\n", i, p->get_output_label());
              p->trigger_on_for_step(BPM_CURRENT_STEP_OF_PHRASE);
            } /*else {
              Serial.printf("pattern %i: null\n", i);
            }*/
          } 
        } else if (serial_input_buffer[0]=='f') {   // toggle fills_enabled
          Serial_printf("f command received - fills_enabled was %s!\n", ((EuclidianSequencer*)sequencer)->is_fills_enabled() ? "true" : "false");
          ((EuclidianSequencer*)sequencer)->set_fills_enabled(!((EuclidianSequencer*)sequencer)->is_fills_enabled());
          current_settings.fills_enabled = ((EuclidianSequencer*)sequencer)->is_fills_enabled();
          Serial_printf("f command received - fills_enabled is now %s!\n", ((EuclidianSequencer*)sequencer)->is_fills_enabled() ? "true" : "false");
        } else if (serial_input_buffer[0]=='I') {   // toggle debug on/off for output parameter inputs
          Serial_println(F("I command received!"));
          debug_enable_output_parameter_input = !debug_enable_output_parameter_input;
        } else if (serial_input_buffer[0]=='d' || serial_input_buffer[0]=='D') {  // toggle debug on/off for parameters or inputs
          Serial_println(F("d command received!"));
          if (serial_input_buffer[1]=='p') {
            // set debug status on a Parameter
            Serial_println(F("d p command received!"));
            if(strlen(serial_input_buffer)>2) {
              // get the index of the parameter from 2nd byte
              int index = atoi(&serial_input_buffer[2]);
              if (index>=0 && index<parameter_manager->available_parameters->size()) {
                FloatParameter *p = parameter_manager->available_parameters->get(index);
                p->debug = serial_input_buffer[0]=='D' ? true : false;
                Serial_printf("parameter %i: %s set to debug=%s\n", index, p->label, serial_input_buffer[0]=='D' ? "true" : "false");
              } else {
                Serial_printf("invalid parameter index %i\n", index);
              }
            } else {
              Serial_println(F("d i command needs a parameter index!"));
            }
          } else {
            // set debug status on a ParameterInput
            Serial_println(F("d i command received!"));
            if(strlen(serial_input_buffer)>2) {
              // get the index of the parameter from 2nd byte
              int index = atoi(&serial_input_buffer[2]);
              if (index>=0 && index<parameter_manager->available_inputs->size()) {
                BaseParameterInput *p = parameter_manager->available_inputs->get(index);
                p->debug = serial_input_buffer[0]=='D' ? true : false;
                Serial_printf("input %i: %s set to debug=%s\n", index, p->name, serial_input_buffer[0]=='D' ? "true" : "false");
              } else {
                Serial_printf("invalid input index %i\n", index);
              }
            } else {
              Serial_println(F("d i command needs an input index!"));
            }
          }
          serial_input_buffer_index = 0;
          Serial_println("Finished parsing serial input");
          break;
        } else if (serial_input_buffer[0]=='s') {   // toggle sample interpolation on/off
          sw.interpolate_enabled = !sw.interpolate_enabled;
          Serial_printf("Interpolation %s\n", sw.interpolate_enabled ? "enabled" : "disabled");
        } else if (serial_input_buffer[0]=='c') {  // toggle calculate mode between main loop (core0) and audio worker (core1)
          Serial_printf("calculate_mode was %s\n", sw.calculate_mode == IN_MAIN_LOOP ? "IN_MAIN_LOOP" : "IN_PROCESS_SAMPLE");
          sw.calculate_mode = sw.calculate_mode == IN_MAIN_LOOP ? IN_AUDIO_WORKER: IN_MAIN_LOOP;
          Serial_printf("calculate_mode now %s\n", sw.calculate_mode == IN_MAIN_LOOP ? "IN_MAIN_LOOP" : "IN_PROCESS_SAMPLE");
        } else if (serial_input_buffer[0]=='v') {  // toggle volume on/off
          Serial_printf("volume was %s\n", sw.enable_volume ? "enabled" : "disabled");
          sw.enable_volume = !sw.enable_volume;
          Serial_printf("volume now %s\n", sw.enable_volume ? "enabled" : "disabled");
        } else if (serial_input_buffer[0]=='V') {
          // dump version/build information
            Serial_printf("COMMIT_INFO: %s\n", COMMIT_INFO);
            Serial_printf("ENV_NAME: %s\n", ENV_NAME);
            Serial_printf("Built at " __TIME__ " on " __DATE__ "\n");
        } else if (serial_input_buffer[0]=='b') {
          // re-print bank detection status with per-sample detail
          Serial_printf("--- Banks: active=%d  valid=%d  flash=0x%08X+0x%08X  max=%d ---\n",
              bankManager.active_bank(), bankManager.num_valid_banks(),
              FLASH_BANKS_OFFSET, FLASH_BANK_SIZE, FLASH_MAX_BANKS);
          Serial_printf("  slot 0: compiled-in  valid=%s  voices=%d\n",
              bankManager.is_bank_valid(0) ? "yes" : "no", NUM_VOICES);
          #ifdef ENABLE_LITTLEFS
              for (int slot = 1; slot <= FLASH_MAX_BANKS; ++slot) {
                  bankManager.debug_print_bank_detail(slot);
              }
          #else
                    Serial_println("  (bank config files unavailable - ENABLE_LITTLEFS not defined in this build)");
          #endif
          Serial_printf("---\n");

        } else if (serial_input_buffer[0]=='B') {
          // switch to a different bank
          int desired_bank = atoi(&serial_input_buffer[2]);
          if (bankManager.switch_bank(desired_bank)) {
              Serial_printf("Switched to bank %d\n", desired_bank);
          } else {
              Serial_printf("Failed to switch to bank %d\n", desired_bank);
          }
        } else if (serial_input_buffer[0]=='X') {
          // send config to the host (for debugging)
          send_config_sysex();
        } else if (serial_input_buffer[0]=='y') {
          // dump the live saveloadlib settings tree - Phase A smoke test
          #ifdef ENABLE_LITTLEFS
            extern ISaveableSettingHost* SL_ROOT;
            if (SL_ROOT) {
                LinkedList<String> lines;
                sl_save_to_linkedlist(SL_ROOT, lines, SL_SCOPE_ALL);
                for (size_t i = 0; i < lines.size(); i++) Serial_println(lines.get(i));
            } else {
                Serial_println("SL_ROOT not registered yet");
            }
          #else
            Serial_println("ENABLE_LITTLEFS not defined in this build");
          #endif
        } else if (serial_input_buffer[0]=='Y') {
          // dump sample store status - Phase B smoke test
          Serial_printf("SampleStore: valid=%s  entries=%u  region=0x%08X+0x%08X\n",
              sampleStore.is_valid() ? "yes" : "no",
              sampleStore.num_entries(),
              FLASH_SAMPLESTORE_ADDR, FLASH_SAMPLESTORE_SIZE
          );
          for (uint32_t i = 0; i < sampleStore.num_entries(); i++) {
              const SampleStoreEntryHeader *e = sampleStore.entry_at(i);
              if (!e) continue;
              Serial_printf("  [%3u] hash=0x%08X  %5u Hz  %ub x %u  '%.*s'\n",
                  i, e->content_hash, e->sample_rate, e->bit_depth, e->num_samples,
                  24, e->name
              );
          }
        // } else if (serial_input_buffer[0]=='Z') {
        //   // set the debug_step_offset to a new value
        //   int new_offset = atoi(&serial_input_buffer[2]);
        //   debug_step_offset = new_offset;
        //   Serial.printf("debug_step_offset set to %d (%d)\n", debug_step_offset, new_offset);
        } else if (serial_input_buffer[0]=='?') {
          Serial_println(
            "Commands: "
            "p [pattern_name]=trigger pattern  "
            "f=toggle fills  "
            "b=bank status  "
            "B [N]=switch to bank N  "
            "l=list patterns  "
            "s=interpolation  "
            "c=calc mode  "
            "v=volume  "
            "V=version  "
            "I=input debug  "
            "d/D=param debug  "
            "y=dump live settings  "
            "Y=dump sample store"
          );
        }
        serial_input_buffer_index = 0;
      } else {
        serial_input_buffer[serial_input_buffer_index++] = c;
      }
    }
  }
#endif