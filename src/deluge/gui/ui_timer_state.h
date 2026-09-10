#pragma once

#include "gui/ui/ui_session.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

enum class TimerName {
	DISPLAY,
	MIDI_LEARN_FLASH,
	DEFAULT_ROOT_NOTE,
	TAP_TEMPO_SWITCH_OFF,
	PLAY_ENABLE_FLASH,
	LED_BLINK,
	LED_BLINK_TYPE_1,
	LEVEL_INDICATOR_BLINK,
	SHORTCUT_BLINK,
	MATRIX_DRIVER,
	UI_SPECIFIC,
	BACK_MENU_EXIT,
	DISPLAY_AUTOMATION,
	READ_INPUTS,
	BATT_LED_BLINK,
	GRAPHICS_ROUTINE,
	OLED_LOW_LEVEL,
	OLED_CONSOLE,
	OLED_SCROLLING_AND_BLINKING,
	SYSEX_DISPLAY,
	METER_INDICATOR_BLINK,
	SEND_MIDI_FEEDBACK_FOR_AUTOMATION,
	INTERPOLATION_SHORTCUT_BLINK,
	PAD_SELECTION_SHORTCUT_BLINK,
	NOTE_ROW_BLINK,
	LOADING_ANIMATION,
	MOD_ENCODER_POPUP_FLUSH,
	SELECTED_CLIP_PULSE,
	/// Idle countdown before the screensaver appears, then its frame clock while it shows
	SCREENSAVER,
	/// Total number of timers
	NUM_TIMERS
};

struct Timer {
	bool active;
	uint32_t triggerTime;
};

// Storage and deadline arithmetic are independent of hardware callbacks, so
// ownership, wraparound and mirror suspension can be tested on the host.
class UITimerState {
public:
	using Id = deluge::gui::ui_session::Id;
	struct Bank {
		uint32_t next_event = std::numeric_limits<int32_t>::max();
		uint32_t pause_time = 0;
		std::array<Timer, static_cast<size_t>(TimerName::NUM_TIMERS)> timers{};
	};

	static Id owner(TimerName name) {
		switch (name) {
		case TimerName::READ_INPUTS:
		case TimerName::BATT_LED_BLINK:
		case TimerName::OLED_LOW_LEVEL:
		case TimerName::SYSEX_DISPLAY:
		case TimerName::SEND_MIDI_FEEDBACK_FOR_AUTOMATION:
		case TimerName::SCREENSAVER:
			return Id::Local;
		default:
			return deluge::gui::ui_session::current();
		}
	}

	Bank& bank(Id id) { return banks_.for_owner(id); }
	Bank& active_bank() { return banks_.active(); }
	Timer& get(TimerName name) { return bank(owner(name)).timers[static_cast<size_t>(name)]; }

	void recompute(Id id, uint32_t now) {
		auto& state = bank(id);
		int32_t distance = std::numeric_limits<int32_t>::max();
		for (const auto& timer : state.timers) {
			if (timer.active)
				distance = std::min(distance, static_cast<int32_t>(timer.triggerTime - now));
		}
		state.next_event = now + static_cast<uint32_t>(distance);
	}

	void set(TimerName name, uint32_t now, int32_t samples) {
		auto& state = bank(owner(name));
		auto& timer = get(name);
		timer = {true, now + static_cast<uint32_t>(samples)};
		if (samples < static_cast<int32_t>(state.next_event - now))
			state.next_event = timer.triggerTime;
	}

	void unset(TimerName name, uint32_t now) {
		get(name).active = false;
		recompute(owner(name), now);
	}

	void follow(TimerName target, TimerName source, uint32_t now) {
		get(target) = {true, get(source).triggerTime};
		recompute(owner(target), now);
	}

	void pause_local(uint32_t now) { bank(Id::Local).pause_time = now; }
	void resume_local(uint32_t now) {
		auto& state = bank(Id::Local);
		uint32_t elapsed = now - state.pause_time;
		for (size_t i = 0; i < state.timers.size(); ++i) {
			if (i != static_cast<size_t>(TimerName::OLED_LOW_LEVEL) && state.timers[i].active)
				state.timers[i].triggerTime += elapsed;
		}
		recompute(Id::Local, now);
	}

private:
	deluge::gui::ui_session::State<Bank> banks_;
};
