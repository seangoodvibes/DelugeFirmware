#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

class ConsequenceParamChange;

namespace parameter_test {
extern size_t notifications;
extern int allocations_before_failure;
extern size_t allocation_failures;
extern bool allow_no_action;
extern int32_t loop_length;
extern int32_t play_pos;
extern bool reversed;
size_t outstanding_allocations();
void reset();
std::unique_ptr<ConsequenceParamChange> take_snapshot();
} // namespace parameter_test
