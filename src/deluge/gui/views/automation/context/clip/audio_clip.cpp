/*
 * Copyright (c) 2025 Sean Ditny
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

#include "gui/views/automation/context/clip/audio_clip.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/view.h"
#include "hid/led/indicator_leds.h"
#include "model/song/song.h"

PLACE_SDRAM_BSS AutomationViewAudioClip automationViewAudioClip{};

AutomationViewAudioClip::AutomationViewAudioClip() {
}

// used for the play cursor
void AutomationViewAudioClip::graphicsRoutine() {
	audioClipView.graphicsRoutine();
	AutomationView::graphicsRoutine();
}

// called everytime you open up the automation view
bool AutomationViewAudioClip::opened() {
	AutomationView::initialize();
	AutomationViewClip::openedInBackground();
	focusRegained();
	return true;
}

void AutomationViewAudioClip::focusRegained() {
	ClipView::focusRegained();
	indicator_leds::setLedState(IndicatorLED::BACK, false);
	indicator_leds::setLedState(IndicatorLED::AFFECT_ENTIRE, true);
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(getCurrentClip());
	AutomationView::focusRegained();
}

// button action
ActionResult AutomationViewAudioClip::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	using namespace hid::button;

	// these button actions are not used in the audio clip automation view
	if (b == SCALE_MODE || b == KEYBOARD || b == KIT || b == SYNTH || b == MIDI || b == CV) {
		return ActionResult::DEALT_WITH;
	}

	else {
		return AutomationView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}
