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

#pragma once

#include "gui/ui/ui.h"
#include "model/favourite/favourite_manager.h"
#include "util/d_string.h"
#include <cstdint>

class QwertyUI : public UI {
public:
	QwertyUI() = default;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult timerCallback() override;
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr,
	                    bool drawUndefinedArea = true) override {
		return true;
	}

	static bool& prediction_interrupted_for_session();
	static String& entered_text_for_session();

protected:
	bool opened() override;
	virtual bool predictExtendedText() { return true; } // Returns whether we're allowed that new character.
	void drawKeys();
	virtual void processBackspace(); // May be called in card routine
	virtual void enterKeyPress() = 0;

	// This may be called in card routine so long as either !currentFileExists (which is always the case in a
	// processBackspace()), or we are not LoadSongUI

	char const* title;
	void drawTextForOLEDEditing(int32_t textStartX, int32_t xPixelMax, int32_t yPixel, int32_t maxChars,
	                            deluge::hid::display::oled_canvas::Canvas& canvas);

	// 7SEG only
	virtual void displayText(bool blinkImmediately = false);

	// Favourites
	void renderFavourites();

	static uint8_t& favourite_row_for_session();
	static constexpr uint8_t favouriteBankRow = 7;

	static int16_t& entered_text_edit_pos_for_session();
	static int32_t& scroll_pos_horizontal_for_session();

private:
	static uint8_t& current_bank_for_session();
	static std::optional<uint8_t>& current_favourite_for_session();
	static FavouritesDefaultLayout& favourites_layout_selected_for_session();
	struct SessionState;
	static SessionState& session_state();
};
