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

#include "gui/views/automation/context/song/arranger.h"
#include "gui/views/arranger_view.h"
#include "gui/views/view.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/song/song.h"

PLACE_SDRAM_BSS AutomationViewArranger automationViewArranger{};

const uint32_t editPadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

AutomationViewArranger::AutomationViewArranger() {
}

// called everytime you open up the automation view
bool AutomationViewArranger::opened() {
	AutomationView::initialize();
	openedInBackground();
	focusRegained();
	return true;
}

void AutomationViewArranger::openedInBackground() {
	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	AudioEngine::logAction("AutomationViewArranger::beginSession");

	if (renderingToStore) {
		AutomationView::renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
		                               &PadLEDs::occupancyMaskStore[kDisplayHeight], true);
		arrangerView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
		                           &PadLEDs::occupancyMaskStore[kDisplayHeight]);
	}
	else {
		uiNeedsRendering(getRootUI());
	}

	AutomationView::openedInBackground();
}

void AutomationViewArranger::focusRegained() {
	indicator_leds::setLedState(IndicatorLED::BACK, false);
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);
	currentSong->affectEntire = true;
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);
	AutomationView::focusRegained();
}

// used for the play cursor
void AutomationViewArranger::graphicsRoutine() {
	arrangerView.graphicsRoutine();
	AutomationView::graphicsRoutine();
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

	// Auto scrolling
	else if (b == CROSS_SCREEN_EDIT) {
		if (!on && currentUIMode == UI_MODE_NONE) {
			// if another button wasn't pressed while cross screen was held
			if (Buttons::considerCrossScreenReleaseForCrossScreenMode) {
				currentSong->arrangerAutoScrollModeActive = !currentSong->arrangerAutoScrollModeActive;
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, currentSong->arrangerAutoScrollModeActive);

				if (currentSong->arrangerAutoScrollModeActive) {
					arrangerView.reassessWhetherDoingAutoScroll();
				}
				else {
					arrangerView.doingAutoScrollNow = false;
				}
			}
		}
	}

	else {
		return AutomationView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

// pad action
// handles main grid pad actions (e.g. editing, shortcuts) and sidebar pad actions (e.g. status pad)
ActionResult AutomationViewArranger::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (x < kDisplayWidth) {
		// if we're in arranger automation view and holding audition pad, ignore main pad press
		if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
			return ActionResult::DEALT_WITH;
		}

		return editPadAction(x, y, velocity);
	}

	else {
		// don't interact with sidebar if VU Meter is displayed
		if (view.displayVUMeter) {
			return ActionResult::DEALT_WITH;
		}

		// status pad action
		if (x == kDisplayWidth) {
			return arrangerView.handleStatusPadAction(y, velocity, this);
		}

		// audition pad action
		else {
			return arrangerView.handleAuditionPadAction(y, velocity, this);
		}
	}
}

ActionResult AutomationViewArranger::editPadAction(int32_t xDisplay, int32_t yDisplay, int32_t velocity) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];

	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	if (currentSong->lastSelectedParamID != kNoSelection) {
		modelStackWithParam = getModelStackWithParam(modelStackMemory);
	}

	int32_t effectiveLength = getMaxLength();
	int32_t xScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
	int32_t xZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

	// if the user wants to change the parameter they are editing using Shift + Pad shortcut
	// or change the parameter they are editing by press on a shortcut pad on automation overview
	// or they want to enable/disable interpolation
	// or they want to enable/disable pad selection mode
	if (AutomationViewSong::shortcutPadAction(modelStackWithParam, effectiveLength, xDisplay, yDisplay, velocity,
	                                          xScroll, xZoom)) {
		return ActionResult::DEALT_WITH;
	}

	// regular automation editing action
	//	if (isUIModeWithinRange(editPadActionUIModes) && AutomationView::isSquareDefined(xDisplay, xScroll, xZoom)) {

	//    }

	return ActionResult::DEALT_WITH;
}

// defers to arranger sidebar render function
bool AutomationViewArranger::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	return arrangerView.renderSidebar(whichRows, image, occupancyMask);
}

int32_t AutomationViewArranger::getNavSysId() const {
	return NAVIGATION_ARRANGEMENT;
}

uint32_t AutomationViewArranger::getMaxLength() {
	return arrangerView.getMaxLength();
}

uint32_t AutomationViewArranger::getMaxZoom() {
	return arrangerView.getMaxZoom();
}
