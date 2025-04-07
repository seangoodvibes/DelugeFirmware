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

#include "gui/views/automation/context/song/arranger.h"
#include "gui/views/arranger_view.h"
#include "gui/views/view.h"
#include "hid/led/indicator_leds.h"
#include "model/song/song.h"

PLACE_SDRAM_BSS AutomationViewArranger automationViewArranger{};

AutomationViewArranger::AutomationViewArranger() {
}

// used for the play cursor
void AutomationViewArranger::graphicsRoutine() {
	arrangerView.graphicsRoutine();
	AutomationView::graphicsRoutine();
}

void AutomationViewArranger::focusRegained() {
	indicator_leds::setLedState(IndicatorLED::BACK, false);
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);
	currentSong->affectEntire = true;
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);
	AutomationView::focusRegained();
}

// button action
ActionResult AutomationViewArranger::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	using namespace hid::button;

	// these button actions are not used in the arranger automation view
	if (b == SCALE_MODE || b == KEYBOARD || b == KIT || b == SYNTH || b == MIDI || b == CV || b == CLIP_VIEW) {
		return ActionResult::DEALT_WITH;
	}

	return AutomationView::buttonAction(b, on, inCardRoutine);
}
