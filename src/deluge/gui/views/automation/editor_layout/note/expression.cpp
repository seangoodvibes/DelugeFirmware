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

#include "gui/views/automation/editor_layout/note/expression.h"
#include "gui/views/instrument_clip_view.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note.h"

// namespace deluge::gui::views::automation::editor_layout::note {

namespace params = deluge::modulation::params;

using namespace deluge::gui;

PLACE_SDRAM_BSS AutomationEditorLayoutNoteExpression automationEditorLayoutNoteExpression{};

// expression edit pad action
void AutomationEditorLayoutNoteExpression::expressionEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                                   NoteRow* noteRow, InstrumentClip* clip, int32_t x,
                                                                   int32_t y, int32_t velocity, int32_t effectiveLength,
                                                                   SquareInfo& squareInfo) {
	// no note, can't edit note expression
	if (squareInfo.numNotes == 0) {
		return;
	}

	// blurred note, can't edit note expression
	if (squareInfo.squareType == SQUARE_BLURRED) {
		return;
	}

	int32_t max_x = effectiveLength;
	Note* selected_note = squareInfo.firstNote;
	int32_t noteEnd = selected_note->pos + selected_note->getLength();
	// if the note is not wrapped, then max editable length is the note end
	if (noteEnd <= squareInfo.squareEndPos) {
		max_x = noteEnd;
	}

	params::Kind paramKind = params::Kind::EXPRESSION;
	int32_t paramID = Expression::X_PITCH_BEND;

	int32_t xScroll = currentSong->xScroll[AutomationEditorLayoutModControllable::getNavSysId()];
	int32_t xZoom = currentSong->xZoom[AutomationEditorLayoutModControllable::getNavSysId()];

	ModelStackWithAutoParam* modelStackWithParam =
	    noteRow->getModelStackWithParam(modelStackWithNoteRow, paramID, paramKind, false);
	AutomationEditorLayoutModControllable::automationEditPadAction(modelStackWithParam, clip, x, y, velocity, max_x,
	                                                               xScroll, xZoom);
}

// }; // namespace deluge::gui::views::automation::editor_layout::note
