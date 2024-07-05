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

#include "gui/views/automation/sound_editor_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "io/midi/midi_transpose.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/consequence/consequence_note_row_mute.h"
#include "model/drum/drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/copied_note_row.h"
#include "model/note/note.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"
#include "util/cfunctions.h"
#include "util/functions.h"
#include <new>
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

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

AutomationSoundEditorView AutomationSoundEditorView{};

AutomationSoundEditorView::AutomationSoundEditorView() {
}

// gets the length of the clip, renders the pads corresponding to current parameter values set up to the
// clip length renders the undefined area of the clip that the user can't interact with
void AutomationSoundEditorView::renderAutomationEditor(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
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
		AutomationView::renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth, this,
		                    currentSong->tripletsOn, xDisplay);
	}
}

/// render each square in each column of the automation editor grid
void AutomationSoundEditorView::renderAutomationColumn(ModelStackWithAutoParam* modelStackWithParam,
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
void AutomationSoundEditorView::renderAutomationBipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
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
	if (padSelectionOn && ((xDisplay == leftPadSelectedX) || (xDisplay == rightPadSelectedX))) {
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
void AutomationSoundEditorView::renderAutomationUnipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
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
	if (padSelectionOn && ((xDisplay == leftPadSelectedX) || (xDisplay == rightPadSelectedX))) {
		if (doRender) {
			pixel = rowBlurColour[yDisplay];
		}
		else {
			pixel = colours::grey;
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
}

// get's the name of the Parameter being edited so it can be displayed on the screen
void AutomationSoundEditorView::getAutomationParameterName(Clip* clip, OutputType outputType, char* parameterName) {
	if (onArrangerView || outputType == OutputType::SYNTH || outputType == OutputType::KIT
	    || outputType == OutputType::AUDIO) {
		params::Kind lastSelectedParamKind = params::Kind::NONE;
		int32_t lastSelectedParamID = kNoSelection;
		PatchSource lastSelectedPatchSource = PatchSource::NONE;
		if (onArrangerView) {
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

			DEF_STACK_STRING_BUF(paramDisplayName, 30);
			if (source2 == PatchSource::NONE) {
				paramDisplayName.append(getSourceDisplayNameForOLED(lastSelectedPatchSource));
			}
			else {
				paramDisplayName.append(sourceToStringShort(lastSelectedPatchSource));
			}
			if (display->haveOLED()) {
				paramDisplayName.append(" -> ");
			}
			else {
				paramDisplayName.append(" - ");
			}

			if (source2 != PatchSource::NONE) {
				paramDisplayName.append(sourceToStringShort(source2));
				if (display->haveOLED()) {
					paramDisplayName.append(" -> ");
				}
				else {
					paramDisplayName.append(" - ");
				}
			}

			paramDisplayName.append(modulation::params::getPatchedParamShortName(lastSelectedParamID));
			strncpy(parameterName, paramDisplayName.c_str(), 29);
		}
		else {
			strncpy(parameterName, getParamDisplayName(lastSelectedParamKind, lastSelectedParamID), 29);
		}
	}
	else if (outputType == OutputType::MIDI_OUT) {
		if (clip->lastSelectedParamID == CC_NUMBER_NONE) {
			strcpy(parameterName, deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_PARAM));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_PITCH_BEND) {
			strcpy(parameterName, deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_AFTERTOUCH) {
			strcpy(parameterName, deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_MOD_WHEEL || clip->lastSelectedParamID == CC_NUMBER_Y_AXIS) {
			strcpy(parameterName, deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
		}
		else {
			parameterName[0] = 'C';
			parameterName[1] = 'C';
			if (display->haveOLED()) {
				parameterName[2] = ' ';
				intToString(clip->lastSelectedParamID, &parameterName[3]);
			}
			else {
				char* numberStartPos;
				if (clip->lastSelectedParamID < 10) {
					parameterName[2] = ' ';
					numberStartPos = parameterName + 3;
				}
				else {
					numberStartPos = (clip->lastSelectedParamID < 100) ? (parameterName + 2) : (parameterName + 1);
				}
				intToString(clip->lastSelectedParamID, numberStartPos);
			}
		}
	}
}

// adjust the LED meters and update the display

/*updated function for displaying automation when playback is enabled (called from ui_timer_manager).
Also used internally in the automation instrument clip view for updating the display and led
indicators.*/

void AutomationSoundEditorView::displayAutomation(bool padSelected, bool updateDisplay) {
	if ((!padSelectionOn && !isUIModeActive(UI_MODE_NOTES_PRESSED)) || padSelected) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			Clip* clip = getCurrentClip();

			modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {

			if (modelStackWithParam->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, view.modPos) + kKnobPosOffset;

				// update value on the screen when playing back automation
				if (updateDisplay && !playbackStopped) {
					renderDisplay(knobPos);
				}
				// on 7SEG re-render parameter name under certain circumstances
				// e.g. when entering pad selection mode, when stopping playback
				else {
					renderDisplay();
					playbackStopped = false;
				}

				setAutomationKnobIndicatorLevels(modelStackWithParam, knobPos, knobPos);
			}
		}
	}
}

/// toggle automation interpolation on / off
bool AutomationSoundEditorView::toggleAutomationInterpolation() {
	if (interpolation) {
		interpolation = false;
		initInterpolation();
		resetInterpolationShortcutBlinking();

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_DISABLED));
	}
	else {
		interpolation = true;
		blinkInterpolationShortcut();

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_ENABLED));
	}
	return true;
}

/// toggle automation pad selection mode on / off
bool AutomationSoundEditorView::toggleAutomationPadSelectionMode(ModelStackWithAutoParam* modelStackWithParam,
                                                           int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	// enter/exit pad selection mode
	if (padSelectionOn) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_OFF));

		initPadSelection();
		if (!playbackHandler.isEitherClockActive()) {
			displayAutomation(true, !display->have7SEG());
		}
	}
	else {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_ON));

		padSelectionOn = true;
		blinkPadSelectionShortcut();

		multiPadPressSelected = false;
		multiPadPressActive = false;

		// display only left cursor initially
		leftPadSelectedX = 0;
		rightPadSelectedX = kNoSelection;

		uint32_t squareStart = getMiddlePosFromSquare(leftPadSelectedX, effectiveLength, xScroll, xZoom);

		updateAutomationModPosition(modelStackWithParam, squareStart, !display->have7SEG());
	}
	uiNeedsRendering(this);
	return true;
}

// automation edit pad action
// handles single and multi pad presses for automation editing
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationSoundEditorView::automationEditPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                  int32_t xDisplay, int32_t yDisplay, int32_t velocity,
                                                  int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	if (padSelectionOn) {
		selectedPadPressed = velocity;
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

					multiPadPressSelected = true;
					multiPadPressActive = true;

					// the long press logic calculates and renders the interpolation as if the press was
					// entered in a forward fashion (where the first pad is to the left of the second
					// pad). if the user happens to enter a long press backwards then we fix that entry
					// by re-ordering the pad presses so that it is forward again
					leftPadSelectedX = firstPadX > xDisplay ? xDisplay : firstPadX;
					leftPadSelectedY = firstPadX > xDisplay ? yDisplay : firstPadY;
					rightPadSelectedX = firstPadX > xDisplay ? firstPadX : xDisplay;
					rightPadSelectedY = firstPadX > xDisplay ? firstPadY : yDisplay;

					// if you're not in pad selection mode, allow user to enter a long press
					if (!padSelectionOn) {
						handleAutomationMultiPadPress(modelStackWithParam, clip, leftPadSelectedX, leftPadSelectedY,
						                              rightPadSelectedX, rightPadSelectedY, effectiveLength, xScroll,
						                              xZoom);
					}
					else {
						uiNeedsRendering(this);
					}

					// set led indicators to left / right pad selection values
					// and update display
					renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom,
					                                        xDisplay);
				}
				else {
					leftPadSelectedY = firstPadY;
					middlePadPressSelected = true;
					goto singlePadPressAction;
				}
			}
		}

		// Or, if this is a regular create-or-select press...
		else {
singlePadPressAction:
			if (recordAutomationSinglePadPress(xDisplay, yDisplay)) {
				multiPadPressActive = false;
				handleAutomationSinglePadPress(modelStackWithParam, clip, xDisplay, yDisplay, effectiveLength, xScroll,
				                               xZoom);
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
		if (!padSelectionOn && multiPadPressSelected && (currentUIMode != UI_MODE_NOTES_PRESSED)) {
			initPadSelection();
		}
		// switch from long press selection to short press selection in pad selection mode
		else if (padSelectionOn && multiPadPressSelected && !multiPadPressActive
		         && (currentUIMode != UI_MODE_NOTES_PRESSED)
		         && ((AudioEngine::audioSampleTimer - instrumentClipView.timeLastEditPadPress) < kShortPressTime)) {

			multiPadPressSelected = false;

			leftPadSelectedX = xDisplay;
			rightPadSelectedX = kNoSelection;

			uiNeedsRendering(this);
		}

		if (currentUIMode != UI_MODE_NOTES_PRESSED) {
			lastPadSelectedKnobPos = kNoSelection;
			if (multiPadPressSelected) {
				renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom,
				                                        xDisplay);
			}
			else if (!playbackHandler.isEitherClockActive()) {
				displayAutomation(padSelectionOn, !display->have7SEG());
			}
		}

		middlePadPressSelected = false;
	}
}

bool AutomationSoundEditorView::recordAutomationSinglePadPress(int32_t xDisplay, int32_t yDisplay) {
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

// new function created for automation instrument clip view to shift automations of the selected
// parameter previously users only had the option to shift ALL automations together as part of community
// feature i disabled automation shifting in the regular instrument clip view
void AutomationSoundEditorView::shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
                                                      int32_t effectiveLength) {
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		modelStackWithParam->autoParam->shiftHorizontally(offset, effectiveLength);
	}

	uiNeedsRendering(this);
}

bool AutomationSoundEditorView::automationModEncoderActionForSelectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                                   int32_t whichModEncoder, int32_t offset,
                                                                   int32_t effectiveLength) {
	Clip* clip = getCurrentClip();

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		int32_t xDisplay = 0;

		// for a multi pad press, adjust value of first or last pad depending on mod encoder turned
		if (multiPadPressSelected) {
			if (whichModEncoder == 0) {
				xDisplay = leftPadSelectedX;
			}
			else if (whichModEncoder == 1) {
				xDisplay = rightPadSelectedX;
			}
		}

		// if not multi pad press, but in pad selection mode, then just adjust the single selected pad
		else if (padSelectionOn) {
			xDisplay = leftPadSelectedX;
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

		int32_t xScroll = currentSong->xScroll[navSysId];
		int32_t xZoom = currentSong->xZoom[navSysId];

		// for the second pad pressed in a long press, the square start position is set to the very last
		// nodes position
		if (multiPadPressSelected && (whichModEncoder == 1)) {
			int32_t squareRightEdge = getPosFromSquare(xDisplay + 1, xScroll, xZoom);
			squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
		}
		else {
			squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
		}

		if (squareStart < effectiveLength) {

			int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, squareStart);

			int32_t newKnobPos = calculateAutomationKnobPosForModEncoderTurn(modelStackWithParam, knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a
			// value change gets recorded if newKnobPos exceeds 127, then it means current knobPos was
			// 127 and it was increased to 128. In which case, ignore value change
			if (!onArrangerView && ((clip->output->type == OutputType::MIDI_OUT) && (newKnobPos == 64))) {
				return true;
			}

			// use default interpolation settings
			initInterpolation();

			setAutomationParameterValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength,
			                            xScroll, xZoom, true);

			view.potentiallyMakeItHarderToTurnKnob(whichModEncoder, modelStackWithParam, newKnobPos);

			// once first or last pad in a multi pad press is adjusted, re-render calculate multi pad
			// press based on revised start/ending values
			if (multiPadPressSelected) {

				handleAutomationMultiPadPress(modelStackWithParam, clip, leftPadSelectedX, 0, rightPadSelectedX, 0,
				                              effectiveLength, xScroll, xZoom, true);

				renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom,
				                                        xDisplay, true);

				return true;
			}
		}
	}

	return false;
}

void AutomationSoundEditorView::automationModEncoderActionForUnselectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                                     int32_t whichModEncoder, int32_t offset,
                                                                     int32_t effectiveLength) {
	Clip* clip = getCurrentClip();

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		if (modelStackWithParam->getTimelineCounter()
		    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

			int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, view.modPos);

			int32_t newKnobPos = calculateAutomationKnobPosForModEncoderTurn(modelStackWithParam, knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a
			// value change gets recorded if newKnobPos exceeds 127, then it means current knobPos was
			// 127 and it was increased to 128. In which case, ignore value change
			if (!onArrangerView && ((clip->output->type == OutputType::MIDI_OUT) && (newKnobPos == 64))) {
				return;
			}

			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

			// use default interpolation settings
			initInterpolation();

			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, view.modPos,
			                                                          view.modLength);

			if (!onArrangerView) {
				modelStackWithParam->getTimelineCounter()->instrumentBeenEdited();
			}

			if (!playbackHandler.isEitherClockActive()) {
				int32_t knobPos = newKnobPos + kKnobPosOffset;
				renderDisplay(knobPos, kNoSelection, true);
				setAutomationKnobIndicatorLevels(modelStackWithParam, knobPos, knobPos);
			}

			view.potentiallyMakeItHarderToTurnKnob(whichModEncoder, modelStackWithParam, newKnobPos);

			// midi follow and midi feedback enabled
			// re-send midi cc because learned parameter value has changed
			view.sendMidiFollowFeedback(modelStackWithParam, newKnobPos);
		}
	}
}

void AutomationSoundEditorView::copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xScroll,
                                         int32_t xZoom) {
	if (copiedParamAutomation.nodes) {
		delugeDealloc(copiedParamAutomation.nodes);
		copiedParamAutomation.nodes = NULL;
		copiedParamAutomation.numNodes = 0;
	}

	int32_t startPos = getPosFromSquare(0, xScroll, xZoom);
	int32_t endPos = getPosFromSquare(kDisplayWidth, xScroll, xZoom);
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

void AutomationSoundEditorView::pasteAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                          int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	if (!copiedParamAutomation.nodes) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_AUTOMATION_TO_PASTE));
		return;
	}

	int32_t startPos = getPosFromSquare(0, xScroll, xZoom);
	int32_t endPos = getPosFromSquare(kDisplayWidth, xScroll, xZoom);

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
			if (padSelectionOn) {
				if (multiPadPressSelected) {
					renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
				}
				else {
					uint32_t squareStart = getMiddlePosFromSquare(leftPadSelectedX, effectiveLength, xScroll, xZoom);

					updateAutomationModPosition(modelStackWithParam, squareStart);
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

// used with SelectEncoderAction to get the next arranger / audio clip / kit affect entire parameter
void AutomationSoundEditorView::selectGlobalParam(int32_t offset, Clip* clip) {
	if (onArrangerView) {
		auto idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		{
			while ((id == params::UNPATCHED_PITCH_ADJUST || id == params::UNPATCHED_SIDECHAIN_SHAPE
			        || id == params::UNPATCHED_SIDECHAIN_VOLUME || id == params::UNPATCHED_COMPRESSOR_THRESHOLD)) {

				if (offset < 0) {
					offset -= 1;
				}
				else if (offset > 0) {
					offset += 1;
				}
				idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamArrayPosition,
				                                        kNumGlobalParamsForAutomation);
				id = globalParamsForAutomation[idx].second;
			}
		}
		currentSong->lastSelectedParamID = id;
		currentSong->lastSelectedParamKind = kind;
		currentSong->lastSelectedParamArrayPosition = idx;
	}
	else {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		clip->lastSelectedParamID = id;
		clip->lastSelectedParamKind = kind;
		clip->lastSelectedParamArrayPosition = idx;
	}
	automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next synth or kit non-affect entire param
void AutomationSoundEditorView::selectNonGlobalParam(int32_t offset, Clip* clip) {
	bool foundPatchCable = false;
	// if we previously selected a patch cable, we'll see if there are any more to scroll through
	if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
		foundPatchCable = selectPatchCable(offset, clip);
		// did we find another patch cable?
		if (!foundPatchCable) {
			// if we haven't found a patch cable, it means we reached beginning or end of patch cable
			// list if we're scrolling right, we'll resume with selecting a regular param from beg of
			// list if we're scrolling left, we'll resume with selecting a regular param from end of
			// list to do so we re-set the last selected param array position

			// scrolling right
			if (offset > 0) {
				clip->lastSelectedParamArrayPosition = kNumNonGlobalParamsForAutomation - 1;
			}
			// scrolling left
			else if (offset < 0) {
				clip->lastSelectedParamArrayPosition = 0;
			}
		}
	}
	// if we didn't find anymore patch cables, then we'll select a regular param from the list
	if (!foundPatchCable) {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumNonGlobalParamsForAutomation);
		{
			auto [kind, id] = nonGlobalParamsForAutomation[idx];
			if ((clip->output->type == OutputType::KIT) && (kind == params::Kind::UNPATCHED_SOUND)
			    && (id == params::UNPATCHED_PORTAMENTO)) {
				if (offset < 0) {
					offset -= 1;
				}
				else if (offset > 0) {
					offset += 1;
				}
				idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
				                                        kNumNonGlobalParamsForAutomation);
			}
		}

		// did we reach beginning or end of list?
		// if yes, then let's scroll through patch cables
		// but only if we haven't already scrolled through patch cables already above
		if ((clip->lastSelectedParamKind != params::Kind::PATCH_CABLE)
		    && (((offset > 0) && (idx < clip->lastSelectedParamArrayPosition))
		        || ((offset < 0) && (idx > clip->lastSelectedParamArrayPosition)))) {
			foundPatchCable = selectPatchCable(offset, clip);
		}

		// if we didn't find a patch cable, then we'll resume with scrolling the non-patch cable list
		if (!foundPatchCable) {
			auto [kind, id] = nonGlobalParamsForAutomation[idx];
			clip->lastSelectedParamID = id;
			clip->lastSelectedParamKind = kind;
			clip->lastSelectedParamArrayPosition = idx;
		}
	}
	automationParamType = AutomationParamType::PER_SOUND;
}

// iterate through the patch cable list to select the previous or next patch cable
// actual selecting of the patch cable is done in the selectPatchCableAtIndex function
bool AutomationSoundEditorView::selectPatchCable(int32_t offset, Clip* clip) {
	ParamManagerForTimeline* paramManager = clip->getCurrentParamManager();
	if (paramManager) {
		PatchCableSet* set = paramManager->getPatchCableSetAllowJibberish();
		// make sure it's not jiberish
		if (set) {
			// do we have any patch cables?
			if (set->numPatchCables > 0) {
				bool foundCurrentPatchCable = false;
				// scrolling right
				if (offset > 0) {
					// loop from beginning to end of patch cable list
					for (int i = 0; i < set->numPatchCables; i++) {
						// loop through patch cables until we've found a new one and select it
						// adjacent to current found patch cable (if we previously selected one)
						if (selectPatchCableAtIndex(clip, set, i, foundCurrentPatchCable)) {
							return true;
						}
					}
				}
				// scrolling left
				else if (offset < 0) {
					// loop from end to beginning of patch cable list
					for (int i = set->numPatchCables - 1; i >= 0; i--) {
						// loop through patch cables until we've found a new one and select it
						// adjacent to current found patch cable (if we previously selected one)
						if (selectPatchCableAtIndex(clip, set, i, foundCurrentPatchCable)) {
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

// this function does the actual selecting of a patch cable
// see if the patch cable selected is different from current one selected (or not selected)
// if we havent already selected a patch cable, we'll select this one
// if we selected one previously, we'll see if this one is adjacent to the previous one selected
// if it's adjacent to the previous one selected, we'll select this one
bool AutomationSoundEditorView::selectPatchCableAtIndex(Clip* clip, PatchCableSet* set, int32_t patchCableIndex,
                                                  bool& foundCurrentPatchCable) {
	PatchCable* cable = &set->patchCables[patchCableIndex];
	ParamDescriptor desc = cable->destinationParamDescriptor;
	// need to add patch cable source to the descriptor so that we can get the paramId from it
	desc.addSource(cable->from);

	// if we've previously selected a patch cable, we want to start scrolling from that patch cable
	// note: the reason why we can't save the patchCableIndex to make finding the previous patch
	// cable selected easier is because the patch cable array gets re-indexed as patch cables get
	// added or removed or values change. Thus you need to search for the previous patch cable to get
	// the updated index and then you can find the adjacent patch cable in the list.
	if (desc.data == clip->lastSelectedParamID) {
		foundCurrentPatchCable = true;
	}
	// if we found the patch cable we previously selected and we found another one
	// or we hadn't selected a patch cable previously and found a patch cable
	// select the one we found
	else if ((foundCurrentPatchCable || (clip->lastSelectedParamKind != params::Kind::PATCH_CABLE))
	         && (desc.data != clip->lastSelectedParamID)) {
		clip->lastSelectedPatchSource = cable->from;
		clip->lastSelectedParamID = desc.data;
		clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
		return true;
	}
	return false;
}

// used with SelectEncoderAction to get the next midi CC
void AutomationSoundEditorView::selectMIDICC(int32_t offset, Clip* clip) {
	if (onAutomationOverview()) {
		clip->lastSelectedParamID = CC_NUMBER_NONE;
	}
	auto newCC = clip->lastSelectedParamID;
	newCC += offset;
	if (newCC < 0) {
		newCC = CC_NUMBER_Y_AXIS;
	}
	else if (newCC >= kNumCCExpression) {
		newCC = 0;
	}
	if (newCC == CC_NUMBER_MOD_WHEEL) {
		// mod wheel is actually CC_NUMBER_Y_AXIS (122) internally
		newCC += offset;
	}
	clip->lastSelectedParamID = newCC;
	automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next parameter in the list of parameters
int32_t AutomationSoundEditorView::getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
                                                               int32_t numParams) {
	int32_t idx;
	// if you haven't selected a parameter yet, start at the beginning of the list
	if (onAutomationOverview()) {
		idx = 0;
	}
	// if you are scrolling left and are at the beginning of the list, go to the end of the list
	else if ((lastSelectedParamArrayPosition + offset) < 0) {
		idx = numParams + offset;
	}
	// if you are scrolling right and are at the end of the list, go to the beginning of the list
	else if ((lastSelectedParamArrayPosition + offset) > (numParams - 1)) {
		idx = 0;
	}
	// otherwise scrolling left/right within the list
	else {
		idx = lastSelectedParamArrayPosition + offset;
	}
	return idx;
}

// used with Select Encoder action to get the X, Y grid shortcut coordinates of the parameter selected
void AutomationSoundEditorView::getLastSelectedParamShortcut(Clip* clip) {
	bool paramShortcutFound = false;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (onArrangerView) {
				if (unpatchedGlobalParamShortcuts[x][y] == currentSong->lastSelectedParamID) {
					currentSong->lastSelectedParamShortcutX = x;
					currentSong->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
			else if (clip->output->type == OutputType::MIDI_OUT) {
				if (midiCCShortcutsForAutomation[x][y] == clip->lastSelectedParamID) {
					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
			else {
				if ((clip->lastSelectedParamKind == params::Kind::PATCHED
				     && patchedParamShortcuts[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == params::Kind::UNPATCHED_SOUND
				        && unpatchedNonGlobalParamShortcuts[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == params::Kind::UNPATCHED_GLOBAL
				        && unpatchedGlobalParamShortcuts[x][y] == clip->lastSelectedParamID)) {
					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
		}
		if (paramShortcutFound) {
			break;
		}
	}
	if (!paramShortcutFound) {
		if (onArrangerView) {
			currentSong->lastSelectedParamShortcutX = kNoSelection;
			currentSong->lastSelectedParamShortcutY = kNoSelection;
		}
		else {
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
		}
	}
}

void AutomationSoundEditorView::getLastSelectedParamArrayPosition(Clip* clip) {
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you're in arranger view or in a non-midi, non-cv clip (e.g. audio, synth, kit)
	if (onArrangerView || outputType != OutputType::CV) {
		// if you're in a audio clip, a kit with affect entire enabled, or in arranger view
		if (onArrangerView || (outputType == OutputType::AUDIO)
		    || (outputType == OutputType::KIT && getAffectEntire())) {
			getLastSelectedGlobalParamArrayPosition(clip);
		}
		// if you're a synth or a kit (with affect entire off and a drum selected)
		else if (outputType == OutputType::SYNTH || (outputType == OutputType::KIT && ((Kit*)output)->selectedDrum)) {
			getLastSelectedNonGlobalParamArrayPosition(clip);
		}
	}
}

void AutomationSoundEditorView::getLastSelectedNonGlobalParamArrayPosition(Clip* clip) {
	for (auto idx = 0; idx < kNumNonGlobalParamsForAutomation; idx++) {

		auto [kind, id] = nonGlobalParamsForAutomation[idx];

		if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
			clip->lastSelectedParamArrayPosition = idx;
			break;
		}
	}
}

void AutomationSoundEditorView::getLastSelectedGlobalParamArrayPosition(Clip* clip) {
	for (auto idx = 0; idx < kNumGlobalParamsForAutomation; idx++) {

		auto [kind, id] = globalParamsForAutomation[idx];

		if (onArrangerView) {
			if ((id == currentSong->lastSelectedParamID) && (kind == currentSong->lastSelectedParamKind)) {
				currentSong->lastSelectedParamArrayPosition = idx;
				break;
			}
		}
		else {
			if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
				clip->lastSelectedParamArrayPosition = idx;
				break;
			}
		}
	}
}

void AutomationSoundEditorView::initInterpolation() {

	interpolationBefore = false;
	interpolationAfter = false;
}

// calculates the length of the arrangement timeline, clip or the length of the kit row
// if you're in a synth clip, kit clip with affect entire enabled or midi clip it returns clip length
// if you're in a kit clip with affect entire disabled and a row selected, it returns kit row length
int32_t AutomationSoundEditorView::getEffectiveLength(ModelStackWithTimelineCounter* modelStack) {
	Clip* clip = getCurrentClip();
	OutputType outputType = clip->output->type;

	int32_t effectiveLength = 0;

	if (onArrangerView) {
		effectiveLength = arrangerView.getMaxLength();
	}
	else if (outputType == OutputType::KIT && !getAffectEntire()) {
		ModelStackWithNoteRow* modelStackWithNoteRow = ((InstrumentClip*)clip)->getNoteRowForSelectedDrum(modelStack);

		effectiveLength = modelStackWithNoteRow->getLoopLength();
	}
	else {
		// this will differ for a kit when in note row mode
		effectiveLength = clip->loopLength;
	}

	return effectiveLength;
}

uint32_t AutomationSoundEditorView::getSquareWidth(int32_t square, int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	int32_t squareRightEdge = getPosFromSquare(square + 1, xScroll, xZoom);
	return std::min(effectiveLength, squareRightEdge) - getPosFromSquare(square, xScroll, xZoom);
}

// when pressing on a single pad, you want to display the value of the middle node within that square
// as that is the most accurate value that represents that square
uint32_t AutomationSoundEditorView::getMiddlePosFromSquare(int32_t xDisplay, int32_t effectiveLength, int32_t xScroll,
                                                     int32_t xZoom) {
	uint32_t squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
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
int32_t AutomationSoundEditorView::getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t squareStart) {
	// obtain value corresponding to the two pads that were pressed in a multi pad press action
	int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareStart, modelStack);
	int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(currentValue, modelStack);

	return knobPos;
}

// this function is based off the code in AutoParam::getValueAtPos, it was tweaked to just return
// interpolation status of the left node or right node (depending on the reversed parameter which is
// used to indicate what node in what direction we are looking for (e.g. we want status of left node, or
// right node, relative to the current pos we are looking at
bool AutomationSoundEditorView::getAutomationNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos,
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
void AutomationSoundEditorView::setAutomationParameterValue(ModelStackWithAutoParam* modelStack, int32_t knobPos,
                                                      int32_t squareStart, int32_t xDisplay, int32_t effectiveLength,
                                                      int32_t xScroll, int32_t xZoom, bool modEncoderAction) {

	int32_t newValue = modelStack->paramCollection->knobPosToParamValue(knobPos, modelStack);

	uint32_t squareWidth = 0;

	// for a multi pad press, the beginning and ending pad presses are set with a square width of 3 (1
	// node).
	if (multiPadPressSelected) {
		squareWidth = kParamNodeWidth;
	}
	else {
		squareWidth = getSquareWidth(xDisplay, effectiveLength, xScroll, xZoom);
	}

	// if you're doing a single pad press, you don't want the values around that single press position
	// to change they will change if those nodes around the single pad press were created with
	// interpolation turned on to fix this, re-create those nodes with their current value with
	// interpolation off

	interpolationBefore = getAutomationNodeInterpolation(modelStack, squareStart, true);
	interpolationAfter = getAutomationNodeInterpolation(modelStack, squareStart, false);

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

	if (!onArrangerView) {
		modelStack->getTimelineCounter()->instrumentBeenEdited();
	}

	// in a multi pad press, no need to display all the values calculated
	if (!multiPadPressSelected) {
		int32_t newKnobPos = knobPos + kKnobPosOffset;
		renderDisplay(newKnobPos, kNoSelection, modEncoderAction);
		setAutomationKnobIndicatorLevels(modelStack, newKnobPos, newKnobPos);
	}

	// midi follow and midi feedback enabled
	// re-send midi cc because learned parameter value has changed
	view.sendMidiFollowFeedback(modelStack, knobPos);
}

// sets both knob indicators to the same value when pressing single pad,
// deleting automation, or displaying current parameter value
// multi pad presses don't use this function
void AutomationSoundEditorView::setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
                                                           int32_t knobPosRight) {
	params::Kind kind = modelStack->paramCollection->getParamKind();
	bool isBipolar = isParamBipolar(kind, modelStack->paramId);

	// if you're dealing with a patch cable which has a -128 to +128 range
	// we'll need to convert it to a 0 - 128 range for purpose of rendering on knob indicators
	if (kind == params::Kind::PATCH_CABLE) {
		knobPosLeft = view.convertPatchCableKnobPosToIndicatorLevel(knobPosLeft);
		knobPosRight = view.convertPatchCableKnobPosToIndicatorLevel(knobPosRight);
	}

	bool isBlinking = indicator_leds::isKnobIndicatorBlinking(0) || indicator_leds::isKnobIndicatorBlinking(1);

	if (!isBlinking) {
		indicator_leds::setKnobIndicatorLevel(0, knobPosLeft, isBipolar);
		indicator_leds::setKnobIndicatorLevel(1, knobPosRight, isBipolar);
	}
}

// updates the position that the active mod controllable stack is pointing to
// this sets the current value for the active parameter so that it can be auditioned
void AutomationSoundEditorView::updateAutomationModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart,
                                                      bool updateDisplay, bool updateIndicatorLevels) {

	if (!playbackHandler.isEitherClockActive() || padSelectionOn) {
		if (modelStack && modelStack->autoParam) {
			if (modelStack->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				view.activeModControllableModelStack.paramManager->toForTimeline()->grabValuesFromPos(
				    squareStart, &view.activeModControllableModelStack);

				int32_t knobPos = getAutomationParameterKnobPos(modelStack, squareStart) + kKnobPosOffset;

				if (updateDisplay) {
					renderDisplay(knobPos);
				}

				if (updateIndicatorLevels) {
					setAutomationKnobIndicatorLevels(modelStack, knobPos, knobPos);
				}
			}
		}
	}
}

// takes care of setting the automation value for the single pad that was pressed
void AutomationSoundEditorView::handleAutomationSinglePadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                         int32_t xDisplay, int32_t yDisplay, int32_t effectiveLength,
                                                         int32_t xScroll, int32_t xZoom) {

	Output* output = clip->output;
	OutputType outputType = output->type;

	// this means you are editing a parameter's value
	if (inAutomationEditor()) {
		handleAutomationParameterChange(modelStackWithParam, clip, outputType, xDisplay, yDisplay, effectiveLength,
		                                xScroll, xZoom);
	}

	uiNeedsRendering(this);
}

// called by handle single pad press when it is determined that you are editing parameter automation
// using the grid
void AutomationSoundEditorView::handleAutomationParameterChange(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                          OutputType outputType, int32_t xDisplay, int32_t yDisplay,
                                                          int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	if (padSelectionOn) {
		// display pad's value
		uint32_t squareStart = 0;

		// if a long press is selected and you're checking value of start or end pad
		// display value at very first or very last node
		if (multiPadPressSelected && ((leftPadSelectedX == xDisplay) || (rightPadSelectedX == xDisplay))) {
			if (leftPadSelectedX == xDisplay) {
				squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
			}
			else {
				int32_t squareRightEdge = getPosFromSquare(rightPadSelectedX + 1, xScroll, xZoom);
				squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
			}
		}
		// display pad's middle value
		else {
			squareStart = getMiddlePosFromSquare(xDisplay, effectiveLength, xScroll, xZoom);
		}

		updateAutomationModPosition(modelStackWithParam, squareStart);

		if (!multiPadPressSelected) {
			leftPadSelectedX = xDisplay;
		}
	}

	else if (modelStackWithParam && modelStackWithParam->autoParam) {

		uint32_t squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);

		if (squareStart < effectiveLength) {
			// use default interpolation settings
			initInterpolation();

			int32_t newKnobPos = calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, yDisplay);
			setAutomationParameterValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength,
			                            xScroll, xZoom);
		}
	}
}

int32_t AutomationSoundEditorView::calculateAutomationKnobPosForPadPress(ModelStackWithAutoParam* modelStackWithParam,
                                                                   OutputType outputType, int32_t yDisplay) {

	int32_t newKnobPos = 0;
	params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

	if (middlePadPressSelected) {
		newKnobPos = calculateAutomationKnobPosForMiddlePadPress(kind, yDisplay);
	}
	else {
		newKnobPos = calculateAutomationKnobPosForSinglePadPress(kind, yDisplay);
	}

	// for Midi Clips, maxKnobPos = 127
	if (outputType == OutputType::MIDI_OUT && newKnobPos == kMaxKnobPos) {
		newKnobPos -= 1; // 128 - 1 = 127
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos
	// set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

// calculates what the new parameter value is when you press a second pad in the same column
// middle value is calculated by taking average of min and max value of the range for the two pad
// presses
int32_t AutomationSoundEditorView::calculateAutomationKnobPosForMiddlePadPress(params::Kind kind, int32_t yDisplay) {
	int32_t newKnobPos = 0;

	int32_t yMin = yDisplay < leftPadSelectedY ? yDisplay : leftPadSelectedY;
	int32_t yMax = yDisplay > leftPadSelectedY ? yDisplay : leftPadSelectedY;
	int32_t minKnobPos = 0;
	int32_t maxKnobPos = 0;

	if (kind == params::Kind::PATCH_CABLE) {
		minKnobPos = patchCableMinPadDisplayValues[yMin];
		maxKnobPos = patchCableMaxPadDisplayValues[yMax];
	}
	else {
		minKnobPos = nonPatchCableMinPadDisplayValues[yMin];
		maxKnobPos = nonPatchCableMaxPadDisplayValues[yMax];
	}

	newKnobPos = (minKnobPos + maxKnobPos) >> 1;

	return newKnobPos;
}

// calculates what the new parameter value is when you press a single pad
int32_t AutomationSoundEditorView::calculateAutomationKnobPosForSinglePadPress(params::Kind kind, int32_t yDisplay) {
	int32_t newKnobPos = 0;

	// patch cable
	if (kind == params::Kind::PATCH_CABLE) {
		newKnobPos = patchCablePadPressValues[yDisplay];
	}
	// non patch cable
	else {
		newKnobPos = nonPatchCablePadPressValues[yDisplay];
	}

	return newKnobPos;
}

// takes care of setting the automation values for the two pads pressed and the pads in between
void AutomationSoundEditorView::handleAutomationMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                        int32_t firstPadX, int32_t firstPadY, int32_t secondPadX,
                                                        int32_t secondPadY, int32_t effectiveLength, int32_t xScroll,
                                                        int32_t xZoom, bool modEncoderAction) {

	int32_t secondPadLeftEdge = getPosFromSquare(secondPadX, xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadLeftEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t firstPadLeftEdge = getPosFromSquare(firstPadX, xScroll, xZoom);
		int32_t secondPadRightEdge = getPosFromSquare(secondPadX + 1, xScroll, xZoom);

		int32_t firstPadValue = 0;
		int32_t secondPadValue = 0;

		// if we're updating the long press values via mod encoder action, then get current values of
		// pads pressed and re-interpolate
		if (modEncoderAction) {
			firstPadValue = getAutomationParameterKnobPos(modelStackWithParam, firstPadLeftEdge) + kKnobPosOffset;

			uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;

			secondPadValue = getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;
		}

		// otherwise if it's a regular long press, calculate values from the y position of the pads
		// pressed
		else {
			OutputType outputType = clip->output->type;
			firstPadValue =
			    calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, firstPadY) + kKnobPosOffset;
			secondPadValue =
			    calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, secondPadY) + kKnobPosOffset;
		}

		// clear existing nodes from long press range

		// reset interpolation settings to default
		initInterpolation();

		// set value for beginning pad press at the very first node position within that pad
		setAutomationParameterValue(modelStackWithParam, firstPadValue - kKnobPosOffset, firstPadLeftEdge, firstPadX,
		                            effectiveLength, xScroll, xZoom);

		// set value for ending pad press at the very last node position within that pad
		int32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		setAutomationParameterValue(modelStackWithParam, secondPadValue - kKnobPosOffset, squareStart, secondPadX,
		                            effectiveLength, xScroll, xZoom);

		// converting variables to float for more accurate interpolation calculation
		float firstPadValueFloat = static_cast<float>(firstPadValue);
		float firstPadXFloat = static_cast<float>(firstPadLeftEdge);
		float secondPadValueFloat = static_cast<float>(secondPadValue);
		float secondPadXFloat = static_cast<float>(squareStart);

		// loop from first pad to last pad, setting values for nodes in between
		// these values will serve as "key frames" for the interpolation to flow through
		for (int32_t x = firstPadX; x <= secondPadX; x++) {

			int32_t newKnobPos = 0;
			uint32_t squareWidth = 0;

			// we've already set the value for the very first node corresponding to the first pad above
			// now we will set the value for the remaining nodes within the first pad
			if (x == firstPadX) {
				squareStart = getPosFromSquare(x, xScroll, xZoom) + kParamNodeWidth;
				squareWidth = getSquareWidth(x, effectiveLength, xScroll, xZoom) - kParamNodeWidth;
			}

			// we've already set the value for the very last node corresponding to the second pad above
			// now we will set the value for the remaining nodes within the second pad
			else if (x == secondPadX) {
				squareStart = getPosFromSquare(x, xScroll, xZoom);
				squareWidth = getSquareWidth(x, effectiveLength, xScroll, xZoom) - kParamNodeWidth;
			}

			// now we will set the values for the nodes between the first and second pad's pressed
			else {
				squareStart = getPosFromSquare(x, xScroll, xZoom);
				squareWidth = getSquareWidth(x, effectiveLength, xScroll, xZoom);
			}

			// linear interpolation formula to calculate the value of the pads
			// f(x) = A + (x - Ax) * ((B - A) / (Bx - Ax))
			float newKnobPosFloat = std::round(firstPadValueFloat
			                                   + (((squareStart - firstPadXFloat) / kParamNodeWidth)
			                                      * ((secondPadValueFloat - firstPadValueFloat)
			                                         / ((secondPadXFloat - firstPadXFloat) / kParamNodeWidth))));

			newKnobPos = static_cast<int32_t>(newKnobPosFloat);
			newKnobPos = newKnobPos - kKnobPosOffset;

			// if interpolation is off, values for nodes in between first and second pad will not be set
			// in a staggered/step fashion
			if (interpolation) {
				interpolationBefore = true;
				interpolationAfter = true;
			}

			// set value for pads in between
			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
			                                                          squareWidth);
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
			                                                          squareWidth);

			if (!onArrangerView) {
				modelStackWithParam->getTimelineCounter()->instrumentBeenEdited();
			}
		}

		// reset interpolation settings to off
		initInterpolation();

		// render the multi pad press
		uiNeedsRendering(this);
	}
}

// new function to render display when a long press is active
// on OLED this will display the left and right position in a long press on the screen
// on 7SEG this will display the position of the last selected pad
// also updates LED indicators. bottom LED indicator = left pad, top LED indicator = right pad
void AutomationSoundEditorView::renderAutomationDisplayForMultiPadPress(ModelStackWithAutoParam* modelStackWithParam,
                                                                  Clip* clip, int32_t effectiveLength, int32_t xScroll,
                                                                  int32_t xZoom, int32_t xDisplay,
                                                                  bool modEncoderAction) {

	int32_t secondPadLeftEdge = getPosFromSquare(rightPadSelectedX, xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadLeftEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t firstPadLeftEdge = getPosFromSquare(leftPadSelectedX, xScroll, xZoom);
		int32_t secondPadRightEdge = getPosFromSquare(rightPadSelectedX + 1, xScroll, xZoom);

		int32_t knobPosLeft = getAutomationParameterKnobPos(modelStackWithParam, firstPadLeftEdge) + kKnobPosOffset;

		uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		int32_t knobPosRight = getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;

		if (xDisplay != kNoSelection) {
			if (leftPadSelectedX == xDisplay) {
				squareStart = firstPadLeftEdge;
				lastPadSelectedKnobPos = knobPosLeft;
			}
			else {
				lastPadSelectedKnobPos = knobPosRight;
			}
		}

		if (display->haveOLED()) {
			renderDisplay(knobPosLeft, knobPosRight);
		}
		// display pad value of second pad pressed
		else {
			if (modEncoderAction) {
				renderDisplay(lastPadSelectedKnobPos);
			}
			else {
				renderDisplay();
			}
		}

		setAutomationKnobIndicatorLevels(modelStackWithParam, knobPosLeft, knobPosRight);

		// update position of mod controllable stack
		updateAutomationModPosition(modelStackWithParam, squareStart, false, false);
	}
}

// used to calculate new knobPos when you turn the mod encoders (gold knobs)
int32_t AutomationSoundEditorView::calculateAutomationKnobPosForModEncoderTurn(ModelStackWithAutoParam* modelStackWithParam,
                                                                         int32_t knobPos, int32_t offset) {

	// adjust the current knob so that it is within the range of 0-128 for calculation purposes
	knobPos = knobPos + kKnobPosOffset;

	int32_t newKnobPos = 0;

	if ((knobPos + offset) < 0) {
		params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
		if (kind == params::Kind::PATCH_CABLE) {
			if ((knobPos + offset) >= -kMaxKnobPos) {
				newKnobPos = knobPos + offset;
			}
			else if ((knobPos + offset) < -kMaxKnobPos) {
				newKnobPos = -kMaxKnobPos;
			}
			else {
				newKnobPos = knobPos;
			}
		}
		else {
			newKnobPos = knobPos;
		}
	}
	else if ((knobPos + offset) <= kMaxKnobPos) {
		newKnobPos = knobPos + offset;
	}
	else if ((knobPos + offset) > kMaxKnobPos) {
		newKnobPos = kMaxKnobPos;
	}
	else {
		newKnobPos = knobPos;
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos
	// set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}
