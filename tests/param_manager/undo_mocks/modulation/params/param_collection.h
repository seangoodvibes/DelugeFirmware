#pragma once
#include "model/model_stack.h"
#include <cstdlib>

class ParamCollection {
public:
	int32_t get_current_value(int32_t) const { return live->currentValue; }
	AutoParam* live;
	int expectedId;
	int swaps = 0;
	Error remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* stack) {
		if (stack->paramId != expectedId || stack->paramCollection != this) {
			std::abort();
		}
		++swaps;
		live->nodes.swapStateWith(&state->nodes);
		std::swap(live->currentValue, state->value);
		return Error::NONE;
	}
};
