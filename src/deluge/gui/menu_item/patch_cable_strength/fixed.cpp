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
#include "fixed.h"
#include "gui/menu_item/patch_cable_strength/range.h"
#include "gui/menu_item/source_selection/range.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/ui/sound_editor.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"
#include "storage/multi_range/multi_range.h"

namespace deluge::gui::menu_item::patch_cable_strength {

MenuPermission Fixed::checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
                                                    MultiRange** currentRange) {
	sound_editor_for_session().patchingParamSelected = p;
	source_selection::regularMenu.source_for_session() = s;
	return PatchCableStrength::checkPermissionToBeginSession(modControllable, whichThing, currentRange);
}

uint8_t Fixed::shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) {
	PatchCableSet& patchCableSet = *sound_editor_for_session().currentParamManager->getPatchCableSet();

	// If it's the source controlling the range of the source we're editing for...
	if (patchCableSet.getPatchCableIndex(s, getLearningThing()) != 255) {
		return 3;
	}
	return 255;
}

MenuItem* Fixed::patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive) {
	source_selection::rangeMenu.source_for_session() = s;
	return &patch_cable_strength::rangeMenu;
}

} // namespace deluge::gui::menu_item::patch_cable_strength
