/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

// this class is used by all the note parameter menu's to identify the note selected
// so that the note's parameter's can be adjusted
// so the other classes need to inherit this one

namespace deluge::gui::menu_item::note {
class SelectedNote : public Integer {
public:
	using Integer::Integer;

	uint8_t xDisplay = kNoSelection; // x, y coordinate of note pad to blink
	uint8_t yDisplay = kNoSelection;

	// plan:
	// if you vertical scroll, it de-selects note so that you need to re-select
	// "screen will say "please select note"
	// if you horizontal scroll, same thing
	// so need to add verticalEncoderAction and horizontalEncoderAction functions here

	/// @brief Handle horizontal encoder movement.
	///
	/// @param offset must be either -1 or 1, jumping is not supported by many children.
	void horizontalEncoderAction(int32_t offset) override { return; }
	/// @brief Handle vertical encoder movement.
	///
	/// @param offset must be either -1 or 1, jumping is not supported by many children.
	void verticalEncoderAction(int32_t offset) override { return; }

	ModelStackWithNoteRow* getIndividualNoteRow(ModelStackWithTimelineCounter* modelStack) {
		auto* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    clip->getNoteRowOnScreen(instrumentClipView.lastSelectedNoteYDisplay,
		                             modelStack); // don't create
		return modelStackWithNoteRow;
	}

	void updateDisplay() {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}
};
} // namespace deluge::gui::menu_item::note
