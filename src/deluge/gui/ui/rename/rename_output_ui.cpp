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

#include "gui/ui/rename/rename_output_ui.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/views/arranger_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/output.h"
#include "model/song/song.h"

namespace {
RenameOutputUI local_rename_output_ui{"Track name"};
PLACE_SDRAM_BSS deluge::gui::ui_session::RemoteInstance<RenameOutputUI> remote_rename_output_ui;
} // namespace
RenameOutputUI& rename_output_ui_for_session() {
	return remote_rename_output_ui.get(local_rename_output_ui, "Track name");
}

bool RenameOutputUI::canRename() const {
	if (!currentSong || !output)
		return false;
	for (auto* candidate = currentSong->firstOutput; candidate; candidate = candidate->next) {
		if (candidate == output)
			return true;
	}
	return false;
}

std::string_view RenameOutputUI::getCurrentName() const {
	if (!canRename())
		return {};
	return output->name.get();
}

bool RenameOutputUI::trySetName(std::string_view name) {
	if (!canRename())
		return false;
	auto* const source_song = currentSong;
	auto* const source_output = output;
	const auto source_owner = deluge::gui::ui_session::current();
	String previous_name;
	previous_name.set(&output->name); // Shares existing storage without allocation.
	String replacement_name;
	const auto error = replacement_name.set(name);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}
	// Allocation can service callbacks. Revalidate before accessing the retained output.
	if (deluge::gui::ui_session::current() != source_owner || currentSong != source_song || output != source_output
	    || !canRename() || std::string_view(output->name.get()) != previous_name.get()) {
		return false;
	}
	// Duplicate names not allowed for audio outputs.
	void* other = currentSong->getAudioOutputFromName(replacement_name.get());
	if (other != nullptr && other != output) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DUPLICATE_NAMES));
		return false;
	}
	output->name.set(&replacement_name); // Non-allocating commit.
	return true;
}
