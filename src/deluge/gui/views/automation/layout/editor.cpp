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
#include "gui/views/automation/layout/editor/note.h"
#include "gui/views/automation/layout/editor/sound.h"
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
	automationLayoutEditorSound.renderAutomationEditor(modelStackWithParam, clip, image, occupancyMask, renderWidth,
	                                                   xScroll, xZoom, effectiveLength, xDisplay, drawUndefinedArea,
	                                                   kind, isBipolar);
	if (drawUndefinedArea) {
		renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth,
		                    (TimelineView*)getRootUI(), currentSong->tripletsOn, xDisplay);
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
	return automationLayoutEditorNote.renderNoteEditor(modelStackWithNoteRow, clip, image, occupancyMask, renderWidth,
	                                                   xScroll, xZoom, effectiveLength, xDisplay, drawUndefinedArea,
	                                                   squareInfo);
}

// occupancyMask now optional
void AutomationLayoutEditor::renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay,
                                                 RGB image[][kDisplayWidth + kSideBarWidth],
                                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                 int32_t imageWidth, TimelineView* timelineView, bool tripletsOnHere,
                                                 int32_t xDisplay) {
	// If the visible pane extends beyond the end of the Clip, draw it as grey
	int32_t greyStart = timelineView->getSquareFromPos(lengthToDisplay - 1, nullptr, xScroll, xZoom) + 1;

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
	return automationLayoutEditorSound.renderAutomationEditorDisplayOLED(canvas, clip, outputType, knobPosLeft,
	                                                                     knobPosRight);
}

void AutomationLayoutEditor::renderNoteEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                         InstrumentClip* clip, OutputType outputType,
                                                         int32_t knobPosLeft, int32_t knobPosRight) {
	return automationLayoutEditorNote.renderNoteEditorDisplayOLED(canvas, clip, outputType, knobPosLeft, knobPosRight);
}

void AutomationLayoutEditor::renderAutomationEditorDisplay7SEG(Clip* clip, OutputType outputType, int32_t knobPosLeft,
                                                               bool modEncoderAction) {
	return automationLayoutEditorSound.renderAutomationEditorDisplay7SEG(clip, outputType, knobPosLeft,
	                                                                     modEncoderAction);
}

void AutomationLayoutEditor::renderNoteEditorDisplay7SEG(InstrumentClip* clip, OutputType outputType,
                                                         int32_t knobPosLeft) {
	return automationLayoutEditorNote.renderNoteEditorDisplay7SEG(clip, outputType, knobPosLeft);
}

/// if we're entering note editor, we want the selected drum to be visible and in sync with lastAuditionedYDisplay
/// so we'll check if the yDisplay of the selectedDrum is in sync with the lastAuditionedYDisplay
/// if they're not in sync, we'll sync them up by performing a vertical scroll
void AutomationLayoutEditor::potentiallyVerticalScrollToSelectedDrum(InstrumentClip* clip, Output* output) {
	return automationLayoutEditorNote.potentiallyVerticalScrollToSelectedDrum(clip, output);
}

// this function obtains a parameters value and converts it to a knobPos
// the knobPos is used for rendering the current parameter values in the automation editor
// it's also used for obtaining the start and end position values for a multi pad press
// and also used for increasing/decreasing parameter values with the mod encoders
int32_t AutomationLayoutEditor::getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack,
                                                              uint32_t squareStart) {
	return automationLayoutEditorSound.getAutomationParameterKnobPos(modelStack, squareStart);
}

// this function writes the new values calculated by the handleAutomationSinglePadPress and
// handleAutomationMultiPadPress functions
void AutomationLayoutEditor::setAutomationParameterValue(ModelStackWithAutoParam* modelStack, int32_t knobPos,
                                                         int32_t squareStart, int32_t xDisplay, int32_t effectiveLength,
                                                         int32_t xScroll, int32_t xZoom, bool modEncoderAction) {

	return automationLayoutEditorSound.setAutomationParameterValue(modelStack, knobPos, squareStart, xDisplay,
	                                                               effectiveLength, xScroll, xZoom, modEncoderAction);
}

void AutomationLayoutEditor::initInterpolation() {
	return automationLayoutEditorSound.initInterpolation();
}

// automation edit pad action
// handles single and multi pad presses for automation editing
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationLayoutEditor::automationEditPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                     int32_t xDisplay, int32_t yDisplay, int32_t velocity,
                                                     int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	return automationLayoutEditorSound.automationEditPadAction(modelStackWithParam, clip, xDisplay, yDisplay, velocity,
	                                                           effectiveLength, xScroll, xZoom);
}

/// toggle automation interpolation on / off
bool AutomationLayoutEditor::toggleAutomationInterpolation() {
	return automationLayoutEditorSound.toggleAutomationInterpolation();
}

/// toggle automation pad selection mode on / off
bool AutomationLayoutEditor::toggleAutomationPadSelectionMode(ModelStackWithAutoParam* modelStackWithParam,
                                                              int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	return automationLayoutEditorSound.toggleAutomationPadSelectionMode(modelStackWithParam, effectiveLength, xScroll,
	                                                                    xZoom);
}

// note edit pad action
// handles single and multi pad presses for note parameter editing (e.g. velocity)
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationLayoutEditor::noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                               InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity,
                                               int32_t effectiveLength, SquareInfo& squareInfo) {
	return automationLayoutEditorNote.noteEditPadAction(modelStackWithNoteRow, noteRow, clip, x, y, velocity,
	                                                    effectiveLength, squareInfo);
}

bool AutomationLayoutEditor::automationModEncoderActionForSelectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                                      int32_t whichModEncoder, int32_t offset,
                                                                      int32_t effectiveLength) {
	return automationLayoutEditorSound.automationModEncoderActionForSelectedPad(modelStackWithParam, whichModEncoder,
	                                                                            offset, effectiveLength);
}

void AutomationLayoutEditor::automationModEncoderActionForUnselectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                                        int32_t whichModEncoder, int32_t offset,
                                                                        int32_t effectiveLength) {
	return automationLayoutEditorSound.automationModEncoderActionForUnselectedPad(modelStackWithParam, whichModEncoder,
	                                                                              offset, effectiveLength);
}

void AutomationLayoutEditor::copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xScroll,
                                            int32_t xZoom) {
	return automationLayoutEditorSound.copyAutomation(modelStackWithParam, clip, xScroll, xZoom);
}

void AutomationLayoutEditor::pasteAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                             int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	return automationLayoutEditorSound.pasteAutomation(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
}

// adjust the LED meters and update the display

/*updated function for displaying automation when playback is enabled (called from ui_timer_manager).
Also used internally in the automation instrument clip view for updating the display and led
indicators.*/

void AutomationLayoutEditor::displayAutomation(bool padSelected, bool updateDisplay) {
	return automationLayoutEditorSound.displayAutomation(padSelected, updateDisplay);
}

// calculates the length of the arrangement timeline, clip or the length of the kit row
// if you're in a synth clip, kit clip with affect entire enabled or midi clip it returns clip length
// if you're in a kit clip with affect entire disabled and a row selected, it returns kit row length
int32_t AutomationLayoutEditor::getEffectiveLength(ModelStackWithTimelineCounter* modelStack) {
	return automationLayoutEditorSound.getEffectiveLength(modelStack);
}

uint32_t AutomationLayoutEditor::getSquareWidth(int32_t square, int32_t effectiveLength, int32_t xScroll,
                                                int32_t xZoom) {
	return automationLayoutEditorSound.getSquareWidth(square, effectiveLength, xScroll, xZoom);
}

// when pressing on a single pad, you want to display the value of the middle node within that square
// as that is the most accurate value that represents that square
uint32_t AutomationLayoutEditor::getMiddlePosFromSquare(int32_t xDisplay, int32_t effectiveLength, int32_t xScroll,
                                                        int32_t xZoom) {
	return automationLayoutEditorSound.getMiddlePosFromSquare(xDisplay, effectiveLength, xScroll, xZoom);
}

// call instrument clip view edit pad action function to process velocity pad press actions
void AutomationLayoutEditor::recordNoteEditPadAction(int32_t x, int32_t velocity) {
	return automationLayoutEditorNote.recordNoteEditPadAction(x, velocity);
}
