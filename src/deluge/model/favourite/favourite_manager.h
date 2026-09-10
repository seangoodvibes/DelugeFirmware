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

#pragma once

#include "definitions_cxx.hpp"
#include "model/favourite/favourites_session_state.h"
#include "storage/storage_manager.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class FavouritesManager {
public:
	using Favourite = FavouritesSessionState::Favourite;

public:
	FavouritesManager();
	~FavouritesManager();

	void setCategory(const std::string& category);
	void selectFavouritesBank(uint8_t bankNumber);
	void setFavourite(uint8_t position, uint8_t colour, const std::string& filename);
	void unsetFavourite(uint8_t position);
	bool isEmpty(uint8_t position) const;
	Error loadFavouritesFromFile(Deserializer& reader);
	void close();
	std::array<std::optional<uint8_t>, kNumFavourites> getFavouriteColours() const;
	void changeColour(uint8_t position, int32_t offset);
	const std::string& getFavouriteFilename(uint8_t position);
	static constexpr uint8_t favouriteDefaultColor = 4;

	uint8_t current_bank_number_for_session() const { return sessions_.bank().number; }
	std::optional<uint8_t> current_favourite_number_for_session() const { return sessions_.selection().favourite; }

private:
	void resetFavourites();
	void loadFavouritesBank();
	void saveFavouriteBank() const;

	std::string getFilenameForSave() const;
	mutable FavouritesSessionState sessions_;
};

extern FavouritesManager favouritesManager;
