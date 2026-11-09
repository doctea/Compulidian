#pragma once

#include <stdint.h>

struct CvClockInputDiagnostics {
    uint32_t pulses = 0;
    uint32_t resets = 0;
    uint32_t overflow = 0;
    uint32_t last_pulse_us = 0;
    uint32_t last_reset_us = 0;
    uint32_t interval_us = 0;
    uint32_t min_interval_us = 0;
    uint32_t max_interval_us = 0;
    uint32_t queue_lag_us = 0;
    uint32_t max_queue_lag_us = 0;
};

#ifdef ENABLE_CLOCK_INPUT_CV
void capture_cv_clock_edges(bool reset_rising, bool clock_rising);
bool take_cv_clock_event(bool &reset, uint32_t &observed_at_us);
CvClockInputDiagnostics get_cv_clock_input_diagnostics();
#endif