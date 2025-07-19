#pragma once

#include <Arduino.h>
#include "ComputerCard.h"
#include "__version.h"
#include "sequencer/sequencing.h"
#include "audio/audio.h"
#include "Drums.h"
#include "outputs/base_outputs.h"
#include "cv_input.h"

#ifdef USE_TINYUSB
  extern bool debug_enable_output_parameter_input;
  extern char serial_input_buffer[256];
  extern char serial_input_buffer_index;
  void __not_in_flash_func(process_serial_input)();
#endif