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

#include "gui/ui/load/load_midi_cc_labels_ui.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/root_ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "io/debug/log.h"
#include "model/action/action_logger.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

using namespace deluge;

LoadMidiCCLabelsUI loadMidiCCLabelsUI{};

LoadMidiCCLabelsUI::LoadMidiCCLabelsUI() {
}

bool LoadMidiCCLabelsUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool LoadMidiCCLabelsUI::opened() {
	if (getRootUI() != &instrumentClipView || getCurrentOutputType() != OutputType::MIDI_OUT) {
		return false;
	}

	Error error = beginSlotSession(); // Requires currentDir to be set. (Not anymore?)
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	actionLogger.deleteAllLogs();

	error = setupForMidiLabels(); // Sets currentDir.
	if (error != Error::NONE) {
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads, in call to setupForMidiLabels().
		display->displayError(error);
		return false;
	}

	focusRegained();

	return true;
}

// If OLED, then you should make sure renderUIsForOLED() gets called after this.
Error LoadMidiCCLabelsUI::setupForMidiLabels() {
	// reset
	fileIconPt2 = nullptr;
	fileIconPt2Width = 0;

	if (display->haveOLED()) {
		title = "Load midi labels";
		fileIcon = deluge::hid::display::OLED::midiIcon;
		fileIconPt2 = deluge::hid::display::OLED::midiIconPt2;
		fileIconPt2Width = 1;
	}

	enteredText.clear();

	char const* defaultDir = "MIDI/Labels";

	String searchFilename;

	MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();

	// is empty we just start with nothing. currentSlot etc remain set to "zero" from before
	if (midiInstrument->midiLabelFileName.isEmpty()) {
		Error error = currentDir.set(defaultDir);
		if (error != Error::NONE) {
			return error;
		}
	}
	else {
		char const* fullPath = midiInstrument->midiLabelFileName.get();

		// locate last occurence of "/" in string
		char* filename = strrchr((char*)fullPath, '/');

		// lengths of full path
		auto fullPathLength = strlen(fullPath);

		// directory
		char* dir = new char[sizeof(char) * fullPathLength + 1];

		memset(dir, 0, sizeof(char) * fullPathLength + 1);
		strncpy(dir, fullPath, fullPathLength - strlen(filename));

		currentDir.set(dir);
		searchFilename.set(++filename);
	}

	if (!searchFilename.isEmpty()) {
		Error error = searchFilename.concatenate(".XML");
		if (error != Error::NONE) {
			return error;
		}
	}

	Error error = arrivedInNewFolder(0, searchFilename.get(), defaultDir);
	if (error != Error::NONE) {
		return error;
	}

	currentLabelLoadError = (fileIndexSelected >= 0) ? Error::NONE : Error::UNSPECIFIED;

	drawKeys();

	if (display->have7SEG()) {
		displayText(false);
	}

	return Error::NONE;
}

void LoadMidiCCLabelsUI::folderContentsReady(int32_t entryDirection) {
}

void LoadMidiCCLabelsUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	// If it's a directory...
	if (currentFileItem->isFolder) {

		Error error = goIntoFolder(currentFileItem->filename.get());

		if (error != Error::NONE) {
			display->displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}

	else {

	//	if (currentLabelLoadError != Error::NONE) {
			currentLabelLoadError = performLoad();
			if (currentLabelLoadError != Error::NONE) {
				display->displayError(currentLabelLoadError);
				return;
			}
	//	}

		close();
	}
}

ActionResult LoadMidiCCLabelsUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Load button
	if (b == LOAD) {
		return mainButtonAction(on);
	}

	else {
		return LoadUI::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult LoadMidiCCLabelsUI::padAction(int32_t x, int32_t y, int32_t on) {
	if (x < kDisplayWidth) {
		return LoadUI::padAction(x, y, on);
	}
	else {
		LoadUI::exitAction();
		return ActionResult::DEALT_WITH;
	}
}

Error LoadMidiCCLabelsUI::performLoad(bool doClone) {

	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem == nullptr) {
		// Make it say "NONE" on numeric Deluge, for
		// consistency with old times.
		return display->haveOLED() ? Error::FILE_NOT_FOUND : Error::NO_FURTHER_FILES_THIS_DIRECTION;
	}

	if (currentFileItem->isFolder) {
		return Error::NONE;
	}

	Error error = StorageManager::loadMidiCCLabelsFromFile((MIDIInstrument*)getCurrentOutput(),
	                                                       &currentFileItem->filePointer, &enteredText, &currentDir);

	if (error != Error::NONE) {

		display->displayPopup("fail 3");

		return error;
	}

	return Error::NONE;
}
