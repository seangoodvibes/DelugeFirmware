/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/ui/rename/rename_drum_ui.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "util/exceptions.h"

namespace {
RenameDrumUI local_rename_drum_ui{"Drum Name"};
PLACE_SDRAM_BSS deluge::gui::ui_session::RemoteInstance<RenameDrumUI> remote_rename_drum_ui;
} // namespace
RenameDrumUI& rename_drum_ui_for_session() {
	return remote_rename_drum_ui.get(local_rename_drum_ui, "Drum Name");
}

Drum* RenameDrumUI::selected_drum_for_rename() const {
	if (!currentSong || !currentSong->contains_clip_for_undo(currentSong->getCurrentClip()))
		return nullptr;
	auto* const selected_output = getCurrentOutput();
	for (auto* output = currentSong->firstOutput; output; output = output->next) {
		if (output != selected_output)
			continue;
		if (output->type != OutputType::KIT)
			return nullptr;
		auto* kit = static_cast<Kit*>(output);
		auto* selected_drum = kit->selected_drum_for_session();
		for (auto* drum = kit->firstDrum; drum; drum = drum->next) {
			if (drum == selected_drum)
				return drum;
		}
		return nullptr;
	}
	return nullptr;
}

bool RenameDrumUI::canRename() const {
	return selected_drum_for_rename() != nullptr;
}

std::string_view RenameDrumUI::getCurrentName() const {
	auto* drum = selected_drum_for_rename();
	return drum ? std::string_view(drum->drumName) : std::string_view{};
}

bool RenameDrumUI::trySetName(std::string_view name) {
	auto* drum = selected_drum_for_rename();
	if (!drum)
		return false;
	auto* kit = static_cast<Kit*>(getCurrentOutput());
	Drum* other = kit->getDrumFromName(name);
	if (other != nullptr && other != drum) {
		// We only allow renaming if there are no other drums with the same name.
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DUPLICATE_NAMES));
		return false;
	}
	auto* const source_song = currentSong;
	const auto source_owner = deluge::gui::ui_session::current();
	try {
		drum->drumName = name;
	} catch (deluge::exception error) {
		if (error != deluge::exception::BAD_ALLOC)
			throw;
		// std::string preserves its old value on allocation failure. Report only
		// while this panel still targets the same live drum after allocation.
		if (deluge::gui::ui_session::current() == source_owner && currentSong == source_song
		    && selected_drum_for_rename() == drum) {
			display->displayError(Error::INSUFFICIENT_RAM);
		}
		return false;
	}
	return true;
}
