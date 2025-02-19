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

#include "gui/views/automation/layout/editor/note.h"
#include "definitions_cxx.hpp"
#include "gui/views/automation/layout/editor/note/velocity.h"
#include "gui/views/instrument_clip_view.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/note/note.h"
#include "model/song/song.h"

using namespace deluge::gui;

PLACE_SDRAM_BSS AutomationLayoutEditorNote automationLayoutEditorNote{};

AutomationLayoutEditorNote::AutomationLayoutEditorNote() {
}

// gets the length of the note row, renders the pads corresponding to current note parameter values set up to the
// note row length renders the undefined area of the note row that the user can't interact with
void AutomationLayoutEditorNote::renderNoteEditor(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
                                                  RGB image[][kDisplayWidth + kSideBarWidth],
                                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                  int32_t renderWidth, int32_t xScroll, uint32_t xZoom,
                                                  int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea,
                                                  SquareInfo& squareInfo) {
	if (modelStackWithNoteRow->getNoteRowAllowNull()) {
		automationLayoutEditorNoteVelocity.renderNoteColumn(modelStackWithNoteRow, clip, image, occupancyMask, xDisplay,
		                                                    xScroll, xZoom, squareInfo);
	}
}

void AutomationLayoutEditorNote::renderNoteEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                             InstrumentClip* clip, OutputType outputType,
                                                             int32_t knobPosLeft, int32_t knobPosRight) {
	return automationLayoutEditorNoteVelocity.renderNoteEditorDisplayOLED(canvas, clip, outputType, knobPosLeft,
	                                                                      knobPosRight);
}

void AutomationLayoutEditorNote::renderNoteEditorDisplay7SEG(InstrumentClip* clip, OutputType outputType,
                                                             int32_t knobPosLeft) {
	return automationLayoutEditorNoteVelocity.renderNoteEditorDisplay7SEG(clip, outputType, knobPosLeft);
}

// horizontal encoder actions:
// scroll left / right
// zoom in / out
// adjust clip length
// shift automations left / right
// adjust velocity in note editor
ActionResult AutomationLayoutEditorNote::horizontalEncoderAction(int32_t offset) {
	instrumentClipView.rotateNoteRowHorizontally(offset);
	return ActionResult::DEALT_WITH;
}

/// if we're entering note editor, we want the selected drum to be visible and in sync with lastAuditionedYDisplay
/// so we'll check if the yDisplay of the selectedDrum is in sync with the lastAuditionedYDisplay
/// if they're not in sync, we'll sync them up by performing a vertical scroll
void AutomationLayoutEditorNote::potentiallyVerticalScrollToSelectedDrum(InstrumentClip* clip, Output* output) {
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

// note edit pad action
// handles single and multi pad presses for note parameter editing (e.g. velocity)
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationLayoutEditorNote::noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                                   InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity,
                                                   int32_t effectiveLength, SquareInfo& squareInfo) {
	return automationLayoutEditorNoteVelocity.velocityEditPadAction(modelStackWithNoteRow, noteRow, clip, x, y,
	                                                                velocity, effectiveLength, squareInfo);
}

// call instrument clip view edit pad action function to process velocity pad press actions
void AutomationLayoutEditorNote::recordNoteEditPadAction(int32_t x, int32_t velocity) {
	instrumentClipView.editPadAction(velocity, instrumentClipView.lastAuditionedYDisplay, x,
	                                 currentSong->xZoom[NAVIGATION_CLIP]);
}
