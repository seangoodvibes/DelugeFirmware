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

#include "gui/views/automation/note_editor_view.h"
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

namespace params = deluge::modulation::params;
using deluge::modulation::params::kNoParamID;
using deluge::modulation::params::ParamType;
using deluge::modulation::params::patchedParamShortcuts;
using deluge::modulation::params::unpatchedGlobalParamShortcuts;
using deluge::modulation::params::unpatchedNonGlobalParamShortcuts;

using namespace deluge::gui;

// colours for the velocity editor

const RGB velocityRowColour[kDisplayHeight] = {{0, 0, 255},   {36, 0, 219}, {73, 0, 182}, {109, 0, 146},
                                               {146, 0, 109}, {182, 0, 73}, {219, 0, 36}, {255, 0, 0}};

const RGB velocityRowTailColour[kDisplayHeight] = {{2, 2, 53},  {9, 2, 46},  {17, 2, 38}, {24, 2, 31},
                                                   {31, 2, 24}, {38, 2, 17}, {46, 2, 9},  {53, 2, 2}};

const RGB velocityRowBlurColour[kDisplayHeight] = {{71, 71, 111}, {72, 66, 101}, {73, 62, 90}, {74, 57, 80},
                                                   {76, 53, 70},  {77, 48, 60},  {78, 44, 49}, {79, 39, 39}};

AutomationNoteEditorView automationNoteEditorView{};

AutomationNoteEditorView::AutomationNoteEditorView() {

}

// gets the length of the note row, renders the pads corresponding to current note parameter values set up to the
// note row length renders the undefined area of the note row that the user can't interact with
void AutomationNoteEditorView::renderNoteEditor(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
                                              RGB image[][kDisplayWidth + kSideBarWidth],
                                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                              int32_t renderWidth, int32_t xScroll, uint32_t xZoom,
                                              int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea,
                                              SquareInfo& squareInfo) {
	if (modelStackWithNoteRow->getNoteRowAllowNull()) {
		renderNoteColumn(modelStackWithNoteRow, clip, image, occupancyMask, xDisplay, xScroll, xZoom, squareInfo);
	}
	if (drawUndefinedArea) {
		AutomationView::renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth, this,
		                    currentSong->tripletsOn, xDisplay);
	}
}

/// render each square in each column of the note editor grid
void AutomationNoteEditorView::renderNoteColumn(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
                                              RGB image[][kDisplayWidth + kSideBarWidth],
                                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
                                              int32_t xScroll, int32_t xZoom, SquareInfo& squareInfo) {
	int32_t value = 0;

	if (automationParamType == AutomationParamType::NOTE_VELOCITY) {
		value = squareInfo.averageVelocity;
	}

	// iterate through each square
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		renderNoteSquare(image, occupancyMask, xDisplay, yDisplay, squareInfo.squareType, value);
	}
}

/// render column for note parameter
void AutomationNoteEditorView::renderNoteSquare(RGB image[][kDisplayWidth + kSideBarWidth],
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
	// pad selection mode, render cursor
	if (padSelectionOn && ((xDisplay == leftPadSelectedX) || (xDisplay == rightPadSelectedX))) {
		if (doRender) {
			pixel = velocityRowBlurColour[yDisplay];
		}
		else {
			pixel = colours::grey;
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
}

/// toggle velocity pad selection mode on / off
bool AutomationNoteEditorView::toggleVelocityPadSelectionMode(SquareInfo& squareInfo) {
	// enter/exit pad selection mode
	if (padSelectionOn) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_OFF));

		initPadSelection();
	}
	else {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_ON));

		padSelectionOn = true;
		blinkPadSelectionShortcut();

		// display only left cursor
		leftPadSelectedX = 0;
		rightPadSelectedX = kNoSelection;
		numNotesSelected = squareInfo.numNotes;

		// when entering velocity pad selection mode, record note selection
		// but don't record pad press yet if there are no notes in this square
		// because recording a pad press for an empty square creates a note, and we don't want that yet
		// (we'll do that when we try to adjust square velocity)
		if (numNotesSelected != 0) {
			// select note if there are notes in this square
			recordNoteEditPadAction(leftPadSelectedX, 1);
			instrumentClipView.dontDeleteNotesOnDepress();
		}
	}
	uiNeedsRendering(this);
	renderDisplay();
	return true;
}

// note edit pad action
// handles single and multi pad presses for note parameter editing (e.g. velocity)
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationNoteEditorView::noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                               InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity,
                                               int32_t effectiveLength, SquareInfo& squareInfo) {
	if (automationParamType == AutomationParamType::NOTE_VELOCITY) {
		if (padSelectionOn) {
			velocityPadSelectionAction(modelStackWithNoteRow, clip, x, y, velocity, squareInfo);
		}
		else {
			velocityEditPadAction(modelStackWithNoteRow, noteRow, clip, x, y, velocity, effectiveLength, squareInfo);
		}
	}
}

// handle's what happens when you select columns in velocity pad selection mode
void AutomationNoteEditorView::velocityPadSelectionAction(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                        InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity,
                                                        SquareInfo& squareInfo) {

	if (velocity) {
		// if selection has changed and note was previously selected, release previous press
		// if we recorded the previous pad that was pressed
		if (leftPadSelectedX != kNoSelection && isUIModeActive(UI_MODE_NOTES_PRESSED)) {
			recordNoteEditPadAction(leftPadSelectedX, 0);
		}

		// if we selected a new pad, record new press
		// don't record pad press yet if there are no notes in this square
		// because recording a pad press for an empty square creates a note, and we don't want that yet
		// (we'll do that when we try to adjust square velocity)
		if (leftPadSelectedX != x && squareInfo.numNotes != 0) {
			// record new note selection
			recordNoteEditPadAction(x, 1);
			instrumentClipView.dontDeleteNotesOnDepress();
		}

		if (leftPadSelectedX != x) {
			// store new pad selection
			leftPadSelectedX = x;
			numNotesSelected = squareInfo.numNotes;
		}
		else {
			// de-select pad selection
			leftPadSelectedX = kNoSelection;
			numNotesSelected = 0;
		}

		// refresh grid and display
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}
	selectedPadPressed = velocity;
	renderDisplay();
}

// velocity edit pad action
void AutomationNoteEditorView::velocityEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                                   InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity,
                                                   int32_t effectiveLength, SquareInfo& squareInfo) {
	// save pad selected
	leftPadSelectedX = x;

	// calculate new velocity based on Y of pad pressed
	int32_t newVelocity = getVelocityFromY(y);

	// middle pad press variables
	middlePadPressSelected = false;

	// multi pad press variables
	multiPadPressSelected = false;
	SquareInfo rowSquareInfo[kDisplayWidth];
	int32_t multiPadPressVelocityIncrement = 0;

	// update velocity editor rendering
	bool refreshVelocityEditor = false;

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
					middlePadPressSelected = true;

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
					leftPadSelectedX = firstPadX > x ? x : firstPadX;
					rightPadSelectedX = firstPadX > x ? firstPadX : x;

					int32_t numSquares = 0;
					// find total number of notes in note row (excluding the first note)
					for (int32_t i = leftPadSelectedX; i <= rightPadSelectedX; i++) {
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

					if (leftPadSelectedX == firstPadX) { // then left pad is the first press
						leftPadSelectedVelocity = rowSquareInfo[leftPadSelectedX].averageVelocity;
						leftPadSelectedY = getYFromVelocity(leftPadSelectedVelocity);
						rightPadSelectedVelocity = getVelocityFromY(y);
						rightPadSelectedY = y;
					}
					else { // then left pad is the second press
						leftPadSelectedVelocity = getVelocityFromY(y);
						leftPadSelectedY = y;
						rightPadSelectedVelocity = rowSquareInfo[rightPadSelectedX].averageVelocity;
						rightPadSelectedY = getYFromVelocity(rightPadSelectedVelocity);
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
					multiPadPressSelected = true;
					multiPadPressActive = true;

					break;
				}
			}
		}
	}

	// if middle pad press was selected, set the velocity to middle velocity between two pads pressed
	if (middlePadPressSelected) {
		setVelocity(modelStackWithNoteRow, noteRow, x, newVelocity);
		refreshVelocityEditor = true;
	}
	// if multi pad (long) press was selected, set the velocity of all the notes between the two pad presses
	else if (multiPadPressSelected) {
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
		}
		// note(s) exists, adjust velocity of existing notes
		else {
			adjustNoteVelocity(modelStackWithNoteRow, noteRow, x, velocity, newVelocity, squareInfo.squareType);
			refreshVelocityEditor = true;
		}
	}
	// if no note exists and you're trying to remove a note (y == 0 && squareInfo.numNotes == 0),
	// well no need to do anything

	if (multiPadPressActive && !isUIModeActive(UI_MODE_NOTES_PRESSED)) {
		multiPadPressActive = false;
	}

	if (refreshVelocityEditor) {
		// refresh grid and update default velocity on the display
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
		// if holding a multi pad press, render left and right velocity of the multi pad press
		if (multiPadPressActive) {
			int32_t leftPadSelectedVelocity = getVelocityFromY(leftPadSelectedY);
			int32_t rightPadSelectedVelocity = getVelocityFromY(rightPadSelectedY);
			if (display->haveOLED()) {
				renderDisplay(leftPadSelectedVelocity, rightPadSelectedVelocity);
			}
			else {
				// for 7seg, render value of last pad pressed
				renderDisplay(leftPadSelectedX == x ? leftPadSelectedVelocity : rightPadSelectedVelocity);
			}
		}
		else {
			renderDisplay();
		}
	}
}

// convert y of pad press into velocity value between 1 and 127
int32_t AutomationNoteEditorView::getVelocityFromY(int32_t y) {
	int32_t velocity = std::clamp<int32_t>(nonPatchCablePadPressValues[y], 1, 127);
	return velocity;
}

// convert velocity of a square into y
int32_t AutomationNoteEditorView::getYFromVelocity(int32_t velocity) {
	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (nonPatchCableMinPadDisplayValues[i] <= velocity && velocity <= nonPatchCableMaxPadDisplayValues[i]) {
			return i;
		}
	}
	return kNoSelection;
}

// add note and set velocity
void AutomationNoteEditorView::addNoteWithNewVelocity(int32_t x, int32_t velocity, int32_t newVelocity) {
	if (velocity) {
		// we change the instrument default velocity because it is used for new notes
		getCurrentInstrument()->defaultVelocity = newVelocity;
	}

	// record pad press and release
	// adds note with new velocity set
	recordNoteEditPadAction(x, velocity);
}

// adjust velocity of existing notes
void AutomationNoteEditorView::adjustNoteVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
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
void AutomationNoteEditorView::setVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x,
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

				// Rohan: Get the average. Ideally we'd have done this when first selecting the note too, but I didn't

				// Sean: not sure how getting the average when first selecting the note would help because the average
				// will change based on the velocity adjustment happening here.

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
void AutomationNoteEditorView::setVelocityRamp(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                             SquareInfo rowSquareInfo[kDisplayWidth], int32_t velocityIncrement) {
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	int32_t startVelocity = getVelocityFromY(leftPadSelectedY);
	int32_t velocityValue = 0;
	int32_t squaresProcessed = 0;

	for (int32_t i = leftPadSelectedX; i <= rightPadSelectedX; i++) {
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
void AutomationNoteEditorView::recordNoteEditPadAction(int32_t x, int32_t velocity) {
	instrumentClipView.editPadAction(velocity, instrumentClipView.lastAuditionedYDisplay, x,
	                                 currentSong->xZoom[NAVIGATION_CLIP]);
}
