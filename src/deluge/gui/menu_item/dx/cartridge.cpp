/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "cartridge.h"
#include "dsp/dx/engine.h"
#include "fatfs.hpp"
#include "gui/ui/browser/dx_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "memory/allocate_unique.h"
#include "memory/sdram_allocator.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/DX7Cartridge.h"
#include "util/functions.h"
#include "util/try.h"
#include <etl/vector.h>
#include <memory>

static bool openFile(std::string_view path, DX7Cartridge* data) {
	using namespace deluge;
	using enum deluge::l10n::String;
	constexpr size_t minSize = kSmallSysexSize;

	FatFS::FileInfo fno = D_TRY_CATCH(FatFS::stat(path), error, {
		return false; // fail quickly if file doesn't exist
	});

	FSIZE_t filesize = fno.fsize;
	if (filesize < minSize) {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
	}

	// Open the file
	FatFS::File file = D_TRY_CATCH(FatFS::File::open(path, FA_READ), error, {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_READ_ERROR));
		return false;
	});

	l10n::String error = EMPTY_STRING;
	int readsize = std::min((int)filesize, 8192);

	std::unique_ptr buffer = D_TRY_CATCH_MOVE((allocate_unique<std::byte, memory::sdram_allocator>(readsize)), error, {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_READ_ERROR));
		return false;
	});

	std::span<std::byte> readbuffer = D_TRY_CATCH(file.read({buffer.get(), filesize}), error, {
		return false; //
	});

	if (readbuffer.size() < minSize) {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
	}

	error = data->load(readbuffer);
	if (error != EMPTY_STRING) {
		// Allow loading to continue for checksum errors, but fail for other errors
		if (error != deluge::l10n::String::STRING_FOR_DX_ERROR_CHECKSUM_FAIL) {
			display->displayPopup(l10n::get(error), 3);
			return false;
		}
	}

	return true;
}
namespace deluge::gui::menu_item {

DxCartridge dxCartridge{l10n::String::STRING_FOR_DX_CARTRIDGE};

DxCartridge::session_state& DxCartridge::state_for_session() {
	return session_states.active();
}

void DxCartridge::beginSession(MenuItem* navigatedBackwardFrom) {
	loadPatch();
	readValueAgain();
}

// Unpacks the currently-selected program into the live DX patch and hard-cuts any sounding voices.
// Only call this when the selected patch actually changes - NOT from a plain redraw, otherwise an
// audition note gets cut every time the menu refreshes (e.g. mod-encoder press, display change).
void DxCartridge::loadPatch() {
	if (state_for_session().cartridge == nullptr) {
		return;
	}

	DxPatch* patch = sound_editor_for_session().currentSource->ensureDxPatch();
	state_for_session().cartridge->unpackProgram(patch->params, state_for_session().current_value);
	sound_editor_for_session().currentSound->killAllVoices();
	Instrument* instrument = getCurrentInstrument();
	if (instrument->type == OutputType::SYNTH && !instrument->mightExistOnCard) {
		char name[11];
		state_for_session().cartridge->getProgramName(state_for_session().current_value, name);
		if (name[0] != 0) {
			instrument->name.set(name);
		}
	}
}

void DxCartridge::readValueAgain() {
	if (state_for_session().cartridge == nullptr) {
		return;
	}
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

void DxCartridge::drawPixelsForOled() {
	if (state_for_session().cartridge == nullptr) {
		return;
	}
	char names[32][11];
	state_for_session().cartridge->getProgramNames(names);

	etl::vector<std::string_view, 32> itemNames = {};
	for (int i = 0; i < state_for_session().cartridge->numPatches(); i++) {
		itemNames.push_back(names[i]);
	}
	drawItemsForOled(itemNames, state_for_session().current_value - state_for_session().scroll_position,
	                 state_for_session().scroll_position);
}

void DxCartridge::drawValue() {
	char names[32][11];
	state_for_session().cartridge->getProgramNames(names);

	display->setScrollingText(names[state_for_session().current_value]);
}

bool DxCartridge::tryLoad(std::string_view path) {
	if (state_for_session().cartridge == nullptr) {
		state_for_session().cartridge = new DX7Cartridge();
	}
	state_for_session().current_value = 0;
	state_for_session().scroll_position = 0;

	return openFile(path, state_for_session().cartridge);
}

void DxCartridge::selectEncoderAction(int32_t offset) {
	if (!state_for_session().cartridge) {
		return;
	}
	int32_t numValues = state_for_session().cartridge->numPatches();
	if (numValues <= 0) {
		return;
	}

	int32_t newValue = std::clamp<int32_t>(state_for_session().current_value + offset, 0, numValues - 1);

	// if no change, just exit
	if (newValue == state_for_session().current_value) {
		return;
	}

	state_for_session().current_value = newValue;

	if (display->haveOLED()) {
		state_for_session().scroll_position =
		    std::clamp<int>(newValue - 1, 0, std::max<int>(0, numValues - kOLEDMenuNumOptionsVisible));
	}

	loadPatch();
	readValueAgain(); // redraw
}

MenuItem* DxCartridge::selectButtonPress() {
	sound_editor_for_session().exitCompletely();
	return NO_NAVIGATION;
}

} // namespace deluge::gui::menu_item
