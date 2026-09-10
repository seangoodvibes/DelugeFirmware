/*
 * Copyright (c) 2023 Sean Ditny
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

#include "gui/views/automation_view.h"

// namespace deluge::gui::views::automation {

class AutomationEditorLayout {
public:
	AutomationEditorLayout() = default;
	virtual ~AutomationEditorLayout() {}

	// get automationView class info
protected:
	// get AutomationView UI
	inline RootUI* getAutomationView() { return &automation_view_for_session(); }
	inline AutomationParamType& getAutomationParamType() { return automation_view_for_session().automationParamType; }
	inline bool& getOnArrangerView() { return automation_view_for_session().onArrangerView; }
	inline int32_t& getNavSysId() { return automation_view_for_session().navSysId; }

	// display / LED indicators / pad rendering
	inline void renderDisplay(int32_t knobPosLeft = kNoSelection, int32_t knobPosRight = kNoSelection,
	                          bool modEncoderAction = false) {
		automation_view_for_session().renderDisplay(knobPosLeft, knobPosRight, modEncoderAction);
	}
	inline void displayAutomation(bool padSelected = false, bool updateDisplay = true) {
		automation_view_for_session().displayAutomation(padSelected, updateDisplay);
	}
	inline void renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay,
	                                RGB image[][kDisplayWidth + kSideBarWidth],
	                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t imageWidth,
	                                TimelineView* timelineView, bool tripletsOnHere, int32_t xDisplay) {
		automation_view_for_session().renderUndefinedArea(xScroll, xZoom, lengthToDisplay, image, occupancyMask,
		                                                  imageWidth, timelineView, tripletsOnHere, xDisplay);
	}

	// interpolation
	inline void initInterpolation() { automation_view_for_session().initInterpolation(); }
	inline void resetInterpolationShortcutBlinking() {
		automation_view_for_session().resetInterpolationShortcutBlinking();
	}
	inline void blinkInterpolationShortcut() { automation_view_for_session().blinkInterpolationShortcut(); }
	inline bool& getInterpolation() { return automation_view_for_session().interpolation; }
	inline bool& getInterpolationBefore() { return automation_view_for_session().interpolationBefore; }
	inline bool& getInterpolationAfter() { return automation_view_for_session().interpolationAfter; }

	// pad selection mode
	inline bool& getPadSelectionOn() { return automation_view_for_session().padSelectionOn; }
	inline void initPadSelection() { automation_view_for_session().initPadSelection(); }
	inline void blinkPadSelectionShortcut() { automation_view_for_session().blinkPadSelectionShortcut(); }

	// pad press
	inline int32_t getPosFromSquare(int32_t square, int32_t localScroll = -1) const {
		return automation_view_for_session().getPosFromSquare(square, localScroll);
	}
	inline int32_t getPosFromSquare(int32_t square, int32_t xScroll, uint32_t xZoom) const {
		return automation_view_for_session().getPosFromSquare(square, xScroll, xZoom);
	}
	inline bool& getMultiPadPressActive() { return automation_view_for_session().multiPadPressActive; }
	inline bool& getMultiPadPressSelected() { return automation_view_for_session().multiPadPressSelected; }
	inline bool& getMiddlePadPressSelected() { return automation_view_for_session().middlePadPressSelected; }
	inline int32_t& getLeftPadSelectedX() { return automation_view_for_session().leftPadSelectedX; }
	inline int32_t& getLeftPadSelectedY() { return automation_view_for_session().leftPadSelectedY; }
	inline int32_t& getRightPadSelectedX() { return automation_view_for_session().rightPadSelectedX; }
	inline int32_t& getRightPadSelectedY() { return automation_view_for_session().rightPadSelectedY; }
	inline int32_t& getLastPadSelectedKnobPos() { return automation_view_for_session().lastPadSelectedKnobPos; }

	// mod encoder
	inline CopiedParamAutomation* getCopiedParamAutomation() {
		return &automation_view_for_session().copiedParamAutomation;
	}

	// model stack
	inline ModelStackWithAutoParam*
	getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                              int32_t paramID = deluge::modulation::params::kNoParamID,
	                              deluge::modulation::params::Kind paramKind = deluge::modulation::params::Kind::NONE) {
		return automation_view_for_session().getModelStackWithParamForClip(modelStack, clip, paramID, paramKind);
	}
};

// }; // namespace deluge::gui::views::automation
