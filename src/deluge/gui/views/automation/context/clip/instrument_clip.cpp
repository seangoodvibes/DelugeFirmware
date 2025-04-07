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

#include "gui/views/automation/context/clip/instrument_clip.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"

PLACE_SDRAM_BSS AutomationViewInstrumentClip automationViewInstrumentClip{};

AutomationViewInstrumentClip::AutomationViewInstrumentClip() {
}

// used for the play cursor
void AutomationViewInstrumentClip::graphicsRoutine() {
	instrumentClipView.graphicsRoutine();
	AutomationView::graphicsRoutine();
}

void AutomationViewInstrumentClip::focusRegained() {
	ClipView::focusRegained();
	instrumentClipView.auditioningSilently = false; // Necessary?
	InstrumentClipMinder::focusRegained();
	instrumentClipView.setLedStates();
	AutomationView::focusRegained();
}

// button action
ActionResult AutomationViewInstrumentClip::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	using namespace hid::button;

	// Scale mode button
	if (b == SCALE_MODE) {
		return instrumentClipView.handleScaleButtonAction(on, inCardRoutine);
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		handleKeyboardButtonAction(on);
	}

	// Synth button
	else if (b == SYNTH && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		handleSynthButtonAction(on);
	}

	// Kit button
	else if (b == KIT) {
		handleKitButtonAction(on);
	}

	// Midi button
	else if (b == MIDI) {
		handleMidiButtonAction(on);
	}

	// Cv button
	else if (b == CV) {
		handleCvButtonAction(on);
	}

	else {
		return AutomationView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

// called by button action if b == KEYBOARD
void AutomationViewInstrumentClip::handleKeyboardButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE)) {
		// reset blinking if you're leaving automation view for keyboard view
		// blinking will be reset when you come back
		AutomationView::resetShortcutBlinking();

		changeRootUI(&keyboardScreen);
	}
}

// called by button action if b == SYNTH
void AutomationViewInstrumentClip::handleSynthButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE)) {
		handleInstrumentChange(OutputType::SYNTH);
	}
}

// called by button action if b == KIT
void AutomationViewInstrumentClip::handleKitButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE)) {
		handleInstrumentChange(OutputType::KIT);
	}
}

// called by button action if b == MIDI
void AutomationViewInstrumentClip::handleMidiButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE)) {
		handleInstrumentChange(OutputType::MIDI_OUT);
	}
}

// called by button action if b == KIT
void AutomationViewInstrumentClip::handleCvButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE)) {
		handleInstrumentChange(OutputType::CV);
	}
}

void AutomationViewInstrumentClip::handleInstrumentChange(OutputType outputType) {
	// if you're going to create a new instrument or change output type, reset selection
	AutomationView::initParameterSelection();

	instrumentClipView.handleInstrumentChange(outputType);
}

// pad action
// handles main grid pad actions (e.g. editing, shortcuts) and sidebar pad actions (e.g. mute, audition)
ActionResult AutomationViewInstrumentClip::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	// mute pad action
	if (x == kDisplayWidth) {
		if (currentUIMode == UI_MODE_MIDI_LEARN) [[unlikely]] {
			return instrumentClipView.commandLearnMutePad(y, velocity);
		}
	}

	return AutomationView::padAction(x, y, velocity);
}
