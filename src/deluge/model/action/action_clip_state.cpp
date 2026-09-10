/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/action/action_clip_state.h"
#include "definitions_cxx.hpp"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"

void ActionClipState::grabFromClip(Clip* thisClip) {
	clip_identity = thisClip;
	output_identity = thisClip->output;
	selected_drum_identity = nullptr;
	// modKnobMode = thisClip->modKnobMode;

	if (thisClip->type == ClipType::INSTRUMENT) {
		InstrumentClip* instrumentClip = (InstrumentClip*)thisClip;
		yScrollSessionView[BEFORE] = instrumentClip->y_scroll_for_session();
		affectEntire = instrumentClip->affect_entire_for_session();
		wrapEditing = instrumentClip->wrap_editing_for_session();
		wrapEditLevel = instrumentClip->wrap_edit_level_for_session();

		if (thisClip->output->type == OutputType::KIT) {
			Kit* kit = static_cast<Kit*>(thisClip->output);
			Drum* selected = kit->selected_drum_for_session();
			if (selected && kit->getDrumIndex(selected) >= 0) {
				selected_drum_identity = selected;
			}
			else {
				kit->selected_drum_for_session() = nullptr;
			}
		}
	}
}
