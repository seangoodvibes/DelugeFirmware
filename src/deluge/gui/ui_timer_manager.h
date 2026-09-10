/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "gui/ui_timer_state.h"
#include "util/misc.h"
#include <array>
#include <cstdint>

class UITimerManager {
public:
	UITimerManager();

	void routine();
	void setTimer(TimerName which, int32_t ms);
	void setTimerSamples(TimerName which, int32_t samples);
	void unsetTimer(TimerName which);
	void pause_for_mirror();
	void resume_from_mirror();

	bool isTimerSet(TimerName which);
	void setTimerByOtherTimer(TimerName which, TimerName fromTimer);

private:
	UITimerState state_;
	void workOutNextEventTime();

public:
	[[gnu::always_inline]] inline Timer& getTimer(TimerName which) { return state_.get(which); }
};

extern UITimerManager uiTimerManager;
