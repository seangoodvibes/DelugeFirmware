#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class ConsequenceParamChange;

namespace parameter_test {
extern bool allow_midi_params;
struct midi_notification {
	int32_t cc;
	int32_t old_value;
	int32_t new_value;
};
extern std::vector<midi_notification> midi_notifications;
extern bool allow_patch_cables;
extern size_t notifications;
extern int allocations_before_failure;
extern size_t allocation_failures;
extern uint32_t last_failed_allocation_size;
extern bool allow_no_action;
extern int32_t loop_length;
extern int32_t play_pos;
extern bool reversed;
extern bool allow_recording_controls;
extern int indicator_calls;
extern uint8_t indicator_knob;
extern uint8_t indicator_level;
extern bool indicator_bipolar;
size_t outstanding_allocations();
void reset();
std::unique_ptr<ConsequenceParamChange> take_snapshot();
} // namespace parameter_test
