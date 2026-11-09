#ifdef COMPUTERCARD_NOIMPL
#undef COMPUTERCARD_NOIMPL
#endif
#include "ComputerCard.h"

#ifdef ENABLE_CLOCK_INPUT_CV
	#include <atomic>
	#include "clock.h"
	#include "cv_clock_input.h"

	extern volatile uint32_t pulse_0_transitions;
	extern volatile uint32_t pulse_1_transitions;

	struct CvClockEvent {
		uint32_t observed_at_us;
		bool reset;
	};

	static constexpr uint8_t CV_CLOCK_EVENT_QUEUE_SIZE = 8;
	static CvClockEvent cv_clock_events[CV_CLOCK_EVENT_QUEUE_SIZE];
	static std::atomic<uint32_t> cv_clock_event_head{0};
	static std::atomic<uint32_t> cv_clock_event_tail{0};
	volatile uint32_t cv_clock_event_overflow = 0;
	static volatile CvClockInputDiagnostics cv_clock_input_diagnostics;

	static void enqueue_cv_clock_event(bool reset) {
		const uint32_t observed_at_us = micros();
		if (reset) {
			cv_clock_input_diagnostics.resets++;
			cv_clock_input_diagnostics.last_reset_us = observed_at_us;
		} else {
			if (cv_clock_input_diagnostics.pulses > 0) {
				const uint32_t interval = observed_at_us - cv_clock_input_diagnostics.last_pulse_us;
				cv_clock_input_diagnostics.interval_us = interval;
				if (cv_clock_input_diagnostics.min_interval_us == 0 || interval < cv_clock_input_diagnostics.min_interval_us)
					cv_clock_input_diagnostics.min_interval_us = interval;
				if (interval > cv_clock_input_diagnostics.max_interval_us)
					cv_clock_input_diagnostics.max_interval_us = interval;
			}
			cv_clock_input_diagnostics.pulses++;
			cv_clock_input_diagnostics.last_pulse_us = observed_at_us;
		}
		const uint32_t head = cv_clock_event_head.load(std::memory_order_relaxed);
		const uint32_t next_head = (head + 1) % CV_CLOCK_EVENT_QUEUE_SIZE;
		if (next_head == cv_clock_event_tail.load(std::memory_order_acquire)) {
			cv_clock_event_overflow++;
			cv_clock_input_diagnostics.overflow++;
			clock_report_external_event_overflow();
			return;
		}

		cv_clock_events[head] = {observed_at_us, reset};
		cv_clock_event_head.store(next_head, std::memory_order_release);
	}

	void capture_cv_clock_edges(bool reset_rising, bool clock_rising) {
		if (reset_rising) {
			pulse_0_transitions++;
			enqueue_cv_clock_event(true);
		}
		if (clock_rising) {
			pulse_1_transitions++;
			enqueue_cv_clock_event(false);
		}
	}

	bool take_cv_clock_event(bool &reset, uint32_t &observed_at_us) {
		const uint32_t tail = cv_clock_event_tail.load(std::memory_order_relaxed);
		if (tail == cv_clock_event_head.load(std::memory_order_acquire))
			return false;

		CvClockEvent event = cv_clock_events[tail];
		cv_clock_event_tail.store((tail + 1) % CV_CLOCK_EVENT_QUEUE_SIZE, std::memory_order_release);
		reset = event.reset;
		observed_at_us = event.observed_at_us;
		const uint32_t lag = micros() - observed_at_us;
		cv_clock_input_diagnostics.queue_lag_us = lag;
		if (lag > cv_clock_input_diagnostics.max_queue_lag_us)
			cv_clock_input_diagnostics.max_queue_lag_us = lag;
		return true;
	}

	CvClockInputDiagnostics get_cv_clock_input_diagnostics() {
		return {
			cv_clock_input_diagnostics.pulses, cv_clock_input_diagnostics.resets,
			cv_clock_input_diagnostics.overflow, cv_clock_input_diagnostics.last_pulse_us,
			cv_clock_input_diagnostics.last_reset_us, cv_clock_input_diagnostics.interval_us,
			cv_clock_input_diagnostics.min_interval_us, cv_clock_input_diagnostics.max_interval_us,
			cv_clock_input_diagnostics.queue_lag_us, cv_clock_input_diagnostics.max_queue_lag_us
		};
	}
#endif
