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

#include "gui/ui/rename/rename_clip_ui.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/output.h"
#include "model/song/song.h"

namespace {
RenameClipUI local_rename_clip_ui{"Clip Name"};
PLACE_SDRAM_BSS deluge::gui::ui_session::RemoteInstance<RenameClipUI> remote_rename_clip_ui;
} // namespace
RenameClipUI& rename_clip_ui_for_session() {
	return remote_rename_clip_ui.get(local_rename_clip_ui, "Clip Name");
}

bool RenameClipUI::canRename() const {
	if (!currentSong || !currentSong->contains_clip_for_undo(clip))
		return false;
	for (auto* output = currentSong->firstOutput; output; output = output->next) {
		if (output == clip->output)
			return true;
	}
	return false;
}

std::string_view RenameClipUI::getCurrentName() const {
	if (!canRename())
		return {};
	return clip->name.get();
}

bool RenameClipUI::trySetName(std::string_view name) {
	if (!canRename())
		return false;
	auto* const source_song = currentSong;
	auto* const source_clip = clip;
	auto* const source_output = clip->output;
	const auto source_owner = deluge::gui::ui_session::current();
	String previous_name;
	previous_name.set(&clip->name); // Shares existing storage without allocation.
	String replacement_name;
	const auto error = replacement_name.set(name);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}
	// Allocation can service callbacks. Validate before touching the retained clip again.
	if (deluge::gui::ui_session::current() != source_owner || currentSong != source_song || clip != source_clip
	    || !canRename() || clip->output != source_output || std::string_view(clip->name.get()) != previous_name.get()) {
		return false;
	}
	// Don't allow duplicate names on clips of a single output.
	Clip* other = clip->output->getClipFromName(replacement_name.get());
	if (other != nullptr && other != clip) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DUPLICATE_NAMES));
		return false;
	}
	clip->name.set(&replacement_name); // Non-allocating commit; failure leaves the old name intact.
	return true;
}
