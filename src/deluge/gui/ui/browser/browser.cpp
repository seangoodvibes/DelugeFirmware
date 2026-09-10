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

#include "gui/ui/browser/browser.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "fatfs.hpp"
#include "gui/context_menu/delete_file.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/default_name.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/encoders.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/try.h"
#include <cstring>
#include <new>

using namespace deluge;
struct Browser::SessionState {
	String currentDir{};
	CStringArray fileItems{sizeof(FileItem)};
	int32_t numFileItemsDeletedAtStart{};
	int32_t numFileItemsDeletedAtEnd{};
	String firstFileItemRemaining{};
	String lastFileItemRemaining{};
	OutputType outputTypeToLoad{};
	char const* filenameToStartSearchAt{};
	int32_t fileIndexSelected{};
	int32_t scrollPosVertical{};
	int32_t numCharsInPrefix{};
	bool qwertyVisible = true;
	bool arrivedAtFileByTyping{};
	bool allowFoldersSharingNameWithFile{};
	char const** allowedFileExtensions{};
	int8_t numberEditPos{};
	NumericLayerScrollingText* scrollingText{};
};
Browser::SessionState& Browser::session_state() {
	static deluge::gui::ui_session::State<SessionState> states;
	return states.active();
}
String& Browser::current_dir_for_session() {
	return session_state().currentDir;
}
CStringArray& Browser::file_items_for_session() {
	return session_state().fileItems;
}
int32_t& Browser::num_file_items_deleted_at_start_for_session() {
	return session_state().numFileItemsDeletedAtStart;
}
int32_t& Browser::num_file_items_deleted_at_end_for_session() {
	return session_state().numFileItemsDeletedAtEnd;
}
String& Browser::first_file_item_remaining_for_session() {
	return session_state().firstFileItemRemaining;
}
String& Browser::last_file_item_remaining_for_session() {
	return session_state().lastFileItemRemaining;
}
OutputType& Browser::output_type_to_load_for_session() {
	return session_state().outputTypeToLoad;
}
char const*& Browser::filename_to_start_search_at_for_session() {
	return session_state().filenameToStartSearchAt;
}
int32_t& Browser::file_index_selected_for_session() {
	return session_state().fileIndexSelected;
}
int32_t& Browser::scroll_pos_vertical_for_session() {
	return session_state().scrollPosVertical;
}
int32_t& Browser::num_chars_in_prefix_for_session() {
	return session_state().numCharsInPrefix;
}
bool& Browser::qwerty_visible_for_session() {
	return session_state().qwertyVisible;
}
bool& Browser::arrived_at_file_by_typing_for_session() {
	return session_state().arrivedAtFileByTyping;
}
bool& Browser::allow_folders_sharing_name_with_file_for_session() {
	return session_state().allowFoldersSharingNameWithFile;
}
char const**& Browser::allowed_file_extensions_for_session() {
	return session_state().allowedFileExtensions;
}
int8_t& Browser::number_edit_pos_for_session() {
	return session_state().numberEditPos;
}
NumericLayerScrollingText*& Browser::scrolling_text_for_session() {
	return session_state().scrollingText;
}

// 7SEG ONLY

char const* allowedFileExtensionsXML[] = {"XML", "Json", NULL};

Browser::Browser() {
	fileIcon = deluge::hid::display::OLED::songIcon;
	fileIconPt2 = nullptr;
	fileIconPt2Width = 0;
	shouldWrapFolderContents = true;

	mayDefaultToBrandNewNameOnEntry = false;
	qwertyAlwaysVisible = true;
	filePrefix = NULL;
	shouldInterpretNoteNamesForThisBrowser = false;
}

bool Browser::opened() {
	num_chars_in_prefix_for_session() = 0; // For most browsers, this just stays at 0.
	arrived_at_file_by_typing_for_session() = false;
	allowed_file_extensions_for_session() = allowedFileExtensionsXML;
	allow_folders_sharing_name_with_file_for_session() = false;

	number_edit_pos_for_session() = -1;

	return QwertyUI::opened();
}

// returns true if the FP for the filepath is correct
bool Browser::checkFP() {
	FileItem* currentFileItem = getCurrentFileItem();
	String filePath;
	Error error = getCurrentFilePath(&filePath);
	if (error != Error::NONE) {
		D_PRINTLN("couldn't get filepath");
		return false;
	}

	FilePointer tempfp;
	bool fileExists = StorageManager::fileExists(filePath.get(), &tempfp);
	if (!fileExists) {
		D_PRINTLN("couldn't get filepath");
		return false;
	}
	else if (tempfp.sclust != currentFileItem->filePointer.sclust) {
		D_PRINTLN("FPs don't match: correct is %lu but the browser has %lu", tempfp.sclust,
		          currentFileItem->filePointer.sclust);
#if ALPHA_OR_BETA_VERSION
		display->freezeWithError("B001");
#endif
		return false;
	}
	return true;
}

void Browser::close() {
	emptyFileItems();
	favouritesManager.close();
	QwertyUI::close();
}

void Browser::emptyFileItems() {

	AudioEngine::logAction("emptyFileItems");

	for (int32_t i = 0; i < file_items_for_session().getNumElements();) {
		FileItem* item = (FileItem*)file_items_for_session().getElementAddress(i);
		item->~FileItem();

		i++;
		if (!(i & 63)) { //  &127 was even fine, even with only -Og compiler optimization.
			AudioEngine::logAction("emptyFileItems in loop");
			AudioEngine::routineWithClusterLoading();
		}
	}

	AudioEngine::logAction("emptyFileItems 2");

	file_items_for_session().empty();

	AudioEngine::logAction("emptyFileItems 3");
}

void Browser::deleteSomeFileItems(int32_t startAt, int32_t stopAt) {

	// Call destructors.
	for (int32_t i = startAt; i < stopAt;) {
		FileItem* item = (FileItem*)file_items_for_session().getElementAddress(i);
		item->~FileItem();

		i++;
		if (!(i & 63)) { //  &127 was even fine, even with only -Og compiler optimization.
			AudioEngine::routineWithClusterLoading();
		}
	}

	file_items_for_session().deleteAtIndex(startAt, stopAt - startAt);
}

int32_t maxNumFileItemsNow;

int32_t catalogSearchDirection;

FileItem* Browser::getNewFileItem() {
	bool alreadyCulled = false;

	if (file_items_for_session().getNumElements() >= maxNumFileItemsNow) {
doCull:
		cullSomeFileItems();
		alreadyCulled = true;
	}

	int32_t newIndex = file_items_for_session().getNumElements();
	Error error = file_items_for_session().insertAtIndex(newIndex);
	if (error != Error::NONE) {
		if (alreadyCulled) {
			return nullptr;
		}
		else {
			goto doCull;
		}
	}

	void* newMemory = file_items_for_session().getElementAddress(newIndex);

	FileItem* thisItem = new (newMemory) FileItem();
	return thisItem;
}

void Browser::cullSomeFileItems() {
	sortFileItems();

	int32_t startAt, stopAt;

	int32_t numFileItemsDeletingNow =
	    file_items_for_session().getNumElements() - (maxNumFileItemsNow >> 1); // May get modified below.
	if (numFileItemsDeletingNow <= 0) {
		return;
	}

	// If we already know what side we want to be deleting on...
	if (catalogSearchDirection == CATALOG_SEARCH_LEFT) {
deleteFromLeftSide:
		num_file_items_deleted_at_start_for_session() += numFileItemsDeletingNow;
		startAt = 0;
		stopAt = numFileItemsDeletingNow;
		first_file_item_remaining_for_session().set(
		    ((FileItem*)file_items_for_session().getElementAddress(numFileItemsDeletingNow))->displayName);
	}
	else if (catalogSearchDirection == CATALOG_SEARCH_RIGHT) {
deleteFromRightSide:
		num_file_items_deleted_at_end_for_session() += numFileItemsDeletingNow;
		stopAt = file_items_for_session().getNumElements();
		startAt = stopAt - numFileItemsDeletingNow;
		last_file_item_remaining_for_session().set(
		    ((FileItem*)file_items_for_session().getElementAddress(startAt - 1))->displayName);
	}

	// Or if we've been using a search term *and* searching both directions, try to tend towards keeping equal amounts
	// of FileIems either side.
	else {

		shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
		octaveStartsFromA = false;
		int32_t foundIndex = file_items_for_session().search(filename_to_start_search_at_for_session());

		// If search-item is in second half, delete from start.
		if ((foundIndex << 1) >= file_items_for_session().getNumElements()) {
			int32_t newNumFilesDeleting =
			    foundIndex >> 1; // Delete half the existing items to the left of the search-item.
			if (newNumFilesDeleting <= 0) {
				return;
			}
			if (numFileItemsDeletingNow > newNumFilesDeleting) {
				numFileItemsDeletingNow = newNumFilesDeleting;
			}
			goto deleteFromLeftSide;
		}

		// Or, vice versa.
		else {
			int32_t newNumFilesDeleting = (file_items_for_session().getNumElements() - foundIndex)
			                              >> 1; // Delete half the existing items to the right of the search-item.
			if (newNumFilesDeleting <= 0) {
				return;
			}
			if (numFileItemsDeletingNow > newNumFilesDeleting) {
				numFileItemsDeletingNow = newNumFilesDeleting;
			}
			goto deleteFromRightSide;
		}
	}

	if (startAt != stopAt) {
		deleteSomeFileItems(startAt, stopAt); // Check might not be necessary?
	}
}

Error Browser::readFileItemsForFolder(char const* filePrefixHere, bool allowFolders,
                                      char const** allowedFileExtensionsHere, char const* filenameToStartAt,
                                      int32_t newMaxNumFileItems, int32_t newCatalogSearchDirection) {

	AudioEngine::logAction("readFileItemsForFolder");

	emptyFileItems();

	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
		return error;
	}

	staticDIR = D_TRY_CATCH(FatFS::Directory::open(current_dir_for_session().get()), error,
	                        { return fatfsErrorToDelugeError(error); });

	num_file_items_deleted_at_start_for_session() = 0;
	num_file_items_deleted_at_end_for_session() = 0;
	first_file_item_remaining_for_session().clear();
	last_file_item_remaining_for_session().clear();
	catalogSearchDirection = newCatalogSearchDirection;
	maxNumFileItemsNow = newMaxNumFileItems;
	filename_to_start_search_at_for_session() = filenameToStartAt;

	while (true) {
		AudioEngine::logAction("while loop");

		audioFileManager.loadAnyEnqueuedClusters();
		FilePointer thisFilePointer;

		std::tie(staticFNO, thisFilePointer) = D_TRY_CATCH(staticDIR.read_and_get_filepointer(), error, {
			break; // Break on error
		});

		if (staticFNO.fname[0] == 0) {
			break; /* Break on end of dir */
		}
		if (staticFNO.fname[0] == '.') {
			continue; /* Ignore dot entry */
		}
		bool isFolder = staticFNO.fattrib & AM_DIR;
		if (isFolder) {
			if (!allowFolders) {
				continue;
			}
		}
		else {
			char const* dotPos = strrchr(staticFNO.fname, '.');
			if (!dotPos) {
extensionNotSupported:
				continue;
			}
			char const* fileExtension = dotPos + 1;
			char const** thisExtension = allowedFileExtensionsHere;
			while (strcasecmp(fileExtension, *thisExtension)) {
				thisExtension++;
				if (!*thisExtension) {
					goto extensionNotSupported; // If reached end of list
				}
			}
		}

		FileItem* thisItem = getNewFileItem();
		if (!thisItem) {
			error = Error::INSUFFICIENT_RAM;
			break;
		}
		error = thisItem->filename.set(staticFNO.fname);
		if (error != Error::NONE) {
			break;
		}
		thisItem->isFolder = isFolder;
		thisItem->filePointer = thisFilePointer;

		// displayName is the CStringArray sort key, and must equal the real on-card name. The 7SEG short form ("185")
		// is produced at render time, not stored here - storing it made enteredText display-dependent, which is what
		// broke default naming on 7SEG (#1069).
		thisItem->displayName = thisItem->filename.get();
	}
	staticDIR.close();

	if (error != Error::NONE) {
		emptyFileItems();
	}

	return error;
}

void Browser::deleteFolderAndDuplicateItems(Availability instrumentAvailabilityRequirement) {
	int32_t writeI = 0;
	FileItem* nextItem = (FileItem*)file_items_for_session().getElementAddress(0);

	for (int32_t readI = 0; readI < file_items_for_session().getNumElements(); readI++) {
		FileItem* readItem = nextItem;

		// If there's a next item after "this" item, to compare it to...
		if (readI < file_items_for_session().getNumElements() - 1) {
			nextItem = (FileItem*)file_items_for_session().getElementAddress(readI + 1);

			// If we're a folder, and the next item is a file of the same name, delete this item.
			if (readItem->isFolder) {
				if (!nextItem->isFolder) {
					int32_t nameLength = readItem->filename.getLength();
					char const* nextItemFilename = nextItem->filename.get();
					if (!memcasecmp(readItem->filename.get(), nextItemFilename, nameLength)) {
						if (nextItemFilename[nameLength] == '.' && !strchr(&nextItemFilename[nameLength + 1], '.')) {
							goto deleteThisItem;
						}
					}
				}
			}

			// Or if we have an Instrument, and the next item is a file of the same name, delete the next item.
			else if (readItem->instrument) {
				if (!nextItem->instrument && !nextItem->isFolder) {
					if (!strcasecmp(readItem->displayName, nextItem->displayName)) {
						// if (readItem->filename.equalsCaseIrrespective(&nextItem->filename)) {
						if (readItem->maybeExistsOnCard && readItem->filePointer.sclust == 0) {
							readItem->filePointer = nextItem->filePointer;
						}
						nextItem->~FileItem();
						readI++;
						nextItem = (FileItem*)file_items_for_session().getElementAddress(readI + 1);
						// That may set it to an invalid address, but in that case, it won't get read.
					}
				}

checkAvailabilityRequirement:
				// Check Instrument's availabilityRequirement
				if (readItem->instrumentAlreadyInSong) {
					if (instrumentAvailabilityRequirement == Availability::INSTRUMENT_UNUSED) {
deleteThisItem:
						readItem->~FileItem();
						continue;
					}
					else if (instrumentAvailabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
						if (currentSong->doesOutputHaveActiveClipInSession(readItem->instrument)) {
							goto deleteThisItem;
						}
					}
				}
			}

			// Or if next item has an Instrument, and we're just a file...
			else if (nextItem->instrument) {
				if (!strcasecmp(readItem->displayName, nextItem->displayName)) { // And if same name...
					if (nextItem->maybeExistsOnCard && nextItem->filePointer.sclust == 0) {
						nextItem->filePointer = readItem->filePointer;
					}
					goto deleteThisItem;
				}
			}
		}
		else {
			if (readItem->instrument) {
				goto checkAvailabilityRequirement;
			}
		}

		void* writeAddress = file_items_for_session().getElementAddress(writeI);
		if (writeAddress != readItem) {
			memcpy(writeAddress, readItem, sizeof(FileItem));
		}
		writeI++;
	}

	int32_t numToDelete = file_items_for_session().getNumElements() - writeI;
	if (numToDelete > 0) {
		file_items_for_session().deleteAtIndex(writeI, numToDelete);
	}

	// Our system of keeping FileItems from getting too full by deleting elements from its ends as we go could have
	// caused bad results at the edges of the above, so delete a further one element at each end as needed.
	if (!first_file_item_remaining_for_session().isEmpty()) {
		file_items_for_session().deleteAtIndex(0);
	}
	if (!last_file_item_remaining_for_session().isEmpty()) {
		file_items_for_session().deleteAtIndex(file_items_for_session().getNumElements() - 1);
	}
}

Error Browser::setFileByFullPath(OutputType outputType, char const* fullPath) {
	arrived_at_file_by_typing_for_session() = true;
	FilePointer tempfp;
	bool fileExists = StorageManager::fileExists(fullPath, &tempfp);
	if (!fileExists) {
		return Error::FILE_NOT_FOUND;
	}

	const char* fileName = getFileNameFromEndOfPath(fullPath);
	// Copy the directory portion (everything before the final '/') straight into currentDir. String::set copies, so we
	// bound it by length rather than building a temporary - the old getPathFromFullPath() returned a pointer into a
	// std::string temporary that was already destroyed by the time we read it.
	char const* slashPos = strrchr(fullPath, '/');
	if (slashPos) {
		current_dir_for_session().set(fullPath, (int32_t)(slashPos - fullPath));
	}
	else {
		current_dir_for_session().clear();
	}

	// Change to the File Folder
	Error error = arrivedInNewFolder(0, fileName);
	if (error != Error::NONE) {
		return error;
	}

	//  Get the File Index
	file_index_selected_for_session() = file_items_for_session().search(fileName);
	if (file_index_selected_for_session() > file_items_for_session().getNumElements()) {
		return Error::FILE_NOT_FOUND;
	}

	// Update the Display
	scroll_pos_vertical_for_session() = file_index_selected_for_session();
	setEnteredTextFromCurrentFilename();
	renderUIsForOled();

	// Inform the Load UI that the File has changed
	currentFileChanged(1);
	return Error::NONE;
}

// song may be supplied as NULL, in which case it won't be searched for Instruments; sometimes this will get called when
// the currentSong is not set up.
Error Browser::readFileItemsFromFolderAndMemory(Song* song, OutputType outputType, char const* filePrefixHere,
                                                char const* filenameToStartAt, char const* defaultDirToAlsoTry,
                                                bool allowFolders, Availability availabilityRequirement,
                                                int32_t newCatalogSearchDirection) {
	// filenameToStartAt should have .XML at the end of it.
	bool triedCreatingFolder = false;

tryReadingItems:
	Error error = readFileItemsForFolder(filePrefixHere, allowFolders, allowed_file_extensions_for_session(),
	                                     filenameToStartAt, FILE_ITEMS_MAX_NUM_ELEMENTS, newCatalogSearchDirection);
	if (error != Error::NONE) {

		// If folder didn't exist, try our alternative one if there is one.
		if (error == Error::FOLDER_DOESNT_EXIST) {
			if (defaultDirToAlsoTry) {
				// ... only if we haven't already tried the alternative folder.
				if (!current_dir_for_session().equalsCaseIrrespective(defaultDirToAlsoTry)) {
					filenameToStartAt = NULL;
					Error error = current_dir_for_session().set(defaultDirToAlsoTry);
					if (error != Error::NONE) {
						return error;
					}
					goto tryReadingItems;
				}

				// Or if we have already tried it and it didn't exist, try creating it...
				else {
					// But not if we already tried.
					if (triedCreatingFolder) {
						return error;
					}
					FRESULT result = f_mkdir(defaultDirToAlsoTry);
					if (result == FR_OK) {
						triedCreatingFolder = true;
						goto tryReadingItems;
					}
					else {
						return fresultToDelugeErrorCode(result);
					}
				}
			}
		}

		return error;
	}

	if (song && outputType != OutputType::NONE) {
		error = song->addInstrumentsToFileItems(outputType);
		if (error != Error::NONE) {
			return error;
		}
	}

	if (file_items_for_session().getNumElements()) {
		sortFileItems();

		if (file_items_for_session().getNumElements()) {
			// Delete folders sharing name of file.
			// And, files sharing name of in-memory Instrument.
			if (!allow_folders_sharing_name_with_file_for_session()) {
				deleteFolderAndDuplicateItems(
				    Availability::ANY); // I think this is right - was Availability::INSTRUMENT_UNUSED until 2023-01
			}
		}
	}

	return Error::NONE;
}

namespace {
/// Adapts Browser::fileItems to the FileListView seam used by nextDefaultName().
class BrowserFileListView final : public deluge::gui::browser::FileListView {
public:
	bool contains(char const* nameWithExtension) const override {
		bool foundExact = false;
		Browser::file_items_for_session().search(nameWithExtension, &foundExact);
		return foundExact;
	}
};
} // namespace

// If OLED, then you should make sure renderUIsForOLED() gets called after this.
// outputTypeToLoad must be set before calling this.
Error Browser::arrivedInNewFolder(int32_t direction, char const* filenameToStartAt, char const* defaultDirToAlsoTry) {
	arrived_at_file_by_typing_for_session() = false;

	if (!qwertyAlwaysVisible) {
		qwerty_visible_for_session() = false;
	}

	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

tryReadingItems:
	bool doWeHaveASearchString = (filenameToStartAt && *filenameToStartAt);
	int32_t newCatalogSearchDirection = doWeHaveASearchString ? CATALOG_SEARCH_BOTH : CATALOG_SEARCH_RIGHT;
	Error error =
	    readFileItemsFromFolderAndMemory(currentSong, output_type_to_load_for_session(), filePrefix, filenameToStartAt,
	                                     defaultDirToAlsoTry, true, Availability::ANY, newCatalogSearchDirection);
	if (error != Error::NONE) {
gotErrorAfterAllocating:
		emptyFileItems();
		return error;
	}

	entered_text_edit_pos_for_session() = 0;
	if (display->haveOLED()) {
		scroll_pos_horizontal_for_session() = 0;
	}

	bool foundExact = false;
	if (file_items_for_session().getNumElements()) {
		file_index_selected_for_session() = 0;

		if (!doWeHaveASearchString) {
noExactFileFound:
			// We did not find exact file.
			// Normally, we'll need to just use one of the ones we found. (That's just always the first one, I think...)
			if (!mayDefaultToBrandNewNameOnEntry || direction) {

				// But since we're going to just use the first file, if we've deleted items at the start (meaning we had
				// a search string), we need to go back and get them.
				if (num_file_items_deleted_at_start_for_session()) {
					filenameToStartAt = NULL;
					goto tryReadingItems;
				}
setEnteredTextAndUseFoundFile:
				error = setEnteredTextFromCurrentFilename();
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating;
				}
useFoundFile:
				scroll_pos_vertical_for_session() = file_index_selected_for_session();
				// Starting in the middle or end of a short folder should still fill as many display rows as possible.
				clampFileSelectionAndScroll();

				goto everythingFinalized;
			}

			// But sometimes...
			else {
				// Ok so we didn't find an exact file, we've just entered the browser, and we're allowed new names.
				// So, choose a brand new name (if there wasn't already a new one nominated).
				goto pickBrandNewNameIfNoneNominated;
			}
		}

		int32_t i = file_items_for_session().search(filenameToStartAt, &foundExact);
		if (!foundExact) {
			goto noExactFileFound;
		}

		file_index_selected_for_session() = i;

		// Usually we'll just use that file.
		if (!mayDefaultToBrandNewNameOnEntry || direction) {
			goto setEnteredTextAndUseFoundFile;
		}

		// We found an exact file. But if we've just entered the Browser and are allowed, then we need to find a new
		// subslot variation. Come up with a new name variation.
		error = setEnteredTextFromCurrentFilename();
		if (error != Error::NONE) {
			goto gotErrorAfterAllocating;
		}
		// Come up with a new name variation. Names are display-agnostic ("SONG185", never "185"), so this is one
		// code path for both displays - see default_name.h.
		{
			BrowserFileListView file_list_view;
			// Only songs earn letter suffixes; presets pass an empty slotPrefix and take the numeric suffix path,
			// preserving existing preset behaviour.
			char const* slotPrefix = (filePrefix && !memcasecmp(filePrefix, "SONG", 4)) ? filePrefix : "";
			std::string newName =
			    deluge::gui::browser::nextDefaultName(entered_text_for_session().get(), slotPrefix, file_list_view);
			if (newName == entered_text_for_session().get()) {
				goto useFoundFile; // No free variation available - stay on the file we found.
			}
			error = entered_text_for_session().set(newName.c_str());
			if (error != Error::NONE) {
				goto gotErrorAfterAllocating;
			}
			entered_text_edit_pos_for_session() = entered_text_for_session().getLength();
		}
	}

	// Or if no files found at all...
	else {
		// Can we just pick a brand new name?
		if (mayDefaultToBrandNewNameOnEntry && !direction) {
pickBrandNewNameIfNoneNominated:
			if (entered_text_for_session().isEmpty()) {
				error = getUnusedSlot(OutputType::NONE, &entered_text_for_session(), filePrefix);
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating;
				}
				// Note - this is only hit if we're saving the first song created on boot (because the default name
				// won't match anything) Because that will have cleared out all the FileItems, we need to get them
				// again. Actually there would kinda be a way around doing this...
				error = readFileItemsFromFolderAndMemory(currentSong, OutputType::NONE, "SONG",
				                                         entered_text_for_session().get(), NULL, true,
				                                         Availability::ANY, CATALOG_SEARCH_BOTH);
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating;
				}
			}
		}
		else {
			entered_text_for_session().clear();
		}
	}

useNonExistentFileName:                     // Normally this will get skipped over - if we found a file.
	file_index_selected_for_session() = -1; // No files.
	scroll_pos_vertical_for_session() = 0;

everythingFinalized:
	folderContentsReady(direction);

	if (display->have7SEG()) {
		displayText();
	}
	return Error::NONE;
}

// You must set currentDir before calling this.
Error Browser::getUnusedSlot(OutputType outputType, String* newName, char const* thingName) {

	Error error;
	// Names always carry the prefix now, on both displays, so there is one search key.
	// Sean: thingName is usually max 4 chars (e.g. SONG, SYNT, KIT), but can be longer
	// - e.g. "PATTERN" with pattern browser or "MIDIDEVICE" with midi device definition browser.
	// it's used for proposing the file name and on 7SEG if you type a # it will prefix it
	uint8_t buffer_size = 20;
	char filenameToStartAt[buffer_size];
	strncpy(filenameToStartAt, thingName, buffer_size - 2); // Leave 2 chars for "colon + null terminator"
	strcat(filenameToStartAt, ":");                         // Colon is the first character after the digits.
	error = readFileItemsFromFolderAndMemory(currentSong, outputType, getThingName(outputType), filenameToStartAt, NULL,
	                                         false, Availability::ANY, CATALOG_SEARCH_LEFT);

	if (error != Error::NONE) {
doReturn:
		return error;
	}

	sortFileItems();

	{
		int32_t freeSlotNumber = 1;
		int32_t minNumDigits = 1;
		if (file_items_for_session().getNumElements()) {
			FileItem* fileItem =
			    (FileItem*)file_items_for_session().getElementAddress(file_items_for_session().getNumElements() - 1);
			String filename;
			error = fileItem->getFilenameWithoutExtension(&filename);
			if (error != Error::NONE) {
				goto emptyFileItemsAndReturn;
			}
			char const* readingChar = &filename.get()[strlen(thingName)];
			freeSlotNumber = 0;
			minNumDigits = 0;
			while (*readingChar >= '0' && *readingChar <= '9') {
				freeSlotNumber *= 10;
				freeSlotNumber += *readingChar - '0';
				minNumDigits++;
				readingChar++;
			}
			freeSlotNumber++;
		}

		error = newName->set(thingName);
		if (error != Error::NONE) {
			goto emptyFileItemsAndReturn;
		}
		error = newName->concatenateInt(freeSlotNumber, minNumDigits);
	}

emptyFileItemsAndReturn:
	emptyFileItems();
	goto doReturn;
}

void Browser::selectEncoderAction(int8_t offset) {
	arrived_at_file_by_typing_for_session() = false;

	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_HORIZONTAL_SCROLL) {
		return; // This was from SampleBrowser. Is it still necessary?
	}

	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	int32_t newFileIndex;

	if (file_index_selected_for_session() < 0) { // If no file selected and we were typing a new name?
		if (!file_items_for_session().getNumElements()) {
			return;
		}

		newFileIndex = file_items_for_session().search(entered_text_for_session().get());
		if (offset < 0) {
			newFileIndex--;
		}
	}
	else {
		// If user is holding shift, skip past any subslots. And the user may have chosen one digit to "edit" (7SEG
		// only - numberEditPos is -1 otherwise).
		//
		// Names always carry the file prefix, so there is one path here, not one per display. (The two branches this
		// replaced were each written for the *other* display's convention, leaving both dead.)
		int32_t numberEditPosNow = number_edit_pos_for_session();
		if (Buttons::isShiftButtonPressed() && numberEditPosNow == -1) {
			numberEditPosNow = 0;
		}

		if (numberEditPosNow != -1) {
			char const* numberPart = nameAfterPrefix(entered_text_for_session().get());
			if (!numberPart) {
				goto nonNumeric;
			}
			Slot thisSlot = getSlot(numberPart);
			if (thisSlot.slot < 0) {
				goto nonNumeric;
			}
			thisSlot.subSlot = -1;

			switch (numberEditPosNow) {
			case 0:
				thisSlot.slot += offset;
				break;

			case 1:
				thisSlot.slot = (thisSlot.slot / 10 + offset) * 10;
				break;

			case 2:
				thisSlot.slot = (thisSlot.slot / 100 + offset) * 100;
				break;

			default:
				__builtin_unreachable();
			}

			int32_t filePrefixLength = strlen(filePrefix);
			char searchString[16];
			memcpy(searchString, filePrefix, filePrefixLength);
			char* searchStringNumbersStart = searchString + filePrefixLength;
			intToString(thisSlot.slot, searchStringNumbersStart, 1);
			if (offset < 0) {
				char* pos = strchr(searchStringNumbersStart, 0);
				*pos = 'A';
				pos++;
				*pos = 0;
			}
			newFileIndex = file_items_for_session().search(searchString);
			if (offset < 0) {
				newFileIndex--;
			}
		}
		else {
nonNumeric:
			newFileIndex = file_index_selected_for_session() + offset;
		}
	}

	int32_t newCatalogSearchDirection;
	Error error;

	if (newFileIndex < 0) {
		D_PRINTLN("index below 0");
		if (num_file_items_deleted_at_start_for_session()) {
			scroll_pos_vertical_for_session() = 9999;

tryReadingItems:
			D_PRINTLN("reloading");
			error = readFileItemsFromFolderAndMemory(currentSong, output_type_to_load_for_session(), filePrefix,
			                                         entered_text_for_session().get(), NULL, true, Availability::ANY,
			                                         CATALOG_SEARCH_BOTH);
			if (error != Error::NONE) {
gotErrorAfterAllocating:
				D_PRINTLN("error while reloading, emptying file items");
				emptyFileItems();
				return;
				// TODO - need to close UI or something?
			}

			newFileIndex = file_items_for_session().search(entered_text_for_session().get()) + offset;
			D_PRINTLN("new file Index is %d", newFileIndex);
		}

		else if (!shouldWrapFolderContents && display->have7SEG()) {
			return;
		}

		else { // Wrap to end
			scroll_pos_vertical_for_session() = 0;

			if (num_file_items_deleted_at_end_for_session()) {
				newCatalogSearchDirection = CATALOG_SEARCH_LEFT;
searchFromOneEnd:
				D_PRINTLN("reloading and wrap");
				error = readFileItemsFromFolderAndMemory(currentSong, output_type_to_load_for_session(), filePrefix,
				                                         NULL, NULL, true, Availability::ANY,
				                                         newCatalogSearchDirection); // Load from start
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating;
				}

				newFileIndex = (newCatalogSearchDirection == CATALOG_SEARCH_LEFT)
				                   ? (file_items_for_session().getNumElements() - 1)
				                   : 0;
			}
			else {
				newFileIndex = file_items_for_session().getNumElements() - 1;
			}
		}
	}

	else if (newFileIndex >= file_items_for_session().getNumElements()) {
		D_PRINTLN("out of file items");
		if (num_file_items_deleted_at_end_for_session()) {
			scroll_pos_vertical_for_session() = 0;
			goto tryReadingItems;
		}

		else if (!shouldWrapFolderContents && display->have7SEG()) {
			return;
		}

		else {
			scroll_pos_vertical_for_session() = 9999;

			if (num_file_items_deleted_at_start_for_session()) {
				newCatalogSearchDirection = CATALOG_SEARCH_RIGHT;
				goto searchFromOneEnd;
			}
			else {
				newFileIndex = 0;
			}
		}
	}

	if (!qwertyAlwaysVisible) {
		qwerty_visible_for_session() = false;
	}

	file_index_selected_for_session() = newFileIndex;
	// A fast turn may be delivered as a multi-file offset; after a folder-window re-read, that offset can still
	// overshoot.
	clampFileSelectionAndScroll(false);
	if (file_index_selected_for_session() == -1) {
		return;
	}

	entered_text_edit_pos_for_session() = 0;
	if (display->haveOLED()) {
		scroll_pos_horizontal_for_session() = 0;
	}
	else {
		char const* oldCharAddress = entered_text_for_session().get();
		char const* newCharAddress = getCurrentFileItem()->displayName; // Will have file extension, so beware...
		while (true) {
			char oldChar = *oldCharAddress;
			char newChar = *newCharAddress;

			if (oldChar >= 'A' && oldChar <= 'Z') {
				oldChar += 32;
			}
			if (newChar >= 'A' && newChar <= 'Z') {
				newChar += 32;
			}

			if (oldChar != newChar) {
				break;
			}
			oldCharAddress++;
			newCharAddress++;
			entered_text_edit_pos_for_session()++;
		}
	}

	error = setEnteredTextFromCurrentFilename();
	if (error != Error::NONE) {
		display->displayError(error);
		return;
	}

	displayText();
	// currentFileChanged uses the value as a scroll-animation direction, so give it only the sign.
	currentFileChanged(offset > 0 ? 1 : (offset < 0 ? -1 : 0));
}

bool Browser::predictExtendedText() {
	Error error;
	arrived_at_file_by_typing_for_session() = true;
	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	// Names always carry the file prefix, but on 7SEG the user only ever sees and types the number ("185"). When
	// typing begins with a digit, treat the prefix as implicitly typed - otherwise "1" would match nothing. The typed
	// portion of enteredText is [0, enteredTextEditPos), so the prefix has to go *into* enteredText and be counted,
	// not merely prepended to the search key.
	if (display->have7SEG() && filePrefix && entered_text_edit_pos_for_session() > 0) {
		char const* typed = entered_text_for_session().get();
		if (typed[0] >= '0' && typed[0] <= '9') {
			int32_t prefixLength = strlen(filePrefix);
			String prefixed;
			error = prefixed.set(filePrefix);
			if (error == Error::NONE) {
				error = prefixed.concatenate(&entered_text_for_session());
			}
			if (error != Error::NONE) {
				// Must not advance enteredTextEditPos here: it indexes into enteredText, and a short/stale string with
				// an advanced edit pos would make the shorten() and memcasecmp() below read out of bounds.
				display->displayError(error);
				return false;
			}
			entered_text_for_session().set(&prefixed); // Cannot fail - takes ownership of the already-allocated buffer.
			entered_text_edit_pos_for_session() += prefixLength;
		}
	}

	FileItem* oldFileItem = getCurrentFileItem();
	DWORD oldClust = 0;
	if (oldFileItem) {
		oldClust = oldFileItem->filePointer.sclust;
	}

	String searchString;
	searchString.set(&entered_text_for_session());
	bool doneNewRead = false;
	error = searchString.shorten(entered_text_edit_pos_for_session());
	if (error != Error::NONE) {
gotError:
		display->displayError(error);
		return false;
	}

	int32_t numExtraZeroesAdded = 0;

	// Change 2026-03-29 - this code used to append a tilde to the search string. The tilde would match after all
	// printable characters so it would return 1 greater than the index of the search result we wanted. unfortunately
	// for reasons I don't fully understand (7seg compatibility maybe?) multi digit integers are compared as one number
	// rather than character by character, so if you had files named "SONG1", "SONG2", "SONG10", and you typed in
	// "SONG1", and then pressed the encoder to try to get it to predict "SONG10", it would instead match "SONG2"
	// because 2 is the closest number to 1 that comes after 1. So now we just search for the string as is. The major
	// impact is this now returns the first match instead of the last match.
doSearch:
	int32_t i = file_items_for_session().search(searchString.get());

	// If that search takes us off the right-hand end of the list...
	if (i >= file_items_for_session().getNumElements()) {

		// If we haven't yet done a whole new read from the SD card etc, from within this function, do that now.
		if (!doneNewRead) {
doNewRead:
			doneNewRead = true;
			error = readFileItemsFromFolderAndMemory(
			    currentSong, output_type_to_load_for_session(), filePrefix, searchString.get(), NULL, true,
			    Availability::ANY,
			    CATALOG_SEARCH_BOTH); // This could probably actually be made to work with searching left only...
			if (error != Error::NONE) {
gotErrorAfterAllocating:
				emptyFileItems();
				goto gotError;
				// TODO - need to close UI or something?
			}
			goto doSearch;
		}

		// Otherwise if we already tried that, then our whole search is fruitless.
notFound:
		if (display->haveOLED() && !mayDefaultToBrandNewNameOnEntry) {
			if (file_index_selected_for_session() >= 0) {
				setEnteredTextFromCurrentFilename(); // Set it back
			}
			return false;
		}

		file_index_selected_for_session() = -1;
		return true;
	}

	// The search returns the index where searchString would be inserted
	// Now check if the file at index i actually matches our prefix. Covers any shenanigans with the weird string
	// matching
	FileItem* fileItem = (FileItem*)file_items_for_session().getElementAddress(i);

	// If it didn't match exactly, that's ok, but we need to try some other stuff before we accept that result.
	if (memcasecmp(fileItem->displayName, entered_text_for_session().get(), entered_text_edit_pos_for_session())) {
		// If the search landed on the first cached item, the folder cache may be missing earlier entries.
		if (i == 0 && !doneNewRead) {
			goto doNewRead;
		}

		// this code is original but I don't know what it does. Slot browser maybe?
		// Just updated to append to the string instead of replacing the tilde
		if (numExtraZeroesAdded < 4) {
			error = searchString.concatenateAtPos("0", searchString.getLength(), 1);
			if (error != Error::NONE) {
				goto gotError;
			}
			numExtraZeroesAdded++;
			doneNewRead = false;
			goto doSearch;
		}
		else {
			goto notFound;
		}
	}

	file_index_selected_for_session() = i;

	// Typing/prediction can land on a cached item without needing to move the viewport unless it is offscreen.
	clampFileSelectionAndScroll();

	error = setEnteredTextFromCurrentFilename();
	if (error != Error::NONE) {
		goto gotError;
	}

	displayText();

	// If we're now on a different file than before, preview it
	if (fileItem->filePointer.sclust != oldClust) {
		currentFileChanged(0);
	}

	return true;
}

void Browser::currentFileDeleted() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return; // Shouldn't happen...
	}
	if (currentFileItem->instrument && !currentFileItem->instrumentAlreadyInSong) {
		currentFileItem->instrument->shouldHibernate = false;
	}
	currentFileItem->~FileItem();

	file_items_for_session().deleteAtIndex(file_index_selected_for_session());

	if (file_index_selected_for_session() == file_items_for_session().getNumElements()) {
		file_index_selected_for_session()--; // It might go to -1 if no files left.
		entered_text_for_session().clear();
		entered_text_edit_pos_for_session() = 0;
	}
	else {
		setEnteredTextFromCurrentFilename();
	}
	// Deleting the last visible item can leave the top row past the new end of the list.
	clampFileSelectionAndScroll();
	currentFileChanged(0);
}

void Browser::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	canvas.drawScreenTitle(title);

	int32_t textStartX = 14;
	int32_t iconStartX = 1;
	if (FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION) {
		textStartX += kTextSpacingX;
		iconStartX = kTextSpacingX;
	}

	int32_t yPixel = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	yPixel += OLED_MAIN_TOPMOST_PIXEL;

	int32_t maxChars = (uint32_t)(OLED_MAIN_WIDTH_PIXELS - textStartX) / (uint32_t)kTextSpacingX;

	bool isFolder = false;
	bool isSelectedIndex = true;
	char const* displayName;
	int32_t o;
	// Use the display contract for browser/menu rows instead of assuming the OLED character-grid height.
	int32_t visibleRows = display->getNumBrowserAndMenuLines();
	if (visibleRows < 1) {
		visibleRows = 1;
	}

	// If we're currently typing a filename which doesn't (yet?) have a file...
	if (file_index_selected_for_session() == -1) {
		displayName = entered_text_for_session().get();
		o = visibleRows; // Make sure below loop doesn't keep looping.
		goto drawAFile;
	}

	else {
		for (o = 0; o < visibleRows; o++) {
			{
				int32_t i = o + scroll_pos_vertical_for_session();

				if (i >= file_items_for_session().getNumElements()) {
					break;
				}

				FileItem* thisFile = (FileItem*)file_items_for_session().getElementAddress(i);
				isFolder = thisFile->isFolder;
				displayName = thisFile->filename.get();
				isSelectedIndex = (i == file_index_selected_for_session());
			}
drawAFile:
			// Draw graphic
			int32_t iconWidth = 8;
			uint8_t const* graphic = isFolder ? deluge::hid::display::OLED::folderIcon : fileIcon;
			canvas.drawGraphicMultiLine(graphic, iconStartX, yPixel + 0, iconWidth);
			if (!isFolder && fileIconPt2 && fileIconPt2Width) {
				canvas.drawGraphicMultiLine(fileIconPt2, iconStartX + iconWidth, yPixel + 0, fileIconPt2Width);
			}

			// Draw filename
			char finalChar = isFolder ? 0 : '.';
searchForChar:
			char const* finalCharAddress = strrchr(displayName, finalChar);
			if (!finalCharAddress) { // Shouldn't happen... or maybe for in-memory presets?
				finalChar = 0;
				goto searchForChar;
			}

			int32_t displayStringLength = (uintptr_t)finalCharAddress - (uintptr_t)displayName;

			if (isSelectedIndex) {
				drawTextForOLEDEditing(textStartX, OLED_MAIN_WIDTH_PIXELS, yPixel, maxChars, canvas);
				if (!entered_text_edit_pos_for_session()) {
					deluge::hid::display::OLED::setupSideScroller(0, entered_text_for_session().get(), textStartX,
					                                              OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
					                                              kTextSpacingX, kTextSpacingY, true);
				}
			}
			else {
				canvas.drawString(std::string_view{displayName, static_cast<size_t>(displayStringLength)}, textStartX,
				                  yPixel, kTextSpacingX, kTextSpacingY);
			}

			yPixel += kTextSpacingY;
		}
	}
}

void Browser::clampFileSelectionAndScroll(bool allowNoFileSelection) {
	int32_t numFileItems = file_items_for_session().getNumElements();
	if (numFileItems <= 0) {
		// No cached files means there is no real selection, and the viewport must reset to the top.
		file_index_selected_for_session() = -1;
		scroll_pos_vertical_for_session() = 0;
		return;
	}

	if (file_index_selected_for_session() >= numFileItems) {
		// A large encoder offset can overshoot the freshly cached window; land on the last cached item instead.
		file_index_selected_for_session() = numFileItems - 1;
	}
	else if (file_index_selected_for_session() < 0) {
		// -1 is valid only while typing a new name; encoder browsing must stay on a real cached file.
		file_index_selected_for_session() = allowNoFileSelection ? -1 : 0;
	}

	// Fast encoder turns can arrive as multi-file jumps after the cached folder window has been re-read.
	// Keep both the selection and the visible window inside the files we actually have.
	int32_t visibleRows = display->getNumBrowserAndMenuLines();
	if (visibleRows < 1) {
		// Defensive fallback for mock or future displays; the scroll math needs at least one visible row.
		visibleRows = 1;
	}
	int32_t lastAllowedScroll = numFileItems - visibleRows;
	if (lastAllowedScroll < 0) {
		// Short folders cannot fill every row, so their top visible row is always the first item.
		lastAllowedScroll = 0;
	}

	if (file_index_selected_for_session() == -1) {
		// While typing a new name, the rendered row is enteredText rather than an item from fileItems.
		scroll_pos_vertical_for_session() = 0;
		return;
	}

	if (scroll_pos_vertical_for_session() > file_index_selected_for_session()) {
		// The selected item is above the current viewport; move it to the first visible row.
		scroll_pos_vertical_for_session() = file_index_selected_for_session();
	}
	else if (scroll_pos_vertical_for_session() < file_index_selected_for_session() - visibleRows + 1) {
		// The selected item is below the current viewport; move it to the last visible row.
		scroll_pos_vertical_for_session() = file_index_selected_for_session() - visibleRows + 1;
	}

	if (scroll_pos_vertical_for_session() > lastAllowedScroll) {
		// Keep the viewport from starting so low that the bottom browser rows would be blank.
		scroll_pos_vertical_for_session() = lastAllowedScroll;
	}
	if (scroll_pos_vertical_for_session() < 0) {
		// Short-folder and typing cases can make the intermediate top row negative; clamp back to the start.
		scroll_pos_vertical_for_session() = 0;
	}
}

// Names always carry the file prefix (e.g. "SONG185"). Only rendering strips it, so anything wanting the numeric part
// goes through here first. Zero-padding is skipped too - getSlot() cannot parse "001". See default_name.h.
char const* Browser::nameAfterPrefix(char const* name) const {
	return deluge::gui::browser::numberPartOf(name, filePrefix);
}

// Supply a string with no prefix (e.g. SONG), and no file extension.
// If name is non-numeric, a slot of -1 will be returned.
Slot Browser::getSlot(char const* displayName) {

	char const* charPos = displayName;
	if (*charPos == '0') { // If first digit is 0, then no more digits allowed.
		charPos++;
	}
	else { // Otherwise, up to 3 digits allowed.
		while (*charPos >= '0' && *charPos <= '9' && charPos < (displayName + 3)) {
			charPos++;
		}
	}

	int32_t numDigitsFound = charPos - displayName;

	Slot toReturn;

	if (!numDigitsFound) { // We are required to have found at least 1 digit.
nonNumeric:
		toReturn.slot = -1;
doReturn:
		return toReturn;
	}

	char thisSlotNumber[4];
	memcpy(thisSlotNumber, displayName, numDigitsFound);
	thisSlotNumber[numDigitsFound] = 0;
	toReturn.slot = stringToInt(thisSlotNumber);

	// Get the file's subslot
	uint8_t subSlotChar = *charPos;
	switch (subSlotChar) {

	case 'a' ... 'z':
		subSlotChar -= 32;
		// No break.

	case 'A' ... 'Z': {
		toReturn.subSlot = subSlotChar - 'A';
		charPos++;
		char nextChar = *charPos;
		if (nextChar) {
			goto nonNumeric; // Ensure no more characters
		}
		break;
	}

		// case '.':
		// if (strchr(charPos + 1, '.')) goto nonNumeric; // Ensure no more dots after this dot.
		//  No break.

	case 0:
		toReturn.subSlot = -1;
		break;

	default:
		goto nonNumeric; // Ensure no more characters
	}

	goto doReturn;
}

void Browser::displayText(bool blinkImmediately) {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		if (arrived_at_file_by_typing_for_session() || qwerty_visible_for_session()) {
			if (!arrived_at_file_by_typing_for_session()) {
				// This means a key has been hit while browsing
				// to bring up the keyboard, so set position to -1
				// this might not be neccesary?
				number_edit_pos_for_session() = -1;
			}
			QwertyUI::displayText(blinkImmediately);
		}
		else {
			if (entered_text_for_session().isEmpty() && file_index_selected_for_session() == -1) {
				display->setText("----");
			}
			else {
				// A name is always the full on-card name ("SONG185"). On 7SEG we render the numeric part alone
				// ("185") so it fits the four-character display.
				char const* numberPart = nameAfterPrefix(entered_text_for_session().get());
				if (numberPart) {

					Slot thisSlot = getSlot(numberPart);
					if (thisSlot.slot >= 0) {
						display->setTextAsSlot(thisSlot.slot, thisSlot.subSlot,
						                       (file_index_selected_for_session() != -1), true,
						                       number_edit_pos_for_session(), blinkImmediately);
						return;
					}
				}
				int16_t scrollStart = entered_text_edit_pos_for_session();
				// if the first difference would be visible on
				// screen anyway, start scroll from the beginning
				if (entered_text_edit_pos_for_session() < 3) {
					scrollStart = 0;
				}
				else {
					// provide some context in case the post-fix is long
					scrollStart = entered_text_edit_pos_for_session() - 2;
				}
				FileItem* currentFileItem = getCurrentFileItem();
				bool currentItemIsFolder = currentFileItem && currentFileItem->isFolder;
				auto dotPos = currentItemIsFolder ? 3 : 255;
				scrolling_text_for_session() =
				    display->setScrollingText(entered_text_for_session().get(), scrollStart, 600, -1, dotPos);
			}
		}
	}
}

FileItem* Browser::getCurrentFileItem() {
	if (file_index_selected_for_session() == -1) {
		return nullptr;
	}
	return (FileItem*)file_items_for_session().getElementAddress(file_index_selected_for_session());
}

// This and its individual contents are frequently overridden by child classes.
ActionResult Browser::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Select encoder
	if (b == SELECT_ENC) {
		return mainButtonAction(on);
	}

	// Save button, to delete file
	else if (b == SAVE && Buttons::isShiftButtonPressed()) {
		if (!currentUIMode && on) {
			FileItem* currentFileItem = getCurrentFileItem();
			if (currentFileItem) {
				if (currentFileItem->isFolder) {
					display->displayPopup(
					    deluge::l10n::get(deluge::l10n::String::STRING_FOR_FOLDERS_CANNOT_BE_DELETED_ON_THE_DELUGE));
					return ActionResult::DEALT_WITH;
				}
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				// deletes the underlying item
				goIntoDeleteFileContextMenu();
			}
		}
	}

	// Back button
	else if (b == BACK) {
		if (on && !currentUIMode) {
			return backButtonAction();
		}
	}
	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

ActionResult Browser::padAction(int32_t x, int32_t y, int32_t on) {
	bool inFavouriteOrBanksColumn = (x >= 0 && x < static_cast<int32_t>(kNumFavourites));

	if (isFavouritesVisible() && inFavouriteOrBanksColumn && y == favourite_row_for_session()) {
		if (on) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (Buttons::isShiftButtonPressed()) {
				String filePath;
				Error error = getCurrentFilePath(&filePath);
				if (error != Error::NONE) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_ERROR_FILE_NOT_FOUND));
				}
				if (favouritesManager.isEmpty(x)) {
					if (!getCurrentFileItem()->isFolder) {
						favouritesManager.setFavourite(x, FavouritesManager::favouriteDefaultColor, filePath.get());
						favouritesChanged();
					}
				}
				else {
					favouritesManager.unsetFavourite(x);
					favouritesChanged();
				}
			}
			else {
				const std::string favouritePath = favouritesManager.getFavouriteFilename(x);
				favouritesChanged();
				if (!favouritePath.empty()) {
					setFileByFullPath(output_type_to_load_for_session(), favouritePath.c_str());
				}
				else {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_FAVOURITES_EMPTY));
				}
			}
		}
		return ActionResult::DEALT_WITH;
	}
	else if (isBanksVisible() && inFavouriteOrBanksColumn && y == favouriteBankRow) {
		if (on) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			favouritesManager.selectFavouritesBank(x);
			favouritesChanged();
			return ActionResult::DEALT_WITH;
		}
	}
	else {
		return QwertyUI::padAction(x, y, on);
	}
	return ActionResult::DEALT_WITH;
}

void Browser::favouritesChanged() {
	renderFavourites();
}

ActionResult Browser::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (isFavouritesVisible()) {
		if (Buttons::isShiftButtonPressed()) {
			if (favouritesManager.current_favourite_number_for_session().has_value()) {
				favouritesManager.changeColour(favouritesManager.current_favourite_number_for_session().value(),
				                               offset);
				favouritesChanged();
			}
		}
	}
	return ActionResult::DEALT_WITH;
}

ActionResult Browser::mainButtonAction(bool on) {
	// Press down
	if (on) {
		if (currentUIMode == UI_MODE_NONE) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, LONG_PRESS_DURATION);
			currentUIMode = UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS;
		}
	}

	// Release press
	else {
		if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			currentUIMode = UI_MODE_NONE;
			uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
			enterKeyPress();
		}
	}

	return ActionResult::DEALT_WITH;
}

// Virtual function - may be overridden, by child classes that need to do more stuff, e.g. SampleBrowser needs to mute
// any previewing Sample.
ActionResult Browser::backButtonAction() {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	Error error = goUpOneDirectoryLevel();
	if (error != Error::NONE) {
		exitAction();
	}

	return ActionResult::DEALT_WITH;
}

// Virtual function - may be overridden, by child classes that need to do more stuff on exit.
void Browser::exitAction() {
	close();
}

void Browser::goIntoDeleteFileContextMenu() {
	using namespace gui;
	bool available = context_menu::delete_file_for_session().setupAndCheckAvailability();

	if (available) {
		display->setNextTransitionDirection(1);
		openUI(&context_menu::delete_file_for_session());
	}
}

Error Browser::setEnteredTextFromCurrentFilename() {
	FileItem* currentFileItem = getCurrentFileItem();

	Error error = entered_text_for_session().set(currentFileItem->displayName);
	if (error != Error::NONE) {
		return error;
	}

	// Cut off the file extension
	if (!currentFileItem->isFolder) {
		char const* enteredTextChars = entered_text_for_session().get();
		char const* dotAddress = strrchr(enteredTextChars, '.');
		if (dotAddress) {
			int32_t dotPos = (uintptr_t)dotAddress - (uintptr_t)enteredTextChars;
			error = entered_text_for_session().shorten(dotPos);
			if (error != Error::NONE) {
				return error;
			}
		}
	}

	return Error::NONE;
}

Error Browser::goIntoFolder(char const* folderName) {
	Error error;

	if (!current_dir_for_session().isEmpty()) {
		error = current_dir_for_session().concatenate("/");
		if (error != Error::NONE) {
			return error;
		}
	}

	error = current_dir_for_session().concatenate(folderName);
	if (error != Error::NONE) {
		return error;
	}

	entered_text_for_session().clear();
	entered_text_edit_pos_for_session() = 0;

	display->setNextTransitionDirection(1);
	error = arrivedInNewFolder(1);
	if (display->haveOLED()) {
		if (error == Error::NONE) {
			renderUIsForOled();
		}
	}

	return error;
}

Error Browser::goUpOneDirectoryLevel() {

	char const* currentDirChars = current_dir_for_session().get();
	char const* slashAddress = strrchr(currentDirChars, '/');
	if (!slashAddress || slashAddress == currentDirChars) {
		return Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP;
	}

	int32_t slashPos = (uintptr_t)slashAddress - (uintptr_t)currentDirChars;
	Error error = entered_text_for_session().set(slashAddress + 1);
	if (error != Error::NONE) {
		return error;
	}
	current_dir_for_session().shorten(slashPos);
	if (error != Error::NONE) {
		return error;
	}
	entered_text_edit_pos_for_session() = 0;

	display->setNextTransitionDirection(-1);
	error = arrivedInNewFolder(-1, entered_text_for_session().get());
	if (display->haveOLED()) {
		if (error == Error::NONE) {
			renderUIsForOled();
		}
	}
	return error;
}

Error Browser::createFolder() {
	displayText();

	String newDirPath;
	Error error;

	newDirPath.set(&current_dir_for_session());
	if (!newDirPath.isEmpty()) {
		error = newDirPath.concatenate("/");
		if (error != Error::NONE) {
			return error;
		}
	}

	error = newDirPath.concatenate(&entered_text_for_session());
	if (error != Error::NONE) {
		return error;
	}

	FRESULT result = f_mkdir(newDirPath.get());
	if (result) {
		return Error::SD_CARD;
	}

	error = goIntoFolder(entered_text_for_session().get());

	return error;
}

Error Browser::createFoldersRecursiveIfNotExists(const char* path) {
	if (!path || *path == '\0') {
		return Error::UNSPECIFIED;
	}

	char tempPath[256];
	size_t len = 0;

	// Iterate through the path and create directories step by step
	for (const char* p = path; *p; ++p) {
		tempPath[len++] = *p;
		tempPath[len] = '\0';

		if (*p == '/' || *(p + 1) == '\0') {
			FRESULT result = f_mkdir(tempPath);
			if (result != FR_OK && result != FR_EXIST) {
				return fresultToDelugeErrorCode(FR_NO_PATH);
			}
		}
	}
	return Error::NONE;
}

void Browser::sortFileItems() {
	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	file_items_for_session().sortForStrings();

	// If we're just wanting to look to one side or the other of a given filename, then delete everything in the other
	// direction.
	if (filename_to_start_search_at_for_session() && *filename_to_start_search_at_for_session()) {

		if (catalogSearchDirection == CATALOG_SEARCH_LEFT) {
			bool foundExact;
			int32_t searchIndex =
			    file_items_for_session().search(filename_to_start_search_at_for_session(), &foundExact);
			// Check for duplicates.
			if (foundExact) {
				int32_t prevIndex = searchIndex - 1;
				if (prevIndex >= 0) {
					FileItem* prevItem = (FileItem*)file_items_for_session().getElementAddress(prevIndex);
					if (!strcmpspecial(prevItem->displayName, filename_to_start_search_at_for_session())) {
						searchIndex = prevIndex;
					}
				}
			}
			int32_t numToDelete = file_items_for_session().getNumElements() - searchIndex;
			if (numToDelete > 0) {
				deleteSomeFileItems(searchIndex, file_items_for_session().getNumElements());
				num_file_items_deleted_at_end_for_session() += numToDelete;
			}
		}
		else if (catalogSearchDirection == CATALOG_SEARCH_RIGHT) {
			bool foundExact;
			int32_t searchIndex =
			    file_items_for_session().search(filename_to_start_search_at_for_session(), &foundExact);
			// Check for duplicates.
			if (foundExact) {
				int32_t nextIndex = searchIndex + 1;
				if (nextIndex < file_items_for_session().getNumElements()) {
					FileItem* nextItem = (FileItem*)file_items_for_session().getElementAddress(nextIndex);
					if (!strcmpspecial(nextItem->displayName, filename_to_start_search_at_for_session())) {
						searchIndex = nextIndex;
					}
				}
			}
			int32_t numToDelete = searchIndex + (int32_t)foundExact;
			if (numToDelete > 0) {
				deleteSomeFileItems(0, numToDelete);
				num_file_items_deleted_at_start_for_session() += numToDelete;
			}
		}
	}

	// If we'd previously deleted items from either end of the list (apart from due to search direction as above),
	// we need to now delete any items which would have fallen in that region.
	if (!last_file_item_remaining_for_session().isEmpty()) {
		int32_t searchIndex = file_items_for_session().search(last_file_item_remaining_for_session().get());
		int32_t itemsToDeleteAtEnd = file_items_for_session().getNumElements() - searchIndex - 1;
		if (itemsToDeleteAtEnd > 0) {
			deleteSomeFileItems(searchIndex + 1, file_items_for_session().getNumElements());
			num_file_items_deleted_at_end_for_session() += itemsToDeleteAtEnd;
		}
	}

	if (!first_file_item_remaining_for_session().isEmpty()) {
		int32_t itemsToDeleteAtStart = file_items_for_session().search(first_file_item_remaining_for_session().get());
		if (itemsToDeleteAtStart) {
			deleteSomeFileItems(0, itemsToDeleteAtStart);
			num_file_items_deleted_at_start_for_session() += itemsToDeleteAtStart;
		}
	}
}

bool Browser::isFavouritesVisible() {
	return (getCurrentUI()->canDisplayFavourites() && qwerty_visible_for_session()
	        && FlashStorage::defaultFavouritesLayout != FavouritesDefaultLayout::FavouritesDefaultLayoutOff);
}

bool Browser::isBanksVisible() {
	return (getCurrentUI()->canDisplayFavourites() && qwerty_visible_for_session()
	        && FlashStorage::defaultFavouritesLayout
	               == FavouritesDefaultLayout::FavouritesDefaultLayoutFavouritesAndBanks);
}
