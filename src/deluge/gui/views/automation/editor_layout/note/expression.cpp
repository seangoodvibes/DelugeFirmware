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
#include "gui/views/automation/editor_layout/note/expression_values.h"
#include "gui/views/automation/editor_layout/note/velocity.h"
#include "gui/views/instrument_clip_view.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note.h"
#include "modulation/params/param_set.h"

// namespace deluge::gui::views::automation::editor_layout::note {

namespace params = deluge::modulation::params;

using namespace deluge::gui;

PLACE_SDRAM_BSS AutomationEditorLayoutNoteExpression automationEditorLayoutNoteExpression{};

int32_t AutomationEditorLayoutNoteExpression::getExpressionDimension() {
	switch (getAutomationParamType()) {
	case AutomationParamType::NOTE_Y_SLIDE_TIMBRE:
		return Expression::Y_SLIDE_TIMBRE;
	case AutomationParamType::NOTE_Z_PRESSURE:
		return Expression::Z_PRESSURE;
	default:
		return Expression::X_PITCH_BEND;
	}
}

const char* AutomationEditorLayoutNoteExpression::getParameterName() {
	switch (getExpressionDimension()) {
	case Expression::Y_SLIDE_TIMBRE:
		return "Note Y / Timbre";
	case Expression::Z_PRESSURE:
		return "Note Z / Pressure";
	default:
		return "Note X / Pitch";
	}
}

void AutomationEditorLayoutNoteExpression::renderNoteColumn(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                            InstrumentClip* clip,
                                                            RGB image[][kDisplayWidth + kSideBarWidth],
                                                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                            int32_t xDisplay, int32_t xScroll, int32_t xZoom,
                                                            SquareInfo& squareInfo) {
	int32_t value = 0;
	int32_t dimension = getExpressionDimension();
	auto* noteRow = modelStackWithNoteRow->getNoteRow();
	// Rendering must not allocate expression data for untouched rows.
	auto* expression = noteRow->paramManager.getExpressionParamSet();
	if (expression && squareInfo.numNotes) {
		auto* param = noteRow->getModelStackWithParam(modelStackWithNoteRow, dimension, params::Kind::EXPRESSION,
		                                              clip->output->type == OutputType::KIT);
		int32_t pos = squareInfo.squareStartPos;
		if (squareInfo.squareType == SQUARE_BLURRED) {
			pos = squareInfo.firstNote->pos;
		}
		value = param->autoParam->getValueAtPos(pos, param);
	}
	bool bipolar = dimension != Expression::Z_PRESSURE;
	int32_t row = 0;
	for (int32_t y = 1; y < kDisplayHeight; ++y) {
		if (value >= note_expression::valueFromRow(y, bipolar)) {
			row = y;
		}
	}
	// Reuse the velocity editor's head / tail / blurred colours and bar rendering.
	constexpr int32_t barValues[] = {0, 17, 33, 49, 65, 81, 97, 113};
	SquareInfo displaySquare = squareInfo;
	displaySquare.averageVelocity = barValues[row];
	automationEditorLayoutNoteVelocity.renderNoteColumn(modelStackWithNoteRow, clip, image, occupancyMask, xDisplay,
	                                                    xScroll, xZoom, displaySquare);
}

void AutomationEditorLayoutNoteExpression::expressionEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                                   NoteRow* noteRow, InstrumentClip* clip, int32_t x,
                                                                   int32_t y, int32_t velocity, int32_t effectiveLength,
                                                                   SquareInfo& squareInfo) {
	if (!velocity) {
		actionLogger.closeAction(ActionType::NOTE_EDIT);
		return;
	}
	// Zoom in to disambiguate squares containing multiple notes.
	if (squareInfo.numNotes != 1 || !squareInfo.firstNote) {
		return;
	}
	auto* note = squareInfo.firstNote;
	auto region = note_expression::editableRegion(note->pos, note->getLength(), effectiveLength,
	                                              squareInfo.squareStartPos, squareInfo.squareEndPos);
	if (region.end <= region.start) {
		return;
	}
	int32_t dimension = getExpressionDimension();
	auto* param = noteRow->getModelStackWithParam(modelStackWithNoteRow, dimension, params::Kind::EXPRESSION,
	                                              clip->output->type == OutputType::KIT);
	if (!param || !param->autoParam) {
		return;
	}
	bool bipolar = dimension != Expression::Z_PRESSURE;
	int32_t value = note_expression::valueFromRow(y, bipolar);
	// AutoParam preserves values outside this region and records the change for undo.
	param->autoParam->setValueForRegion(region.start, region.end - region.start, value, param, ActionType::NOTE_EDIT);
	renderDisplay(note_expression::displayValueFromRow(y, bipolar));
	uiNeedsRendering(&automationView);
}
