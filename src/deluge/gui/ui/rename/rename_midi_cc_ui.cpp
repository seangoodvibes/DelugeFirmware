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

#include "gui/ui/rename/rename_midi_cc_ui.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"
#include "util/exceptions.h"
#include <string_view>

namespace {
RenameMidiCCUI local_rename_midi_cc_ui{"CC Name"};
PLACE_SDRAM_BSS deluge::gui::ui_session::RemoteInstance<RenameMidiCCUI> remote_rename_midi_cc_ui;
} // namespace
RenameMidiCCUI& rename_midi_cc_ui_for_session() {
	return remote_rename_midi_cc_ui.get(local_rename_midi_cc_ui, "CC Name");
}

MIDIInstrument* RenameMidiCCUI::instrument_for_rename() const {
	if (!currentSong || !currentSong->contains_clip_for_undo(currentSong->getCurrentClip()))
		return nullptr;
	auto* clip = currentSong->getCurrentClip();
	const auto cc = clip->last_selected_param_id_for_session();
	if (cc < 0 || cc == CC_EXTERNAL_MOD_WHEEL || cc >= kNumRealCCNumbers)
		return nullptr;
	for (auto* output = currentSong->firstOutput; output; output = output->next) {
		if (output == clip->output)
			return output->type == OutputType::MIDI_OUT ? static_cast<MIDIInstrument*>(output) : nullptr;
	}
	return nullptr;
}

bool RenameMidiCCUI::canRename() const {
	return instrument_for_rename() != nullptr;
}

std::string_view RenameMidiCCUI::getCurrentName() const {
	auto* instrument = instrument_for_rename();
	if (!instrument)
		return {};
	return instrument->getNameFromCC(currentSong->getCurrentClip()->last_selected_param_id_for_session());
}

bool RenameMidiCCUI::trySetName(std::string_view name) {
	auto* instrument = instrument_for_rename();
	if (!instrument)
		return false;
	const auto cc = currentSong->getCurrentClip()->last_selected_param_id_for_session();
	auto* const source_song = currentSong;
	auto* const source_clip = currentSong->getCurrentClip();
	const auto source_owner = deluge::gui::ui_session::current();
	try {
		instrument->setNameForCC(cc, name);
	} catch (deluge::exception error) {
		if (error != deluge::exception::BAD_ALLOC)
			throw;
		if (deluge::gui::ui_session::current() == source_owner && currentSong == source_song
		    && instrument_for_rename() == instrument && currentSong->getCurrentClip() == source_clip
		    && source_clip->last_selected_param_id_for_session() == cc) {
			display->displayError(Error::INSUFFICIENT_RAM);
		}
		return false;
	}
	instrument->editedByUser = true; // Persist the name with the song / preset.
	return true;
}
