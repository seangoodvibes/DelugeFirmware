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

#include "gui/views/automation/layout/editor.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/menus.h"
#include "gui/ui/rename/rename_midi_cc_ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/midi/midi_follow.h"
#include "model/action/action_logger.h"
#include "model/instrument/midi_instrument.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "util/comparison.h"

namespace params = deluge::modulation::params;
using deluge::modulation::params::kNoParamID;
using deluge::modulation::params::ParamType;
using deluge::modulation::params::patchedParamShortcuts;
using deluge::modulation::params::unpatchedGlobalParamShortcuts;
using deluge::modulation::params::unpatchedNonGlobalParamShortcuts;

using namespace deluge::gui;

// VU meter style colours for the automation editor

const RGB rowColour[kDisplayHeight] = {{0, 255, 0},   {36, 219, 0}, {73, 182, 0}, {109, 146, 0},
                                       {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0}};

const RGB rowTailColour[kDisplayHeight] = {{2, 53, 2},  {9, 46, 2},  {17, 38, 2}, {24, 31, 2},
                                           {31, 24, 2}, {38, 17, 2}, {46, 9, 2},  {53, 2, 2}};

const RGB rowBlurColour[kDisplayHeight] = {{71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57},
                                           {76, 70, 53},  {77, 60, 48},  {78, 49, 44}, {79, 39, 39}};

const RGB rowBipolarDownColour[kDisplayHeight / 2] = {{255, 0, 0}, {182, 73, 0}, {73, 182, 0}, {0, 255, 0}};

const RGB rowBipolarDownTailColour[kDisplayHeight / 2] = {{53, 2, 2}, {38, 17, 2}, {17, 38, 2}, {2, 53, 2}};

const RGB rowBipolarDownBlurColour[kDisplayHeight / 2] = {{79, 39, 39}, {77, 60, 48}, {73, 90, 62}, {71, 111, 71}};

// colours for the velocity editor

const RGB velocityRowColour[kDisplayHeight] = {{0, 0, 255},   {36, 0, 219}, {73, 0, 182}, {109, 0, 146},
                                               {146, 0, 109}, {182, 0, 73}, {219, 0, 36}, {255, 0, 0}};

const RGB velocityRowTailColour[kDisplayHeight] = {{2, 2, 53},  {9, 2, 46},  {17, 2, 38}, {24, 2, 31},
                                                   {31, 2, 24}, {38, 2, 17}, {46, 2, 9},  {53, 2, 2}};

const RGB velocityRowBlurColour[kDisplayHeight] = {{71, 71, 111}, {72, 66, 101}, {73, 62, 90}, {74, 57, 80},
                                                   {76, 53, 70},  {77, 48, 60},  {78, 44, 49}, {79, 39, 39}};

// lookup tables for the values that are set when you press the pads in each row of the grid
const int32_t nonPatchCablePadPressValues[kDisplayHeight] = {0, 18, 37, 55, 73, 91, 110, 128};
const int32_t patchCablePadPressValues[kDisplayHeight] = {-128, -90, -60, -30, 30, 60, 90, 128};

// lookup tables for the min value of each pad's value range used to display automation on each row of the grid
const int32_t nonPatchCableMinPadDisplayValues[kDisplayHeight] = {0, 17, 33, 49, 65, 81, 97, 113};
const int32_t patchCableMinPadDisplayValues[kDisplayHeight] = {-128, -96, -64, -32, 1, 33, 65, 97};

// lookup tables for the max value of each pad's value range used to display automation on each row of the grid
const int32_t nonPatchCableMaxPadDisplayValues[kDisplayHeight] = {16, 32, 48, 64, 80, 96, 112, 128};
const int32_t patchCableMaxPadDisplayValues[kDisplayHeight] = {-97, -65, -33, -1, 32, 64, 96, 128};

// summary of pad ranges and press values (format: MIN < PRESS < MAX)
// patch cable:
// y = 7 ::   97 <  128 < 128
// y = 6 ::   65 <   90 <  96
// y = 5 ::   33 <   60 <  64
// y = 4 ::    1 <   30 <  32
// y = 3 ::  -32 <  -30 <  -1
// y = 2 ::  -64 <  -60 < -33
// y = 1 ::  -96 <  -90 < -65
// y = 0 :: -128 < -128 < -97

// non-patch cable:
// y = 7 :: 113 < 128 < 128
// y = 6 ::  97 < 110 < 112
// y = 5 ::  81 <  91 <  96
// y = 4 ::  65 <  73 <  80
// y = 3 ::  49 <  55 <  64
// y = 2 ::  33 <  37 <  48
// y = 1 ::  17 <  18 <  32
// y = 0 ::  0  <   0 <  16

constexpr int32_t kParamNodeWidth = 3;

AutomationLayoutEditor automationLayoutEditor{};

AutomationLayoutEditor::AutomationLayoutEditor() {
}

// gets the length of the clip, renders the pads corresponding to current parameter values set up to the
// clip length renders the undefined area of the clip that the user can't interact with
void AutomationLayoutEditor::renderAutomationEditor(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                    RGB image[][kDisplayWidth + kSideBarWidth],
                                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                    int32_t renderWidth, int32_t xScroll, uint32_t xZoom,
                                                    int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea,
                                                    params::Kind kind, bool isBipolar) {
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		renderAutomationColumn(modelStackWithParam, image, occupancyMask, effectiveLength, xDisplay,
		                       modelStackWithParam->autoParam->isAutomated(), xScroll, xZoom, kind, isBipolar);
	}
	if (drawUndefinedArea) {
		renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth, &automationView,
		                    currentSong->tripletsOn, xDisplay);
	}
}

/// render each square in each column of the automation editor grid
void AutomationLayoutEditor::renderAutomationColumn(ModelStackWithAutoParam* modelStackWithParam,
                                                    RGB image[][kDisplayWidth + kSideBarWidth],
                                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                    int32_t lengthToDisplay, int32_t xDisplay, bool isAutomated,
                                                    int32_t xScroll, int32_t xZoom, params::Kind kind, bool isBipolar) {

	uint32_t squareStart = getMiddlePosFromSquare(xDisplay, lengthToDisplay, xScroll, xZoom);
	int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;

	// iterate through each square
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (isBipolar) {
			renderAutomationBipolarSquare(image, occupancyMask, xDisplay, yDisplay, isAutomated, kind, knobPos);
		}
		else {
			renderAutomationUnipolarSquare(image, occupancyMask, xDisplay, yDisplay, isAutomated, knobPos);
		}
	}
}

/// render column for bipolar params - e.g. pan, pitch, patch cable
void AutomationLayoutEditor::renderAutomationBipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
                                                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                           int32_t xDisplay, int32_t yDisplay, bool isAutomated,
                                                           params::Kind kind, int32_t knobPos) {
	RGB& pixel = image[yDisplay][xDisplay];

	int32_t middleKnobPos;

	// for patch cable that has a range of -128 to + 128, the middle point is 0
	if (kind == params::Kind::PATCH_CABLE) {
		middleKnobPos = 0;
	}
	// for non-patch cable that has a range of 0 to 128, the middle point is 64
	else {
		middleKnobPos = 64;
	}

	// if it's bipolar, only render grid rows above or below middle value
	if (((knobPos > middleKnobPos) && (yDisplay < 4)) || ((knobPos < middleKnobPos) && (yDisplay > 3))) {
		pixel = colours::black; // erase pad
		return;
	}

	bool doRender = false;

	// determine whether or not you should render a row based on current value
	if (knobPos != middleKnobPos) {
		if (kind == params::Kind::PATCH_CABLE) {
			if (knobPos > middleKnobPos) {
				doRender = (knobPos >= patchCableMinPadDisplayValues[yDisplay]);
			}
			else {
				doRender = (knobPos <= patchCableMaxPadDisplayValues[yDisplay]);
			}
		}
		else {
			if (knobPos > middleKnobPos) {
				doRender = (knobPos >= nonPatchCableMinPadDisplayValues[yDisplay]);
			}
			else {
				doRender = (knobPos <= nonPatchCableMaxPadDisplayValues[yDisplay]);
			}
		}
	}

	// render automation lane
	if (doRender) {
		if (isAutomated) { // automated, render bright colour
			if (knobPos > middleKnobPos) {
				pixel = rowBipolarDownColour[-yDisplay + 7];
			}
			else {
				pixel = rowBipolarDownColour[yDisplay];
			}
		}
		else { // not automated, render less bright tail colour
			if (knobPos > middleKnobPos) {
				pixel = rowBipolarDownTailColour[-yDisplay + 7];
			}
			else {
				pixel = rowBipolarDownTailColour[yDisplay];
			}
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
	else {
		pixel = colours::black; // erase pad
	}

	// pad selection mode, render cursor
	if (automationLayout.padSelectionOn
	    && ((xDisplay == automationLayout.leftPadSelectedX) || (xDisplay == automationLayout.rightPadSelectedX))) {
		if (doRender) {
			if (knobPos > middleKnobPos) {
				pixel = rowBipolarDownBlurColour[-yDisplay + 7];
			}
			else {
				pixel = rowBipolarDownBlurColour[yDisplay];
			}
		}
		else {
			pixel = colours::grey;
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
}

/// render column for unipolar params (e.g. not pan, pitch, or patch cables)
void AutomationLayoutEditor::renderAutomationUnipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
                                                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                            int32_t xDisplay, int32_t yDisplay, bool isAutomated,
                                                            int32_t knobPos) {
	RGB& pixel = image[yDisplay][xDisplay];

	// determine whether or not you should render a row based on current value
	bool doRender = false;
	if (knobPos) {
		doRender = (knobPos >= nonPatchCableMinPadDisplayValues[yDisplay]);
	}

	// render square
	if (doRender) {
		if (isAutomated) { // automated, render bright colour
			pixel = rowColour[yDisplay];
		}
		else { // not automated, render less bright tail colour
			pixel = rowTailColour[yDisplay];
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
	else {
		pixel = colours::black; // erase pad
	}

	// pad selection mode, render cursor
	if (automationLayout.padSelectionOn
	    && ((xDisplay == automationLayout.leftPadSelectedX) || (xDisplay == automationLayout.rightPadSelectedX))) {
		if (doRender) {
			pixel = rowBlurColour[yDisplay];
		}
		else {
			pixel = colours::grey;
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
}

// gets the length of the note row, renders the pads corresponding to current note parameter values set up to the
// note row length renders the undefined area of the note row that the user can't interact with
void AutomationLayoutEditor::renderNoteEditor(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
                                              RGB image[][kDisplayWidth + kSideBarWidth],
                                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                              int32_t renderWidth, int32_t xScroll, uint32_t xZoom,
                                              int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea,
                                              SquareInfo& squareInfo) {
	if (modelStackWithNoteRow->getNoteRowAllowNull()) {
		renderNoteColumn(modelStackWithNoteRow, clip, image, occupancyMask, xDisplay, xScroll, xZoom, squareInfo);
	}
	if (drawUndefinedArea) {
		renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth, &automationView,
		                    currentSong->tripletsOn, xDisplay);
	}
}

/// render each square in each column of the note editor grid
void AutomationLayoutEditor::renderNoteColumn(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
                                              RGB image[][kDisplayWidth + kSideBarWidth],
                                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
                                              int32_t xScroll, int32_t xZoom, SquareInfo& squareInfo) {
	int32_t value = 0;

	if (automationView.automationParamType == AutomationParamType::NOTE_VELOCITY) {
		value = squareInfo.averageVelocity;
	}

	// iterate through each square
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		renderNoteSquare(image, occupancyMask, xDisplay, yDisplay, squareInfo.squareType, value);
	}
}

/// render column for note parameter
void AutomationLayoutEditor::renderNoteSquare(RGB image[][kDisplayWidth + kSideBarWidth],
                                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
                                              int32_t yDisplay, uint8_t squareType, int32_t value) {
	RGB& pixel = image[yDisplay][xDisplay];
	bool doRender = false;

	if (squareType == SQUARE_NO_NOTE) {
		pixel = colours::black; // erase pad
	}
	else {
		// render square
		if (value >= nonPatchCableMinPadDisplayValues[yDisplay]) {
			doRender = true;
			if (squareType == SQUARE_NOTE_HEAD) {
				pixel = velocityRowColour[yDisplay];
			}
			else if (squareType == SQUARE_NOTE_TAIL) {
				pixel = velocityRowTailColour[yDisplay];
			}
			else if (squareType == SQUARE_BLURRED) {
				pixel = velocityRowBlurColour[yDisplay];
			}
			occupancyMask[yDisplay][xDisplay] = 64;
		}
		else {
			pixel = colours::black; // erase pad
		}
	}
}

// occupancyMask now optional
void AutomationLayoutEditor::renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay,
                                                 RGB image[][kDisplayWidth + kSideBarWidth],
                                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                 int32_t imageWidth, TimelineView* timelineView, bool tripletsOnHere,
                                                 int32_t xDisplay) {
	// If the visible pane extends beyond the end of the Clip, draw it as grey
	int32_t greyStart = timelineView->getSquareFromPos(lengthToDisplay - 1, NULL, xScroll, xZoom) + 1;

	if (greyStart < 0) {
		greyStart = 0; // This actually happened in a song of Marek's, due to another bug, but best to check
		               // for this
	}

	if (greyStart <= xDisplay) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			image[yDisplay][xDisplay] = colours::grey;
			occupancyMask[yDisplay][xDisplay] = 64;
		}
	}

	if (tripletsOnHere && timelineView->supportsTriplets()) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (!timelineView->isSquareDefined(xDisplay, xScroll, xZoom)) {
				image[yDisplay][xDisplay] = colours::grey;

				if (occupancyMask) {
					occupancyMask[yDisplay][xDisplay] = 64;
				}
			}
		}
	}
}

void AutomationLayoutEditor::renderAutomationEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                               Clip* clip, OutputType outputType, int32_t knobPosLeft,
                                                               int32_t knobPosRight) {
	// display parameter name
	DEF_STACK_STRING_BUF(parameterName, 30);
	getAutomationParameterName(clip, outputType, parameterName);

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	canvas.drawStringCentredShrinkIfNecessary(parameterName.c_str(), yPos, kTextSpacingX, kTextSpacingY);

	// display automation status
	yPos = yPos + 12;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (automationView.onArrangerView) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = automationLayout.getModelStackWithParamForClip(modelStack, clip);
	}

	char const* isAutomated;

	// check if Parameter is currently automated so that the automation status can be drawn on
	// the screen with the Parameter Name
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		if (modelStackWithParam->autoParam->isAutomated()) {
			isAutomated = l10n::get(l10n::String::STRING_FOR_AUTOMATION_ON);
		}
		else {
			isAutomated = l10n::get(l10n::String::STRING_FOR_AUTOMATION_OFF);
		}
	}

	canvas.drawStringCentred(isAutomated, yPos, kTextSpacingX, kTextSpacingY);

	// display parameter value
	yPos = yPos + 12;

	if (knobPosRight != kNoSelection) {
		char bufferLeft[10];
		bufferLeft[0] = 'L';
		bufferLeft[1] = ':';
		bufferLeft[2] = ' ';
		intToString(knobPosLeft, &bufferLeft[3]);
		canvas.drawString(bufferLeft, 0, yPos, kTextSpacingX, kTextSpacingY);

		char bufferRight[10];
		bufferRight[0] = 'R';
		bufferRight[1] = ':';
		bufferRight[2] = ' ';
		intToString(knobPosRight, &bufferRight[3]);
		canvas.drawStringAlignRight(bufferRight, yPos, kTextSpacingX, kTextSpacingY);
	}
	else {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		canvas.drawStringCentred(buffer, yPos, kTextSpacingX, kTextSpacingY);
	}
}

void AutomationLayoutEditor::renderNoteEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                         InstrumentClip* clip, OutputType outputType,
                                                         int32_t knobPosLeft, int32_t knobPosRight) {
	// display note parameter name
	DEF_STACK_STRING_BUF(parameterName, 30);
	if (automationView.automationParamType == AutomationParamType::NOTE_VELOCITY) {
		parameterName.append("Velocity");
	}

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	canvas.drawStringCentredShrinkIfNecessary(parameterName.c_str(), yPos, kTextSpacingX, kTextSpacingY);

	// display note / drum name
	yPos = yPos + 12;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	bool isKit = outputType == OutputType::KIT;

	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
	                                                                        modelStack); // don't create
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (!isKit) {
			modelStackWithNoteRow =
			    instrumentClipView.createNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);
		}
	}

	char noteRowName[50];

	if (modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (isKit) {
			DEF_STACK_STRING_BUF(drumName, 50);
			instrumentClipView.getDrumName(modelStackWithNoteRow->getNoteRow()->drum, drumName);
			strncpy(noteRowName, drumName.c_str(), 49);
		}
		else {
			int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
			noteCodeToString(modelStackWithNoteRow->getNoteRow()->getNoteCode(), noteRowName, &isNatural);
		}
	}
	else {
		if (isKit) {
			strncpy(noteRowName, "(Select Drum)", 49);
		}
		else {
			strncpy(noteRowName, "(Select Note)", 49);
		}
	}

	canvas.drawStringCentred(noteRowName, yPos, kTextSpacingX, kTextSpacingY);

	// display parameter value
	yPos = yPos + 12;

	if (automationView.automationParamType == AutomationParamType::NOTE_VELOCITY) {
		if (knobPosRight != kNoSelection) {
			char bufferLeft[10];
			bufferLeft[0] = 'L';
			bufferLeft[1] = ':';
			bufferLeft[2] = ' ';
			intToString(knobPosLeft, &bufferLeft[3]);
			canvas.drawString(bufferLeft, 0, yPos, kTextSpacingX, kTextSpacingY);

			char bufferRight[10];
			bufferRight[0] = 'R';
			bufferRight[1] = ':';
			bufferRight[2] = ' ';
			intToString(knobPosRight, &bufferRight[3]);
			canvas.drawStringAlignRight(bufferRight, yPos, kTextSpacingX, kTextSpacingY);
		}
		else if (knobPosLeft != kNoSelection) {
			char buffer[5];
			intToString(knobPosLeft, buffer);
			canvas.drawStringCentred(buffer, yPos, kTextSpacingX, kTextSpacingY);
		}
		else {
			char buffer[5];
			intToString(getCurrentInstrument()->defaultVelocity, buffer);
			canvas.drawStringCentred(buffer, yPos, kTextSpacingX, kTextSpacingY);
		}
	}
}

void AutomationLayoutEditor::renderAutomationEditorDisplay7SEG(Clip* clip, OutputType outputType, int32_t knobPosLeft,
                                                               bool modEncoderAction) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (automationView.onArrangerView) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithParam = automationLayout.getModelStackWithParamForClip(modelStack, clip);
	}

	bool padSelected =
	    (!automationLayout.padSelectionOn && isUIModeActive(UI_MODE_NOTES_PRESSED)) || automationLayout.padSelectionOn;

	/* check if you're holding a pad
	 * if yes, store pad press knob position in lastPadSelectedKnobPos
	 * so that it can be used next time as the knob position if returning here
	 * to display parameter value after another popup has been cancelled (e.g. audition pad)
	 */
	if (padSelected) {
		if (knobPosLeft != kNoSelection) {
			automationLayout.lastPadSelectedKnobPos = knobPosLeft;
		}
		else if (automationLayout.lastPadSelectedKnobPos != kNoSelection) {
			params::Kind lastSelectedParamKind = params::Kind::NONE;
			int32_t lastSelectedParamID = kNoSelection;
			if (automationView.onArrangerView) {
				lastSelectedParamKind = currentSong->lastSelectedParamKind;
				lastSelectedParamID = currentSong->lastSelectedParamID;
			}
			else {
				lastSelectedParamKind = clip->lastSelectedParamKind;
				lastSelectedParamID = clip->lastSelectedParamID;
			}
			knobPosLeft = view.calculateKnobPosForDisplay(lastSelectedParamKind, lastSelectedParamID,
			                                              automationLayout.lastPadSelectedKnobPos);
		}
	}

	bool isAutomated =
	    modelStackWithParam && modelStackWithParam->autoParam && modelStackWithParam->autoParam->isAutomated();
	bool playbackStarted = playbackHandler.isEitherClockActive();

	// display parameter value if knobPos is provided
	if ((knobPosLeft != kNoSelection) && (padSelected || (playbackStarted && isAutomated) || modEncoderAction)) {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		if (modEncoderAction && !padSelected) {
			display->displayPopup(buffer, 3, true);
		}
		else {
			display->setText(buffer, true, 255, false);
		}
	}
	// display parameter name
	else if (knobPosLeft == kNoSelection) {
		DEF_STACK_STRING_BUF(parameterName, 30);
		getAutomationParameterName(clip, outputType, parameterName);
		// if playback is running and there is automation, the screen will display the
		// current automation value at the playhead position
		// when changing to a parameter with automation, flash the parameter name first
		// before the value is displayed
		// otherwise if there's no automation, just scroll the parameter name
		if (padSelected || (playbackStarted && isAutomated)) {
			display->displayPopup(parameterName.c_str(), 3, true, isAutomated ? 3 : 255);
		}
		else {
			display->setScrollingText(parameterName.c_str(), 0, 600, -1, isAutomated ? 3 : 255);
		}
	}
}

void AutomationLayoutEditor::renderNoteEditorDisplay7SEG(InstrumentClip* clip, OutputType outputType,
                                                         int32_t knobPosLeft) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	bool isKit = outputType == OutputType::KIT;

	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
	                                                                        modelStack); // don't create
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (!isKit) {
			modelStackWithNoteRow =
			    instrumentClipView.createNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);
		}
	}

	if (knobPosLeft != kNoSelection) {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		display->setText(buffer, true, 255, false);
	}
	else {
		// display note / drum name
		char noteRowName[50];
		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			if (isKit) {
				DEF_STACK_STRING_BUF(drumName, 50);
				instrumentClipView.getDrumName(modelStackWithNoteRow->getNoteRow()->drum, drumName);
				strncpy(noteRowName, drumName.c_str(), 49);
			}
			else {
				int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
				noteCodeToString(modelStackWithNoteRow->getNoteRow()->getNoteCode(), noteRowName, &isNatural);
			}
		}
		else {
			if (isKit) {
				strncpy(noteRowName, "(Select Drum)", 49);
			}
			else {
				strncpy(noteRowName, "(Select Note)", 49);
			}
		}
		display->setScrollingText(noteRowName);
	}
}

// get's the name of the Parameter being edited so it can be displayed on the screen
void AutomationLayoutEditor::getAutomationParameterName(Clip* clip, OutputType outputType, StringBuf& parameterName) {
	if (outputType != OutputType::MIDI_OUT) {
		params::Kind lastSelectedParamKind = params::Kind::NONE;
		int32_t lastSelectedParamID = kNoSelection;
		PatchSource lastSelectedPatchSource = PatchSource::NONE;
		if (automationView.onArrangerView) {
			lastSelectedParamKind = currentSong->lastSelectedParamKind;
			lastSelectedParamID = currentSong->lastSelectedParamID;
		}
		else {
			lastSelectedParamKind = clip->lastSelectedParamKind;
			lastSelectedParamID = clip->lastSelectedParamID;
			lastSelectedPatchSource = clip->lastSelectedPatchSource;
		}
		if (lastSelectedParamKind == params::Kind::PATCH_CABLE) {
			PatchSource source2 = PatchSource::NONE;
			ParamDescriptor paramDescriptor;
			paramDescriptor.data = lastSelectedParamID;
			if (!paramDescriptor.hasJustOneSource()) {
				source2 = paramDescriptor.getTopLevelSource();
			}

			parameterName.append(sourceToStringShort(lastSelectedPatchSource));

			if (display->haveOLED()) {
				parameterName.append(" -> ");
			}
			else {
				parameterName.append(" - ");
			}

			if (source2 != PatchSource::NONE) {
				parameterName.append(sourceToStringShort(source2));
				parameterName.append(display->haveOLED() ? " -> " : " - ");
			}

			parameterName.append(params::getPatchedParamShortName(lastSelectedParamID));
		}
		else {
			parameterName.append(getParamDisplayName(lastSelectedParamKind, lastSelectedParamID));
		}
	}
	else {
		if (clip->lastSelectedParamID == CC_NUMBER_NONE) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_PARAM));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_PITCH_BEND) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_AFTERTOUCH) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
		}
		else if (clip->lastSelectedParamID == CC_EXTERNAL_MOD_WHEEL || clip->lastSelectedParamID == CC_NUMBER_Y_AXIS) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
		}
		else {
			MIDIInstrument* midiInstrument = (MIDIInstrument*)clip->output;
			bool appendedName = false;

			if (clip->lastSelectedParamID >= 0 && clip->lastSelectedParamID < kNumRealCCNumbers) {
				String* name = midiInstrument->getNameFromCC(clip->lastSelectedParamID);
				// if we have a name for this midi cc set by the user, display that instead of the cc number
				if (name && !name->isEmpty()) {
					parameterName.append(name->get());
					appendedName = true;
				}
			}

			// if we don't have a midi cc name set, draw CC number instead
			if (!appendedName) {
				if (display->haveOLED()) {
					parameterName.append("CC ");
					parameterName.appendInt(clip->lastSelectedParamID);
				}
				else {
					if (clip->lastSelectedParamID < 100) {
						parameterName.append("CC");
					}
					else {
						parameterName.append("C");
					}
					parameterName.appendInt(clip->lastSelectedParamID);
				}
			}
		}
	}
}

/// if we're entering note editor, we want the selected drum to be visible and in sync with lastAuditionedYDisplay
/// so we'll check if the yDisplay of the selectedDrum is in sync with the lastAuditionedYDisplay
/// if they're not in sync, we'll sync them up by performing a vertical scroll
void AutomationLayoutEditor::potentiallyVerticalScrollToSelectedDrum(InstrumentClip* clip, Output* output) {
	if (output->type == OutputType::KIT) {
		int32_t noteRowIndex;
		Drum* selectedDrum = ((Kit*)output)->selectedDrum;
		if (selectedDrum) {
			NoteRow* noteRow = clip->getNoteRowForDrum(selectedDrum, &noteRowIndex);
			if (noteRow) {
				int32_t lastAuditionedYDisplayScrolled = instrumentClipView.lastAuditionedYDisplay + clip->yScroll;
				if (noteRowIndex != lastAuditionedYDisplayScrolled) {
					int32_t yScrollAdjustment = noteRowIndex - lastAuditionedYDisplayScrolled;
					automationLayout.scrollVertical(yScrollAdjustment);
				}
			}
		}
	}
}

uint32_t AutomationLayoutEditor::getSquareWidth(int32_t square, int32_t effectiveLength, int32_t xScroll,
                                                int32_t xZoom) {
	int32_t squareRightEdge = automationView.getPosFromSquare(square + 1, xScroll, xZoom);
	return std::min(effectiveLength, squareRightEdge) - automationView.getPosFromSquare(square, xScroll, xZoom);
}

// when pressing on a single pad, you want to display the value of the middle node within that square
// as that is the most accurate value that represents that square
uint32_t AutomationLayoutEditor::getMiddlePosFromSquare(int32_t xDisplay, int32_t effectiveLength, int32_t xScroll,
                                                        int32_t xZoom) {
	uint32_t squareStart = automationView.getPosFromSquare(xDisplay, xScroll, xZoom);
	uint32_t squareWidth = getSquareWidth(xDisplay, effectiveLength, xScroll, xZoom);
	if (squareWidth != 3) {
		squareStart = squareStart + (squareWidth / 2);
	}

	return squareStart;
}

// this function obtains a parameters value and converts it to a knobPos
// the knobPos is used for rendering the current parameter values in the automation editor
// it's also used for obtaining the start and end position values for a multi pad press
// and also used for increasing/decreasing parameter values with the mod encoders
int32_t AutomationLayoutEditor::getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack,
                                                              uint32_t squareStart) {
	// obtain value corresponding to the two pads that were pressed in a multi pad press action
	int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareStart, modelStack);
	int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(currentValue, modelStack);

	return knobPos;
}

// this function is based off the code in AutoParam::getValueAtPos, it was tweaked to just return
// interpolation status of the left node or right node (depending on the reversed parameter which is
// used to indicate what node in what direction we are looking for (e.g. we want status of left node, or
// right node, relative to the current pos we are looking at
bool AutomationLayoutEditor::getAutomationNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos,
                                                            bool reversed) {

	if (!modelStack->autoParam->nodes.getNumElements()) {
		return false;
	}

	int32_t rightI = modelStack->autoParam->nodes.search(pos + (int32_t)!reversed, GREATER_OR_EQUAL);
	if (rightI >= modelStack->autoParam->nodes.getNumElements()) {
		rightI = 0;
	}
	ParamNode* rightNode = modelStack->autoParam->nodes.getElement(rightI);

	int32_t leftI = rightI - 1;
	if (leftI < 0) {
		leftI += modelStack->autoParam->nodes.getNumElements();
	}
	ParamNode* leftNode = modelStack->autoParam->nodes.getElement(leftI);

	if (reversed) {
		return leftNode->interpolated;
	}
	else {
		return rightNode->interpolated;
	}
}

// this function writes the new values calculated by the handleAutomationSinglePadPress and
// handleAutomationMultiPadPress functions
void AutomationLayoutEditor::setAutomationParameterValue(ModelStackWithAutoParam* modelStack, int32_t knobPos,
                                                         int32_t squareStart, int32_t xDisplay, int32_t effectiveLength,
                                                         int32_t xScroll, int32_t xZoom, bool modEncoderAction) {

	int32_t newValue = modelStack->paramCollection->knobPosToParamValue(knobPos, modelStack);

	uint32_t squareWidth = 0;

	// for a multi pad press, the beginning and ending pad presses are set with a square width of 3 (1
	// node).
	if (automationLayout.multiPadPressSelected) {
		squareWidth = kParamNodeWidth;
	}
	else {
		squareWidth = automationLayoutEditor.getSquareWidth(xDisplay, effectiveLength, xScroll, xZoom);
	}

	// if you're doing a single pad press, you don't want the values around that single press position
	// to change they will change if those nodes around the single pad press were created with
	// interpolation turned on to fix this, re-create those nodes with their current value with
	// interpolation off

	interpolationBefore = automationLayoutEditor.getAutomationNodeInterpolation(modelStack, squareStart, true);
	interpolationAfter = automationLayoutEditor.getAutomationNodeInterpolation(modelStack, squareStart, false);

	// create a node to the left with the current interpolation status
	int32_t squareNodeLeftStart = squareStart - kParamNodeWidth;
	if (squareNodeLeftStart >= 0) {
		int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareNodeLeftStart, modelStack);
		modelStack->autoParam->setValuePossiblyForRegion(currentValue, modelStack, squareNodeLeftStart,
		                                                 kParamNodeWidth);
	}

	// create a node to the right with the current interpolation status
	int32_t squareNodeRightStart = squareStart + kParamNodeWidth;
	if (squareNodeRightStart < effectiveLength) {
		int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareNodeRightStart, modelStack);
		modelStack->autoParam->setValuePossiblyForRegion(currentValue, modelStack, squareNodeRightStart,
		                                                 kParamNodeWidth);
	}

	// reset interpolation to false for the single pad we're changing (so that the nodes around it don't
	// also change)
	initInterpolation();

	// called twice because there was a weird bug where for some reason the first call wasn't taking
	// effect on one pad (and whatever pad it was changed every time)...super weird...calling twice
	// fixed it...
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);

	if (!automationView.onArrangerView) {
		modelStack->getTimelineCounter()->instrumentBeenEdited();
	}

	// in a multi pad press, no need to display all the values calculated
	if (!automationLayout.multiPadPressSelected) {
		int32_t newKnobPos = knobPos + kKnobPosOffset;
		automationLayout.renderDisplay(newKnobPos, kNoSelection, modEncoderAction);
		automationLayout.setAutomationKnobIndicatorLevels(modelStack, newKnobPos, newKnobPos);
	}

	// midi follow and midi feedback enabled
	// re-send midi cc because learned parameter value has changed
	view.sendMidiFollowFeedback(modelStack, knobPos);
}

void AutomationLayoutEditor::initInterpolation() {

	interpolationBefore = false;
	interpolationAfter = false;
}

// automation edit pad action
// handles single and multi pad presses for automation editing
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationLayoutEditor::automationEditPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                     int32_t xDisplay, int32_t yDisplay, int32_t velocity,
                                                     int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	if (automationLayout.padSelectionOn) {
		automationLayout.selectedPadPressed = velocity;
	}
	// If button down
	if (velocity) {
		// If this is a automation-length-edit press...
		// needed for Automation
		if (instrumentClipView.numEditPadPresses == 1) {

			int32_t firstPadX = 255;
			int32_t firstPadY = 255;

			// Find that original press
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (instrumentClipView.editPadPresses[i].isActive) {

					firstPadX = instrumentClipView.editPadPresses[i].xDisplay;
					firstPadY = instrumentClipView.editPadPresses[i].yDisplay;

					break;
				}
			}

			if (firstPadX != 255 && firstPadY != 255) {
				if (firstPadX != xDisplay) {
					recordAutomationSinglePadPress(xDisplay, yDisplay);

					automationLayout.multiPadPressSelected = true;
					automationLayout.multiPadPressActive = true;

					// the long press logic calculates and renders the interpolation as if the press was
					// entered in a forward fashion (where the first pad is to the left of the second
					// pad). if the user happens to enter a long press backwards then we fix that entry
					// by re-ordering the pad presses so that it is forward again
					automationLayout.leftPadSelectedX = firstPadX > xDisplay ? xDisplay : firstPadX;
					automationLayout.leftPadSelectedY = firstPadX > xDisplay ? yDisplay : firstPadY;
					automationLayout.rightPadSelectedX = firstPadX > xDisplay ? firstPadX : xDisplay;
					automationLayout.rightPadSelectedY = firstPadX > xDisplay ? firstPadY : yDisplay;

					// if you're not in pad selection mode, allow user to enter a long press
					if (!automationLayout.padSelectionOn) {
						automationLayout.handleAutomationMultiPadPress(
						    modelStackWithParam, clip, automationLayout.leftPadSelectedX,
						    automationLayout.leftPadSelectedY, automationLayout.rightPadSelectedX,
						    automationLayout.rightPadSelectedY, effectiveLength, xScroll, xZoom);
					}
					else {
						uiNeedsRendering(&automationView);
					}

					// set led indicators to left / right pad selection values
					// and update display
					automationLayout.renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength,
					                                                         xScroll, xZoom, xDisplay);
				}
				else {
					automationLayout.leftPadSelectedY = firstPadY;
					automationLayout.middlePadPressSelected = true;
					goto singlePadPressAction;
				}
			}
		}

		// Or, if this is a regular create-or-select press...
		else {
singlePadPressAction:
			if (recordAutomationSinglePadPress(xDisplay, yDisplay)) {
				automationLayout.multiPadPressActive = false;
				automationLayout.handleAutomationSinglePadPress(modelStackWithParam, clip, xDisplay, yDisplay,
				                                                effectiveLength, xScroll, xZoom);
			}
		}
	}

	// Or if pad press ended...
	else {
		// Find the corresponding press, if there is one
		int32_t i;
		for (i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive
			    && instrumentClipView.editPadPresses[i].yDisplay == yDisplay
			    && instrumentClipView.editPadPresses[i].xDisplay == xDisplay) {
				break;
			}
		}

		// If we found it...
		if (i < kEditPadPressBufferSize) {
			instrumentClipView.endEditPadPress(i);

			instrumentClipView.checkIfAllEditPadPressesEnded();
		}

		// outside pad selection mode, exit multi pad press once you've let go of the first pad in the
		// long press
		if (!automationLayout.padSelectionOn && automationLayout.multiPadPressSelected
		    && (currentUIMode != UI_MODE_NOTES_PRESSED)) {
			automationLayout.initPadSelection();
		}
		// switch from long press selection to short press selection in pad selection mode
		else if (automationLayout.padSelectionOn && automationLayout.multiPadPressSelected
		         && !automationLayout.multiPadPressActive && (currentUIMode != UI_MODE_NOTES_PRESSED)
		         && ((AudioEngine::audioSampleTimer - instrumentClipView.timeLastEditPadPress) < kShortPressTime)) {

			automationLayout.multiPadPressSelected = false;

			automationLayout.leftPadSelectedX = xDisplay;
			automationLayout.rightPadSelectedX = kNoSelection;

			uiNeedsRendering(&automationView);
		}

		if (currentUIMode != UI_MODE_NOTES_PRESSED) {
			automationLayout.lastPadSelectedKnobPos = kNoSelection;
			if (automationLayout.multiPadPressSelected) {
				automationLayout.renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength,
				                                                         xScroll, xZoom, xDisplay);
			}
			else if (!automationLayout.padSelectionOn && !playbackHandler.isEitherClockActive()) {
				displayAutomation();
			}
		}

		automationLayout.middlePadPressSelected = false;
	}
}

bool AutomationLayoutEditor::recordAutomationSinglePadPress(int32_t xDisplay, int32_t yDisplay) {
	instrumentClipView.timeLastEditPadPress = AudioEngine::audioSampleTimer;
	// Find an empty space in the press buffer, if there is one
	int32_t i;
	for (i = 0; i < kEditPadPressBufferSize; i++) {
		if (!instrumentClipView.editPadPresses[i].isActive) {
			break;
		}
	}
	if (i < kEditPadPressBufferSize) {
		instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;

		// If this is the first press, record the time
		if (instrumentClipView.numEditPadPresses == 0) {
			instrumentClipView.timeFirstEditPadPress = AudioEngine::audioSampleTimer;
			instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
		}

		instrumentClipView.editPadPresses[i].isActive = true;
		instrumentClipView.editPadPresses[i].yDisplay = yDisplay;
		instrumentClipView.editPadPresses[i].xDisplay = xDisplay;
		instrumentClipView.numEditPadPresses++;
		instrumentClipView.numEditPadPressesPerNoteRowOnScreen[yDisplay]++;
		enterUIMode(UI_MODE_NOTES_PRESSED);

		return true;
	}
	return false;
}

/// toggle automation interpolation on / off
bool AutomationLayoutEditor::toggleAutomationInterpolation() {
	if (interpolation) {
		interpolation = false;
		initInterpolation();
		automationLayout.resetInterpolationShortcutBlinking();

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_DISABLED));
	}
	else {
		interpolation = true;
		automationLayout.blinkInterpolationShortcut();

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_ENABLED));
	}
	return true;
}

/// toggle automation pad selection mode on / off
bool AutomationLayoutEditor::toggleAutomationPadSelectionMode(ModelStackWithAutoParam* modelStackWithParam,
                                                              int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	// enter/exit pad selection mode
	if (automationLayout.padSelectionOn) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_OFF));

		automationLayout.initPadSelection();
		displayAutomation(true, !display->have7SEG());
	}
	else {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_ON));

		automationLayout.padSelectionOn = true;
		automationLayout.blinkPadSelectionShortcut();

		automationLayout.multiPadPressSelected = false;
		automationLayout.multiPadPressActive = false;

		// display only left cursor initially
		automationLayout.leftPadSelectedX = 0;
		automationLayout.rightPadSelectedX = kNoSelection;

		uint32_t squareStart =
		    getMiddlePosFromSquare(automationLayout.leftPadSelectedX, effectiveLength, xScroll, xZoom);

		automationLayout.updateAutomationModPosition(modelStackWithParam, squareStart, true, true);
	}
	uiNeedsRendering(&automationView);
	return true;
}

// note edit pad action
// handles single and multi pad presses for note parameter editing (e.g. velocity)
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationLayoutEditor::noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                               InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity,
                                               int32_t effectiveLength, SquareInfo& squareInfo) {
	if (automationView.automationParamType == AutomationParamType::NOTE_VELOCITY) {
		velocityEditPadAction(modelStackWithNoteRow, noteRow, clip, x, y, velocity, effectiveLength, squareInfo);
	}
}

// velocity edit pad action
void AutomationLayoutEditor::velocityEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                                   InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity,
                                                   int32_t effectiveLength, SquareInfo& squareInfo) {
	// save pad selected
	automationLayout.leftPadSelectedX = x;

	// calculate new velocity based on Y of pad pressed
	int32_t newVelocity = getVelocityFromY(y);

	// middle pad press variables
	automationLayout.middlePadPressSelected = false;

	// multi pad press variables
	automationLayout.multiPadPressSelected = false;
	SquareInfo rowSquareInfo[kDisplayWidth];
	int32_t multiPadPressVelocityIncrement = 0;

	// update velocity editor rendering
	bool refreshVelocityEditor = false;
	bool showNewVelocity = true;

	// check for middle or multi pad press
	if (velocity && squareInfo.numNotes != 0 && instrumentClipView.numEditPadPresses == 1) {
		// Find that original press
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive) {
				// if found, calculate middle velocity between two velocity pad presses
				if (instrumentClipView.editPadPresses[i].xDisplay == x) {
					// the last pad press will have updated the default velocity
					// so get it as it will be used to calculate average between previous and new velocity
					int32_t previousVelocity = getCurrentInstrument()->defaultVelocity;

					// calculate middle velocity (average of two pad presses in a column)
					newVelocity = (newVelocity + previousVelocity) / 2;

					// update middle pad press selection indicator
					automationLayout.middlePadPressSelected = true;

					break;
				}
				// found a second press that isn't in the same column as the first press
				else {
					int32_t firstPadX = instrumentClipView.editPadPresses[i].xDisplay;

					// get note info on all the squares in the note row
					noteRow->getRowSquareInfo(effectiveLength, rowSquareInfo);

					// the long press logic calculates and renders the interpolation as if the press was
					// entered in a forward fashion (where the first pad is to the left of the second
					// pad). if the user happens to enter a long press backwards then we fix that entry
					// by re-ordering the pad presses so that it is forward again
					automationLayout.leftPadSelectedX = firstPadX > x ? x : firstPadX;
					automationLayout.rightPadSelectedX = firstPadX > x ? firstPadX : x;

					int32_t numSquares = 0;
					// find total number of notes in note row (excluding the first note)
					for (int32_t i = automationLayout.leftPadSelectedX; i <= automationLayout.rightPadSelectedX; i++) {
						// don't include note tails in note count
						if (rowSquareInfo[i].numNotes != 0 && rowSquareInfo[i].squareType != SQUARE_NOTE_TAIL) {
							numSquares++;
						}
					}

					//	DEF_STACK_STRING_BUF(numSquare, 50);
					//	numSquare.append("Squares: ");
					//	numSquare.appendInt(numSquares);
					//	numSquare.append("\n");

					// calculate start and end velocity for long press
					int32_t leftPadSelectedVelocity;
					int32_t rightPadSelectedVelocity;

					if (automationLayout.leftPadSelectedX == firstPadX) { // then left pad is the first press
						leftPadSelectedVelocity = rowSquareInfo[automationLayout.leftPadSelectedX].averageVelocity;
						automationLayout.leftPadSelectedY = getYFromVelocity(leftPadSelectedVelocity);
						rightPadSelectedVelocity = getVelocityFromY(y);
						automationLayout.rightPadSelectedY = y;
					}
					else { // then left pad is the second press
						leftPadSelectedVelocity = getVelocityFromY(y);
						automationLayout.leftPadSelectedY = y;
						rightPadSelectedVelocity = rowSquareInfo[automationLayout.rightPadSelectedX].averageVelocity;
						automationLayout.rightPadSelectedY = getYFromVelocity(rightPadSelectedVelocity);
					}

					//	numSquare.append("L: ");
					//	numSquare.appendInt(leftPadSelectedVelocity);
					//	numSquare.append(" R: ");
					//	numSquare.appendInt(rightPadSelectedVelocity);
					//	numSquare.append("\n");

					// calculate increment from first pad to last pad
					float multiPadPressVelocityIncrementFloat =
					    static_cast<float>((rightPadSelectedVelocity - leftPadSelectedVelocity)) / (numSquares - 1);
					multiPadPressVelocityIncrement =
					    static_cast<int32_t>(std::round(multiPadPressVelocityIncrementFloat));
					// if ramp is upwards, make increment positive
					if (leftPadSelectedVelocity < rightPadSelectedVelocity) {
						multiPadPressVelocityIncrement = std::abs(multiPadPressVelocityIncrement);
					}

					//	numSquare.append("Inc: ");
					//	numSquare.appendInt(multiPadPressVelocityIncrement);
					//	display->displayPopup(numSquare.c_str());

					// update multi pad press selection indicator
					automationLayout.multiPadPressSelected = true;
					automationLayout.multiPadPressActive = true;

					break;
				}
			}
		}
	}

	// if middle pad press was selected, set the velocity to middle velocity between two pads pressed
	if (automationLayout.middlePadPressSelected) {
		setVelocity(modelStackWithNoteRow, noteRow, x, newVelocity);
		refreshVelocityEditor = true;
	}
	// if multi pad (long) press was selected, set the velocity of all the notes between the two pad presses
	else if (automationLayout.multiPadPressSelected) {
		setVelocityRamp(modelStackWithNoteRow, noteRow, rowSquareInfo, multiPadPressVelocityIncrement);
		refreshVelocityEditor = true;
	}
	// otherwise, it's a regular velocity pad action
	else {
		// no existing notes in square pressed
		// add note and set velocity
		if (squareInfo.numNotes == 0) {
			addNoteWithNewVelocity(x, velocity, newVelocity);
			refreshVelocityEditor = true;
		}
		// pressing pad corresponding to note's current averageVelocity, remove note
		else if (nonPatchCableMinPadDisplayValues[y] <= squareInfo.averageVelocity
		         && squareInfo.averageVelocity <= nonPatchCableMaxPadDisplayValues[y]) {
			recordNoteEditPadAction(x, velocity);
			refreshVelocityEditor = true;
			showNewVelocity = false;
		}
		// note(s) exists, adjust velocity of existing notes
		else {
			adjustNoteVelocity(modelStackWithNoteRow, noteRow, x, velocity, newVelocity, squareInfo.squareType);
			refreshVelocityEditor = true;
		}
	}
	// if no note exists and you're trying to remove a note (y == 0 && squareInfo.numNotes == 0),
	// well no need to do anything

	if (automationLayout.multiPadPressActive && !isUIModeActive(UI_MODE_NOTES_PRESSED)) {
		automationLayout.multiPadPressActive = false;
	}

	if (refreshVelocityEditor) {
		// refresh grid and update default velocity on the display
		uiNeedsRendering(&automationView, 0xFFFFFFFF, 0);
		// if holding a multi pad press, render left and right velocity of the multi pad press
		if (automationLayout.multiPadPressActive) {
			int32_t leftPadSelectedVelocity = getVelocityFromY(automationLayout.leftPadSelectedY);
			int32_t rightPadSelectedVelocity = getVelocityFromY(automationLayout.rightPadSelectedY);
			if (display->haveOLED()) {
				automationLayout.renderDisplay(leftPadSelectedVelocity, rightPadSelectedVelocity);
			}
			else {
				// for 7seg, render value of last pad pressed
				automationLayout.renderDisplay(automationLayout.leftPadSelectedX == x ? leftPadSelectedVelocity
				                                                                      : rightPadSelectedVelocity);
			}
		}
		else {
			if (velocity) {
				automationLayout.renderDisplay(showNewVelocity ? newVelocity : squareInfo.averageVelocity);
			}
			else {
				automationLayout.renderDisplay();
			}
		}
	}
}

// convert y of pad press into velocity value between 1 and 127
int32_t AutomationLayoutEditor::getVelocityFromY(int32_t y) {
	int32_t velocity = std::clamp<int32_t>(nonPatchCablePadPressValues[y], 1, 127);
	return velocity;
}

// convert velocity of a square into y
int32_t AutomationLayoutEditor::getYFromVelocity(int32_t velocity) {
	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (nonPatchCableMinPadDisplayValues[i] <= velocity && velocity <= nonPatchCableMaxPadDisplayValues[i]) {
			return i;
		}
	}
	return kNoSelection;
}

// add note and set velocity
void AutomationLayoutEditor::addNoteWithNewVelocity(int32_t x, int32_t velocity, int32_t newVelocity) {
	if (velocity) {
		// we change the instrument default velocity because it is used for new notes
		getCurrentInstrument()->defaultVelocity = newVelocity;
	}

	// record pad press and release
	// adds note with new velocity set
	recordNoteEditPadAction(x, velocity);
}

// adjust velocity of existing notes
void AutomationLayoutEditor::adjustNoteVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                                int32_t x, int32_t velocity, int32_t newVelocity, uint8_t squareType) {
	if (velocity) {
		// record pad press
		recordNoteEditPadAction(x, velocity);

		// adjust velocities of notes within pressed pad square
		setVelocity(modelStackWithNoteRow, noteRow, x, newVelocity);
	}
	else {
		// record pad release
		recordNoteEditPadAction(x, velocity);
	}
}

// set velocity of notes within pressed pad square
void AutomationLayoutEditor::setVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x,
                                         int32_t newVelocity) {
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	int32_t velocityValue = 0;

	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		bool foundPadPress = instrumentClipView.editPadPresses[i].isActive;

		// if we found an active pad press and we're looking for a pad press with a specific xDisplay
		// see if the active pad press is the one we are looking for
		if (foundPadPress && (x != kNoSelection)) {
			foundPadPress = (instrumentClipView.editPadPresses[i].xDisplay == x);
		}

		if (foundPadPress) {
			instrumentClipView.editPadPresses[i].deleteOnDepress = false;

			// Multiple notes in square
			if (instrumentClipView.editPadPresses[i].isBlurredSquare) {

				uint32_t velocitySumThisSquare = 0;
				uint32_t numNotesThisSquare = 0;

				int32_t noteI =
				    noteRow->notes.search(instrumentClipView.editPadPresses[i].intendedPos, GREATER_OR_EQUAL);
				Note* note = noteRow->notes.getElement(noteI);
				while (note
				       && note->pos - instrumentClipView.editPadPresses[i].intendedPos
				              < instrumentClipView.editPadPresses[i].intendedLength) {
					noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_SET_VELOCITY, newVelocity);

					instrumentClipView.updateVelocityValue(velocityValue, note->getVelocity());

					numNotesThisSquare++;
					velocitySumThisSquare += note->getVelocity();

					noteI++;
					note = noteRow->notes.getElement(noteI);
				}

				// Rohan: Get the average. Ideally we'd have done this when first selecting the note too, but I
				// didn't

				// Sean: not sure how getting the average when first selecting the note would help because the
				// average will change based on the velocity adjustment happening here.

				// We're adjusting the intendedVelocity here because this is the velocity that is used to audition
				// the pad press note so you can hear the velocity changes as you're holding the note down
				instrumentClipView.editPadPresses[i].intendedVelocity = velocitySumThisSquare / numNotesThisSquare;
			}

			// Only one note in square
			else {
				// We're adjusting the intendedVelocity here because this is the velocity that is used to audition
				// the pad press note so you can hear the velocity changes as you're holding the note down
				instrumentClipView.editPadPresses[i].intendedVelocity = newVelocity;
				noteRow->changeNotesAcrossAllScreens(instrumentClipView.editPadPresses[i].intendedPos,
				                                     modelStackWithNoteRow, action, CORRESPONDING_NOTES_SET_VELOCITY,
				                                     newVelocity);

				instrumentClipView.updateVelocityValue(velocityValue,
				                                       instrumentClipView.editPadPresses[i].intendedVelocity);
			}
		}
	}

	instrumentClipView.displayVelocity(velocityValue, 0);

	instrumentClipView.reassessAllAuditionStatus();
}

// set velocity of notes between pressed squares
void AutomationLayoutEditor::setVelocityRamp(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                             SquareInfo rowSquareInfo[kDisplayWidth], int32_t velocityIncrement) {
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	int32_t startVelocity = getVelocityFromY(automationLayout.leftPadSelectedY);
	int32_t velocityValue = 0;
	int32_t squaresProcessed = 0;

	for (int32_t i = automationLayout.leftPadSelectedX; i <= automationLayout.rightPadSelectedX; i++) {
		if (rowSquareInfo[i].numNotes != 0) {
			int32_t intendedPos = rowSquareInfo[i].squareStartPos;

			// Multiple notes in square
			if (rowSquareInfo[i].numNotes > 1) {
				int32_t intendedLength = rowSquareInfo[i].squareEndPos - intendedPos;

				int32_t noteI = noteRow->notes.search(intendedPos, GREATER_OR_EQUAL);

				Note* note = noteRow->notes.getElement(noteI);

				while (note && note->pos - intendedPos < intendedLength) {
					int32_t intendedVelocity =
					    std::clamp<int32_t>(startVelocity + (velocityIncrement * squaresProcessed), 1, 127);

					noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_SET_VELOCITY, intendedVelocity);

					noteI++;

					note = noteRow->notes.getElement(noteI);
				}
			}
			// one note in square
			else {
				int32_t intendedVelocity =
				    std::clamp<int32_t>(startVelocity + (velocityIncrement * squaresProcessed), 1, 127);

				noteRow->changeNotesAcrossAllScreens(intendedPos, modelStackWithNoteRow, action,
				                                     CORRESPONDING_NOTES_SET_VELOCITY, intendedVelocity);
			}

			// don't include note tails in note count
			if (rowSquareInfo[i].squareType != SQUARE_NOTE_TAIL) {
				squaresProcessed++;
			}
		}
	}
}

// call instrument clip view edit pad action function to process velocity pad press actions
void AutomationLayoutEditor::recordNoteEditPadAction(int32_t x, int32_t velocity) {
	instrumentClipView.editPadAction(velocity, instrumentClipView.lastAuditionedYDisplay, x,
	                                 currentSong->xZoom[NAVIGATION_CLIP]);
}

bool AutomationLayoutEditor::automationModEncoderActionForSelectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                                      int32_t whichModEncoder, int32_t offset,
                                                                      int32_t effectiveLength) {
	Clip* clip = getCurrentClip();

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		int32_t xDisplay = 0;

		// for a multi pad press, adjust value of first or last pad depending on mod encoder turned
		if (automationLayout.multiPadPressSelected) {
			if (whichModEncoder == 0) {
				xDisplay = automationLayout.leftPadSelectedX;
			}
			else if (whichModEncoder == 1) {
				xDisplay = automationLayout.rightPadSelectedX;
			}
		}

		// if not multi pad press, but in pad selection mode, then just adjust the single selected pad
		else if (automationLayout.padSelectionOn) {
			xDisplay = automationLayout.leftPadSelectedX;
		}

		// otherwise if not in pad selection mode, adjust the value of the pad currently being held
		else {
			// find pads that are currently pressed
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (instrumentClipView.editPadPresses[i].isActive) {
					xDisplay = instrumentClipView.editPadPresses[i].xDisplay;
				}
			}
		}

		uint32_t squareStart = 0;

		int32_t xScroll = currentSong->xScroll[automationLayout.navSysId];
		int32_t xZoom = currentSong->xZoom[automationLayout.navSysId];

		// for the second pad pressed in a long press, the square start position is set to the very last
		// nodes position
		if (automationLayout.multiPadPressSelected && (whichModEncoder == 1)) {
			int32_t squareRightEdge = automationView.getPosFromSquare(xDisplay + 1, xScroll, xZoom);
			squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
		}
		else {
			squareStart = automationView.getPosFromSquare(xDisplay, xScroll, xZoom);
		}

		if (squareStart < effectiveLength) {

			int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, squareStart);

			int32_t newKnobPos =
			    automationLayout.calculateAutomationKnobPosForModEncoderTurn(modelStackWithParam, knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a
			// value change gets recorded if newKnobPos exceeds 127, then it means current knobPos was
			// 127 and it was increased to 128. In which case, ignore value change
			if (!automationView.onArrangerView
			    && ((clip->output->type == OutputType::MIDI_OUT) && (newKnobPos == 64))) {
				return true;
			}

			// use default interpolation settings
			initInterpolation();

			setAutomationParameterValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength,
			                            xScroll, xZoom, true);

			view.potentiallyMakeItHarderToTurnKnob(whichModEncoder, modelStackWithParam, newKnobPos);

			// once first or last pad in a multi pad press is adjusted, re-render calculate multi pad
			// press based on revised start/ending values
			if (automationLayout.multiPadPressSelected) {

				automationLayout.handleAutomationMultiPadPress(
				    modelStackWithParam, clip, automationLayout.leftPadSelectedX, 0, automationLayout.rightPadSelectedX,
				    0, effectiveLength, xScroll, xZoom, true);

				automationLayout.renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength,
				                                                         xScroll, xZoom, xDisplay, true);

				return true;
			}
		}
	}

	return false;
}

void AutomationLayoutEditor::automationModEncoderActionForUnselectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                                        int32_t whichModEncoder, int32_t offset,
                                                                        int32_t effectiveLength) {
	Clip* clip = getCurrentClip();

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		if (modelStackWithParam->getTimelineCounter()
		    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

			int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, view.modPos);

			int32_t newKnobPos =
			    automationLayout.calculateAutomationKnobPosForModEncoderTurn(modelStackWithParam, knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a
			// value change gets recorded if newKnobPos exceeds 127, then it means current knobPos was
			// 127 and it was increased to 128. In which case, ignore value change
			if (!automationView.onArrangerView
			    && ((clip->output->type == OutputType::MIDI_OUT) && (newKnobPos == 64))) {
				return;
			}

			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

			// use default interpolation settings
			initInterpolation();

			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, view.modPos,
			                                                          view.modLength);

			if (!automationView.onArrangerView) {
				modelStackWithParam->getTimelineCounter()->instrumentBeenEdited();
			}

			if (!playbackHandler.isEitherClockActive() || !modelStackWithParam->autoParam->isAutomated()) {
				int32_t knobPos = newKnobPos + kKnobPosOffset;
				automationLayout.renderDisplay(knobPos, kNoSelection, true);
				automationLayout.setAutomationKnobIndicatorLevels(modelStackWithParam, knobPos, knobPos);
			}

			view.potentiallyMakeItHarderToTurnKnob(whichModEncoder, modelStackWithParam, newKnobPos);

			// midi follow and midi feedback enabled
			// re-send midi cc because learned parameter value has changed
			view.sendMidiFollowFeedback(modelStackWithParam, newKnobPos);
		}
	}
}

void AutomationLayoutEditor::copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xScroll,
                                            int32_t xZoom) {
	if (copiedParamAutomation.nodes) {
		delugeDealloc(copiedParamAutomation.nodes);
		copiedParamAutomation.nodes = NULL;
		copiedParamAutomation.numNodes = 0;
	}

	int32_t startPos = automationView.getPosFromSquare(0, xScroll, xZoom);
	int32_t endPos = automationView.getPosFromSquare(kDisplayWidth, xScroll, xZoom);
	if (startPos == endPos) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		bool isPatchCable = (modelStackWithParam->paramCollection
		                     == modelStackWithParam->paramManager->getPatchCableSetAllowJibberish());
		// Ok this is cursed, but will work fine so long as
		// the possibly invalid memory here doesn't accidentally
		// equal modelStack->paramCollection.

		modelStackWithParam->autoParam->copy(startPos, endPos, &copiedParamAutomation, isPatchCable,
		                                     modelStackWithParam);

		if (copiedParamAutomation.nodes) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_COPIED));
			return;
		}
	}

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_AUTOMATION_TO_COPY));
}

void AutomationLayoutEditor::pasteAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                             int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	if (!copiedParamAutomation.nodes) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_AUTOMATION_TO_PASTE));
		return;
	}

	int32_t startPos = automationView.getPosFromSquare(0, xScroll, xZoom);
	int32_t endPos = automationView.getPosFromSquare(kDisplayWidth, xScroll, xZoom);

	int32_t pastedAutomationWidth = endPos - startPos;
	if (pastedAutomationWidth == 0) {
		return;
	}

	float scaleFactor = (float)pastedAutomationWidth / copiedParamAutomation.width;

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_PASTE);

		if (action) {
			action->recordParamChangeIfNotAlreadySnapshotted(modelStackWithParam, false);
		}

		bool isPatchCable = (modelStackWithParam->paramCollection
		                     == modelStackWithParam->paramManager->getPatchCableSetAllowJibberish());
		// Ok this is cursed, but will work fine so long as
		// the possibly invalid memory here doesn't accidentally
		// equal modelStack->paramCollection.

		modelStackWithParam->autoParam->paste(startPos, endPos, scaleFactor, modelStackWithParam,
		                                      &copiedParamAutomation, isPatchCable);

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_PASTED));

		if (playbackHandler.isEitherClockActive()) {
			currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
		}
		else {
			if (automationLayout.padSelectionOn) {
				if (automationLayout.multiPadPressSelected) {
					automationLayout.renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength,
					                                                         xScroll, xZoom);
				}
				else {
					uint32_t squareStart = automationLayoutEditor.getMiddlePosFromSquare(
					    automationLayout.leftPadSelectedX, effectiveLength, xScroll, xZoom);

					automationLayout.updateAutomationModPosition(modelStackWithParam, squareStart);
				}
			}
			else {
				displayAutomation();
			}
		}

		return;
	}

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_PASTE_AUTOMATION));
}

// adjust the LED meters and update the display

/*updated function for displaying automation when playback is enabled (called from ui_timer_manager).
Also used internally in the automation instrument clip view for updating the display and led
indicators.*/

void AutomationLayoutEditor::displayAutomation(bool padSelected, bool updateDisplay) {
	if ((!automationLayout.padSelectionOn && !isUIModeActive(UI_MODE_NOTES_PRESSED)) || padSelected) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (automationView.onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			Clip* clip = getCurrentClip();

			modelStackWithParam = automationLayout.getModelStackWithParamForClip(modelStack, clip);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {

			if (modelStackWithParam->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				int32_t knobPos = automationLayoutEditor.getAutomationParameterKnobPos(modelStackWithParam, view.modPos)
				                  + kKnobPosOffset;

				bool displayValue =
				    updateDisplay
				    && (display->haveOLED()
				        || (display->have7SEG() && (playbackHandler.isEitherClockActive() || padSelected)));

				// update value on the screen when playing back automation
				// don't update value displayed if there's no automation unless instructed to update display
				// don't update value displayed when playback is stopped
				if (displayValue) {
					automationLayout.renderDisplay(knobPos);
				}
				// on 7SEG re-render parameter name under certain circumstances
				// e.g. when entering pad selection mode, when stopping playback
				else {
					automationLayout.renderDisplay();
				}

				automationLayout.setAutomationKnobIndicatorLevels(modelStackWithParam, knobPos, knobPos);
			}
		}
	}
}

// calculates the length of the arrangement timeline, clip or the length of the kit row
// if you're in a synth clip, kit clip with affect entire enabled or midi clip it returns clip length
// if you're in a kit clip with affect entire disabled and a row selected, it returns kit row length
int32_t AutomationLayoutEditor::getEffectiveLength(ModelStackWithTimelineCounter* modelStack) {
	Clip* clip = getCurrentClip();
	OutputType outputType = clip->output->type;

	int32_t effectiveLength = 0;

	if (automationView.onArrangerView) {
		effectiveLength = arrangerView.getMaxLength();
	}
	else if (outputType == OutputType::KIT && !automationLayout.getAffectEntire()) {
		ModelStackWithNoteRow* modelStackWithNoteRow = ((InstrumentClip*)clip)->getNoteRowForSelectedDrum(modelStack);

		effectiveLength = modelStackWithNoteRow->getLoopLength();
	}
	else {
		// this will differ for a kit when in note row mode
		effectiveLength = clip->loopLength;
	}

	return effectiveLength;
}
