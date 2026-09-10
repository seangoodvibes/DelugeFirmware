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

#include "gui/ui/load/load_pattern_ui.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/root_ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "io/debug/log.h"
#include "model/action/action_logger.h"
#include "model/song/song.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

using namespace deluge;

static constexpr const char* PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER = "PATTERNS/RHYTHMIC/KIT";
static constexpr const char* PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER = "PATTERNS/RHYTHMIC/DRUM";
static constexpr const char* PATTERN_MELODIC_DEFAULT_FOLDER = "PATTERNS/MELODIC";

namespace {
// This is an implicit preview rollback, not an explicit shared-history undo.
bool latest_action_matches_pattern_preview() {
	const Action* action = actionLogger.firstAction[BEFORE];
	return currentSong && action && action->type == ActionType::PATTERN_PASTE
	       && action->navigation_owner == deluge::gui::ui_session::current()
	       && action->currentClip == currentSong->getCurrentClip() && currentSong->getCurrentClip()
	       && action->captured_song == currentSong && action->captured_output == currentSong->getCurrentClip()->output;
}

LoadPatternUI local_load_pattern_ui{};
PLACE_SDRAM_BSS deluge::gui::ui_session::RemoteInstance<LoadPatternUI> remote_load_pattern_ui;
} // namespace
LoadPatternUI& load_pattern_ui_for_session() {
	return remote_load_pattern_ui.get(local_load_pattern_ui);
}

bool LoadPatternUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	// greyout the sidebar, not the main pads
	if (qwerty_visible_for_session()) {
		*cols = 0x03;
	}
	// greyout everything
	else {
		*cols = 0xFFFFFFFF;
	}
	return true;
}

bool LoadPatternUI::opened() {
	// Start Pattern Paste Action

	if (!getRootUI()->toClipMinder() || (getCurrentOutputType() == OutputType::AUDIO)) {
		return false;
	}

	Error error = createFoldersRecursiveIfNotExists(PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}
	error = createFoldersRecursiveIfNotExists(PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}
	error = createFoldersRecursiveIfNotExists(PATTERN_MELODIC_DEFAULT_FOLDER);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	actionLogger.getNewAction(ActionType::PATTERN_PASTE, ActionAddition::ALLOWED);
	overwriteExisting = true;

	if (getCurrentOutputType() == OutputType::KIT) {
		if (getRootUI()->getAffectEntire()) {
			defaultDir = PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER;
			favouritesManager.setCategory(PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER);
			title = "Load Kit Pattern";
			selectedDrumOnly = false;
		}
		else {
			defaultDir = PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER;
			favouritesManager.setCategory(PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER);
			title = "Load Drum Pattern";
			selectedDrumOnly = true;
		}
	}
	else {
		defaultDir = std::string(PATTERN_MELODIC_DEFAULT_FOLDER);
		favouritesManager.setCategory(PATTERN_MELODIC_DEFAULT_FOLDER);
		title = "Load Pattern";
		selectedDrumOnly = false;
	}

	favouritesChanged();
	current_dir_for_session().set(defaultDir.c_str());

	error = beginSlotSession(); // Requires currentDir to be set. (Not anymore?)
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	error = setupForLoadingPattern(); // Sets currentDir.
	if (error != Error::NONE) {
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads, in call to setupForLoadingMidiDeviceDefinition().
		display->displayError(error);
		return false;
	}

	focusRegained();

	return true;
}

void LoadPatternUI::setupLoadPatternUI(bool overwriteExistingState, bool noScalingState) {
	overwriteExisting = overwriteExistingState;
	noScaling = noScalingState;
	previewOnly = true;
	if (!overwriteExisting) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PATTERN_NOOVERWRITE));
	}
	if (noScaling) {
		Error error = instrument_clip_view_for_session().patternClear();
		if (error != Error::NONE) {
			display->displayError(error);
			return;
		}
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PATTERN_NOSCALING));
	}
	Error error = performLoad();
	if (error != Error::NONE) {
		display->displayError(error);
	}
}

void LoadPatternUI::selectEncoderAction(int8_t offset) {
	if (noScaling) {
		Error error = instrument_clip_view_for_session().patternClear();
		if (error != Error::NONE) {
			display->displayError(error);
			return;
		}
	}
	LoadUI::selectEncoderAction(offset);
}

void LoadPatternUI::currentFileChanged(int32_t movementDirection) {
	if (!overwriteExisting && latest_action_matches_pattern_preview()) {
		if (!actionLogger.revert(BEFORE)) {
			display->displayError(Error::UNSPECIFIED);
			return;
		}
		// Create a new Action where the Events can be added
		actionLogger.getNewAction(ActionType::PATTERN_PASTE, ActionAddition::ALLOWED);
	}
	if (noScaling) {
		Error error = instrument_clip_view_for_session().patternClear();
		if (error != Error::NONE) {
			display->displayError(error);
			return;
		}
	}
}

// If OLED, then you should make sure renderUIsForOLED() gets called after this.
Error LoadPatternUI::setupForLoadingPattern() {
	entered_text_for_session().clear();

	if (display->haveOLED()) {
		fileIcon = deluge::hid::display::OLED::midiIcon;
		fileIconPt2 = deluge::hid::display::OLED::midiIconPt2;
		fileIconPt2Width = 1;
	}

	String searchFilename;

	Error error = current_dir_for_session().set(defaultDir.c_str());
	if (error != Error::NONE) {
		return error;
	}

	if (!searchFilename.isEmpty()) {
		Error error = searchFilename.concatenate(".XML");
		if (error != Error::NONE) {
			return error;
		}
	}

	error = arrivedInNewFolder(0, searchFilename.get(), defaultDir.c_str());
	if (error != Error::NONE) {
		return error;
	}

	drawKeys();

	if (display->have7SEG()) {
		displayText(false);
	}

	return Error::NONE;
}

void LoadPatternUI::folderContentsReady(int32_t entryDirection) {
}

void LoadPatternUI::enterKeyPress() {
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
		previewOnly = false;
		Error error = performLoad();
		if (error != Error::NONE) {
			display->displayError(error);
			return;
		}
		close();
	}
}

ActionResult LoadPatternUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Load button
	if (b == LOAD) {
		previewOnly = false;
		return mainButtonAction(on);
	}
	else if (b == PLAY) {
		// Need to use special preview mode for this as on constant playing, big Midi files can lead to stucked notes
		FileItem* currentFileItem = getCurrentFileItem();
		if (!currentFileItem) {
			return ActionResult::DEALT_WITH;
		}
		if (!currentFileItem->isFolder) {
			if (on) {
				previewOnly = true;

				Error error = performLoad();
				if (error != Error::NONE) {
					display->displayError(error);
					return ActionResult::DEALT_WITH;
				}
				// rerenndering Keyboard
				renderingNeededRegardlessOfUI();
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_PATTERN_PREVIEW));
			}
		}
		instrument_clip_view_for_session().patternPreview();
		// rerenndering Keyboard

		return ActionResult::DEALT_WITH;
	}
	else {
		if (on && b == BACK) {
			// don't allow navigation backwards if we're in the default folder
			if (!strcmp(current_dir_for_session().get(), defaultDir.c_str())) {
				// Undo all Changes made during Pattern Preview
				if (latest_action_matches_pattern_preview()) {
					actionLogger.closeAction(ActionType::PATTERN_PASTE);
					if (!actionLogger.revert(BEFORE, false, false)) {
						display->displayError(Error::UNSPECIFIED);
						return ActionResult::DEALT_WITH;
					}
				}
				close();
				return ActionResult::DEALT_WITH;
			}
		}
		return LoadUI::buttonAction(b, on, inCardRoutine);
	}
}

ActionResult LoadPatternUI::padAction(int32_t x, int32_t y, int32_t on) {
	if (x < kDisplayWidth) {
		return LoadUI::padAction(x, y, on);
	}
	else {
		LoadUI::exitAction();
		return ActionResult::DEALT_WITH;
	}
}

Error LoadPatternUI::performLoad() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem == nullptr) {
		// Make it say "NONE" on numeric Deluge, for
		// consistency with old times.
		return display->haveOLED() ? Error::FILE_NOT_FOUND : Error::NO_FURTHER_FILES_THIS_DIRECTION;
	}

	if (currentFileItem->isFolder) {
		return Error::NONE;
	}

	if (!previewOnly && !noScaling) {
		if (latest_action_matches_pattern_preview()) {
			actionLogger.closeAction(ActionType::PATTERN_PASTE);
			if (!actionLogger.revert(BEFORE, false, false)) {
				return Error::UNSPECIFIED;
			}
		}
		actionLogger.getNewAction(ActionType::PATTERN_PASTE, ActionAddition::ALLOWED);
	}

	String fileName;
	fileName.set(current_dir_for_session().get());
	fileName.concatenate("/");
	fileName.concatenate(entered_text_for_session().get());
	fileName.concatenate(".XML");

	Error error = StorageManager::loadPatternFile(&currentFileItem->filePointer, &fileName, overwriteExisting,
	                                              noScaling, previewOnly, selectedDrumOnly);

	if (error != Error::NONE) {
		return error;
	}

	return Error::NONE;
}
