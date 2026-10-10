/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "menu_item_with_cc_learning.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "model/song/song.h"

void MenuItemWithCCLearning::unlearnAction() {

	ParamDescriptor paramDescriptor = getLearningThing();

	// If it was a reasonable-ish request...
	if (!paramDescriptor.isNull()) {
		if (!sound_editor_for_session().currentModControllable) {
			return;
		}
		bool success = sound_editor_for_session().currentModControllable->unlearnKnobs(paramDescriptor, currentSong);

		if (success) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_UNLEARNED));
			view_for_session().setKnobIndicatorLevels();
			sound_editor_for_session().markInstrumentAsEdited();
		}
	}
}

void MenuItemWithCCLearning::learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) {
	ParamDescriptor paramDescriptor = getLearningThing();

	if (!sound_editor_for_session().currentModControllable) {
		return;
	}

	bool success = sound_editor_for_session().currentModControllable->learnKnob(cable, paramDescriptor, whichKnob,
	                                                                            modKnobMode, midiChannel, currentSong);

	if (success) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_LEARNED));
		view_for_session().setKnobIndicatorLevels();
		sound_editor_for_session().markInstrumentAsEdited();
	}
}
