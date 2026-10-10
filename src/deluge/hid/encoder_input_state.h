#pragma once

#include "gui/ui/ui_session.h"
#include <array>
#include <cstdint>

namespace deluge::hid::encoders {

struct InputState {
	std::array<uint32_t, 2> timeModEncoderLastTurned{};
	std::array<int8_t, 2> initial_turn_direction{};
	uint32_t waiting_for_card_routine_end = 0;
};

inline gui::ui_session::State<InputState> input_states;
inline InputState& input_state() {
	return input_states.active();
}
inline auto& time_mod_encoder_last_turned_for_session() {
	return input_state().timeModEncoderLastTurned;
}

} // namespace deluge::hid::encoders
