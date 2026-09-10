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

#include "model/favourite/favourite_manager.h"
#include <cstdint>
#include <string.h>

FavouritesManager favouritesManager{};

FavouritesManager::FavouritesManager() {
	resetFavourites();
}

FavouritesManager::~FavouritesManager() {
	using namespace deluge::gui::ui_session;
	for (Id owner : {Id::Local, Id::Remote}) {
		Scope scope(owner);
		if (!sessions_.bank().category.empty()) {
			saveFavouriteBank();
		}
	}
}
void FavouritesManager::close() {
	if (sessions_.bank().unsavedChanges) {
		saveFavouriteBank();
	}
	sessions_.release();
	return;
}

void FavouritesManager::resetFavourites() {
	sessions_.bank().favourites = {};
	for (uint8_t i = 0; i < kNumFavourites; i++) {
		sessions_.bank().favourites[i].position = i;
	}
}

void FavouritesManager::setCategory(const std::string& category) {
	if (sessions_.bank().unsavedChanges && !sessions_.bank().category.empty()) {
		saveFavouriteBank();
	}
	if (sessions_.select(category, 0)) {
		loadFavouritesBank();
	}
}

std::string FavouritesManager::getFilenameForSave() const {
	return "SETTINGS/FAVOURITES/" + sessions_.bank().category + "_Bank" + std::to_string(sessions_.bank().number)
	       + ".xml";
}

void FavouritesManager::loadFavouritesBank() {
	resetFavourites();
	std::string filePath = getFilenameForSave();
	FilePointer fileToLoad{};
	bool fileExists = StorageManager::fileExists(filePath.c_str(), &fileToLoad);
	if (!fileExists) {
		// Create an empty bank and keep the in-memory defaults.
		saveFavouriteBank();
		return;
	}
	String path;
	path.set(filePath.c_str());
	Error error = StorageManager::loadFavouriteFile(&fileToLoad, &path);
	if (error != Error::NONE) {
		resetFavourites();
	}
}

Error FavouritesManager::loadFavouritesFromFile(Deserializer& reader) {
	reader.match('{');
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "favourite")) {
			int32_t position = -1;
			String fileName;
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "position")) {
					position = reader.readTagOrAttributeValueInt();
					if (position >= 0 && position < kNumFavourites) {
						sessions_.bank().favourites[position].position = position;
					}
					else {
						position = -1;
					}
				}
				else if (!strcmp(tagName, "colour")) {
					int32_t colour = reader.readTagOrAttributeValueInt();
					if (position >= 0) {
						sessions_.bank().favourites[position].colour = static_cast<uint8_t>(colour);
					}
				}
				else if (!strcmp(tagName, "instrumentPresetFolder")) {
					reader.readTagOrAttributeValueString(&fileName);
					if (position >= 0) {
						sessions_.bank().favourites[position].filename = fileName.get();
					}
				}
			}
		}
	}
	return Error::NONE;
}

void FavouritesManager::saveFavouriteBank() const {
	if (sessions_.bank().category.empty()) {
		return;
	}

	std::string filePath = getFilenameForSave();
	Error error = StorageManager::createXMLFile(filePath.c_str(), smSerializer, true, true);
	if (error != Error::FILE_ALREADY_EXISTS && error != Error::NONE) {
		return;
	}

	if (sessions_.bank().favourites.empty()) {
		return;
	}

	Serializer& writer = GetSerializer();

	char buffer[9];
	writer.writeArrayStart("favourites");
	for (const auto& fav : sessions_.bank().favourites) {
		if (fav.colour.has_value()) {
			writer.writeOpeningTagBeginning("favourite");
			writer.writeAttribute("position", fav.position);
			writer.writeAttribute("colour", fav.colour.value());
			writer.writeAttribute("instrumentPresetFolder", fav.filename.c_str());
			writer.closeTag();
		}
	}
	writer.writeArrayEnding("favourites");
	error = writer.closeFileAfterWriting();
	sessions_.bank().unsavedChanges = false;
	return;
}

void FavouritesManager::selectFavouritesBank(uint8_t bankNumber) {
	if (bankNumber > 15)
		return;
	if (sessions_.bank().unsavedChanges) {
		saveFavouriteBank();
	}
	if (sessions_.select(sessions_.bank().category, bankNumber)) {
		loadFavouritesBank();
	}
}

void FavouritesManager::setFavourite(uint8_t position, uint8_t colour, const std::string& filename) {
	if (position >= kNumFavourites)
		return;
	sessions_.selection().favourite = position;
	sessions_.bank().favourites[position] = Favourite(position, static_cast<uint8_t>(colour), filename);
	saveFavouriteBank();
}

void FavouritesManager::unsetFavourite(uint8_t position) {
	if (position >= kNumFavourites)
		return;
	sessions_.selection().favourite = position;
	sessions_.bank().favourites[position] = Favourite(position, std::nullopt, "");
	saveFavouriteBank();
}

bool FavouritesManager::isEmpty(uint8_t position) const {
	if (position >= kNumFavourites || sessions_.bank().favourites.size() <= position)
		return true;
	return !sessions_.bank().favourites[position].colour.has_value();
}

std::array<std::optional<uint8_t>, kNumFavourites> FavouritesManager::getFavouriteColours() const {
	std::array<std::optional<uint8_t>, kNumFavourites> colours{};
	for (uint8_t i = 0; i < kNumFavourites && i < sessions_.bank().favourites.size(); i++) {
		colours[i] = sessions_.bank().favourites[i].colour;
	}
	return colours; // Copy elision makes this efficient
}

void FavouritesManager::changeColour(uint8_t position, int32_t offset) {
	if (position < kNumFavourites && sessions_.bank().favourites.size() > position
	    && sessions_.bank().favourites[position].colour.has_value()) {
		sessions_.bank().favourites[position].colour =
		    ((sessions_.bank().favourites[position].colour.value() + offset) % kNumFavourites + kNumFavourites)
		    % kNumFavourites;
		sessions_.bank().unsavedChanges = true;
		return;
	}
	return;
}

const std::string& FavouritesManager::getFavouriteFilename(uint8_t position) {
	static const std::string emptyString = ""; // Safe default value
	sessions_.selection().favourite = position;
	if (position >= kNumFavourites || sessions_.bank().favourites.size() <= position
	    || !sessions_.bank().favourites[position].colour.has_value()) {
		return emptyString; // Return a reference to an empty string instead of nullptr
	}
	return sessions_.bank().favourites[position].filename;
}
